#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <HX711.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_SHT4x.h>
#include <RTClib.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>

// ---------------- USER CONFIG ----------------

const char* WIFI_SSID = "YOUR_WIFI";
const char* WIFI_PASS = "YOUR_PASSWORD";

const char* API_URL = "https://your-domain.example.com/api/hive-scale";
const char* API_KEY = "CHANGE_ME_SECRET";

const char* DEVICE_ID = "hive_scale_dual_01";

const unsigned long SEND_INTERVAL_MS = 10UL * 60UL * 1000UL;

// HX711 calibration values: replace after calibration
float SCALE1_CALIBRATION = -7050.0;
float SCALE2_CALIBRATION = -7050.0;

// ---------------- PIN CONFIG ----------------

#define HX1_DOUT 16
#define HX1_SCK  17

#define HX2_DOUT 32
#define HX2_SCK  33

#define ONE_WIRE_PIN 4

#define I2C_SDA 21
#define I2C_SCL 22

#define SD_CS   5
#define SD_SCK  18
#define SD_MISO 19
#define SD_MOSI 23

#define LED_PIN 27
#define LED_COUNT 1

// ---------------- OBJECTS ----------------

HX711 scale1;
HX711 scale2;

OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature ds18b20(&oneWire);

Adafruit_SHT4x sht4;
RTC_DS3231 rtc;

Adafruit_NeoPixel led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

const char* CACHE_FILE = "/cache.ndjson";

unsigned long lastSend = 0;

// ---------------- LED STATUS ----------------

void setLed(uint8_t r, uint8_t g, uint8_t b) {
  led.setPixelColor(0, led.Color(r, g, b));
  led.show();
}

// ---------------- TIME ----------------

String timestampNow() {
  DateTime now = rtc.now();

  char buf[25];
  snprintf(
    buf,
    sizeof(buf),
    "%04d-%02d-%02dT%02d:%02d:%02dZ",
    now.year(),
    now.month(),
    now.day(),
    now.hour(),
    now.minute(),
    now.second()
  );

  return String(buf);
}

// ---------------- SD CACHE ----------------

bool appendCacheLine(const String& line) {
  File file = SD.open(CACHE_FILE, FILE_APPEND);
  if (!file) return false;

  file.println(line);
  file.close();
  return true;
}

void clearCache() {
  SD.remove(CACHE_FILE);
}

// ---------------- MEASUREMENT ----------------

String createMeasurementJson() {
  setLed(0, 0, 80); // blue = measuring

  ds18b20.requestTemperatures();

  float hiveTemp1 = ds18b20.getTempCByIndex(0);
  float hiveTemp2 = ds18b20.getTempCByIndex(1);

  sensors_event_t humidity, temp;
  sht4.getEvent(&humidity, &temp);

  float weight1 = scale1.get_units(10);
  float weight2 = scale2.get_units(10);

  StaticJsonDocument<512> doc;

  doc["device_id"] = DEVICE_ID;
  doc["timestamp"] = timestampNow();

  doc["scale_1_weight_kg"] = weight1;
  doc["scale_2_weight_kg"] = weight2;

  doc["hive_1_temp_c"] = hiveTemp1;
  doc["hive_2_temp_c"] = hiveTemp2;

  doc["ambient_temp_c"] = temp.temperature;
  doc["ambient_humidity_percent"] = humidity.relative_humidity;

  String output;
  serializeJson(doc, output);
  return output;
}

// ---------------- WIFI ----------------

bool connectWifi(unsigned long timeoutMs = 15000) {
  if (WiFi.status() == WL_CONNECTED) return true;

  setLed(80, 40, 0); // orange = connecting WiFi

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
  }

  return WiFi.status() == WL_CONNECTED;
}

// ---------------- HTTP UPLOAD ----------------

bool uploadJson(const String& json) {
  if (!connectWifi()) return false;

  setLed(80, 80, 0); // yellow = uploading

  HTTPClient http;
  http.begin(API_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", API_KEY);

  int code = http.POST(json);
  String response = http.getString();
  http.end();

  return code >= 200 && code < 300;
}

bool uploadCachedLines() {
  if (!SD.exists(CACHE_FILE)) return true;
  if (!connectWifi()) return false;

  File file = SD.open(CACHE_FILE, FILE_READ);
  if (!file) return false;

  bool allOk = true;

  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) continue;

    if (!uploadJson(line)) {
      allOk = false;
      break;
    }

    delay(200);
  }

  file.close();

  if (allOk) {
    clearCache();
  }

  return allOk;
}

// ---------------- SETUP ----------------

void setup() {
  Serial.begin(115200);

  led.begin();
  setLed(40, 40, 40); // white = booting

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!rtc.begin()) {
    setLed(80, 0, 0);
    Serial.println("RTC not found");
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power. Set time from compile time.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  if (!sht4.begin()) {
    setLed(80, 0, 0);
    Serial.println("SHT41 not found");
  }

  sht4.setPrecision(SHT4X_HIGH_PRECISION);
  sht4.setHeater(SHT4X_NO_HEATER);

  ds18b20.begin();

  scale1.begin(HX1_DOUT, HX1_SCK);
  scale2.begin(HX2_DOUT, HX2_SCK);

  scale1.set_scale(SCALE1_CALIBRATION);
  scale2.set_scale(SCALE2_CALIBRATION);

  scale1.tare();
  scale2.tare();

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS)) {
    setLed(80, 0, 0);
    Serial.println("SD card failed");
  }

  connectWifi(10000);

  setLed(0, 80, 0); // green = ready
}

// ---------------- LOOP ----------------

void loop() {
  if (millis() - lastSend >= SEND_INTERVAL_MS || lastSend == 0) {
    lastSend = millis();

    String json = createMeasurementJson();

    Serial.println(json);

    bool cached = appendCacheLine(json);

    if (!cached) {
      Serial.println("Could not write to SD cache");
      setLed(80, 0, 80); // purple = SD error
    }

    bool uploadedCache = uploadCachedLines();

    if (uploadedCache) {
      setLed(0, 80, 0); // green = success
    } else {
      setLed(80, 0, 0); // red = upload failed, cached
    }
  }

  delay(1000);
}