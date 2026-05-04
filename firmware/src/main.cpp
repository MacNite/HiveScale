#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <HX711.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_SHT4x.h>
#include <RTClib.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <sys/time.h>

#include "secrets.h"

static const char* FIRMWARE_VERSION = "0.3.2-debug";

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

static const char* CACHE_FILE = "/cache.ndjson";
static const char* TEMP_FILE = "/cache.tmp";

HX711 scale1;
HX711 scale2;
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature ds18b20(&oneWire);
Adafruit_SHT4x sht4;
RTC_DS3231 rtc;
Preferences prefs;

bool sdOk = false;
bool shtOk = false;
bool rtcOk = false;

unsigned long lastCycleMs = 0;
unsigned long sendIntervalMs = 10UL * 60UL * 1000UL;

String timeSource = "unknown";

long scale1Offset = 0;
long scale2Offset = 0;
float scale1Factor = -7050.0f;
float scale2Factor = -7050.0f;

String apiUrl(const String& path) {
  String base = API_BASE_URL;
  if (base.endsWith("/")) base.remove(base.length() - 1);
  return base + path;
}

void debugLine() {
  Serial.println("----------------------------------------");
}

void loadConfigFromPrefs() {
  prefs.begin("hivescale", false);
  sendIntervalMs = prefs.getUInt("interval", 600) * 1000UL;
  scale1Offset = prefs.getLong("s1_offset", 0);
  scale2Offset = prefs.getLong("s2_offset", 0);
  scale1Factor = prefs.getFloat("s1_factor", -7050.0f);
  scale2Factor = prefs.getFloat("s2_factor", -7050.0f);
  prefs.end();

  Serial.println("[PREFS] Loaded config");
  Serial.printf("[PREFS] interval ms: %lu\n", sendIntervalMs);
  Serial.printf("[PREFS] scale1 offset: %ld factor: %.6f\n", scale1Offset, scale1Factor);
  Serial.printf("[PREFS] scale2 offset: %ld factor: %.6f\n", scale2Offset, scale2Factor);
}

void saveScaleConfig() {
  prefs.begin("hivescale", false);
  prefs.putUInt("interval", sendIntervalMs / 1000UL);
  prefs.putLong("s1_offset", scale1Offset);
  prefs.putLong("s2_offset", scale2Offset);
  prefs.putFloat("s1_factor", scale1Factor);
  prefs.putFloat("s2_factor", scale2Factor);
  prefs.end();
}

bool connectWifi(unsigned long timeoutMs = 20000) {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.printf("[WIFI] Connecting to SSID: %s\n", WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    Serial.print(".");
    delay(500);
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WIFI] Connected");
    Serial.print("[WIFI] IP: ");
    Serial.println(WiFi.localIP());
    Serial.printf("[WIFI] RSSI: %d dBm\n", WiFi.RSSI());
    return true;
  }

  Serial.printf("[WIFI] FAILED. Status: %d\n", WiFi.status());
  return false;
}

String timestampNow() {
  if (rtcOk) {
    DateTime now = rtc.now();

    if (now.year() >= 2024 && now.year() <= 2099) {
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
  }

  struct tm tmNow;
  if (getLocalTime(&tmNow, 100)) {
    char buf[25];
    snprintf(
      buf,
      sizeof(buf),
      "%04d-%02d-%02dT%02d:%02d:%02dZ",
      tmNow.tm_year + 1900,
      tmNow.tm_mon + 1,
      tmNow.tm_mday,
      tmNow.tm_hour,
      tmNow.tm_min,
      tmNow.tm_sec
    );
    return String(buf);
  }

  return String("1970-01-01T00:00:00Z");
}

void syncTime() {
  if (!connectWifi()) {
    Serial.println("[TIME] Cannot sync time: WiFi unavailable");
    return;
  }

  Serial.println("[TIME] Syncing with NTP...");
  configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");

  struct tm tmNow;
  for (int i = 0; i < 20; i++) {
    if (getLocalTime(&tmNow, 500)) {
      time_t nowUnix = mktime(&tmNow);

      if (nowUnix > 1700000000) {
        Serial.println("[TIME] NTP sync OK");
        timeSource = "ntp";

        if (rtcOk) {
          struct tm* utc = gmtime(&nowUnix);
          rtc.adjust(DateTime(
            utc->tm_year + 1900,
            utc->tm_mon + 1,
            utc->tm_mday,
            utc->tm_hour,
            utc->tm_min,
            utc->tm_sec
          ));
          Serial.println("[TIME] RTC updated from NTP");
        }

        Serial.print("[TIME] Current timestamp: ");
        Serial.println(timestampNow());
        return;
      }
    }

    delay(500);
  }

  Serial.println("[TIME] NTP sync FAILED");

  if (rtcOk) {
    DateTime now = rtc.now();
    if (now.year() >= 2024 && now.year() <= 2099) {
      timeSource = "rtc";
      Serial.println("[TIME] Using RTC");
      return;
    }
  }

  timeSource = "invalid";
}

bool httpGetJson(const String& url, JsonDocument& doc) {
  if (!connectWifi()) return false;

  Serial.println("[HTTP GET]");
  Serial.println(url);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  if (!http.begin(client, url)) {
    Serial.println("[HTTP GET] http.begin failed");
    return false;
  }

  http.addHeader("X-API-Key", API_KEY);

  int code = http.GET();
  String body = http.getString();

  Serial.printf("[HTTP GET] Status: %d\n", code);
  Serial.print("[HTTP GET] Body: ");
  Serial.println(body);

  http.end();

  if (code < 200 || code >= 300) {
    return false;
  }

  DeserializationError err = deserializeJson(doc, body);

  if (err) {
    Serial.print("[HTTP GET] JSON parse error: ");
    Serial.println(err.c_str());
    return false;
  }

  return true;
}

bool httpPostJson(const String& url, const String& json, String* response = nullptr) {
  if (!connectWifi()) {
    Serial.println("[HTTP POST] No WiFi");
    return false;
  }

  Serial.println("[HTTP POST]");
  Serial.print("[HTTP POST] URL: ");
  Serial.println(url);
  Serial.print("[HTTP POST] Payload: ");
  Serial.println(json);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  if (!http.begin(client, url)) {
    Serial.println("[HTTP POST] http.begin failed");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", API_KEY);

  int code = http.POST((uint8_t*)json.c_str(), json.length());
  String body = http.getString();

  Serial.printf("[HTTP POST] Status: %d\n", code);
  Serial.print("[HTTP POST] Response: ");
  Serial.println(body);

  if (response) {
    *response = body;
  }

  http.end();

  if (code >= 200 && code < 300) {
    Serial.println("[HTTP POST] SUCCESS");
    return true;
  }

  Serial.println("[HTTP POST] FAILED");
  return false;
}

bool appendCacheLine(const String& line) {
  if (!sdOk) {
    Serial.println("[CACHE] SD unavailable, cannot cache");
    return false;
  }

  File file = SD.open(CACHE_FILE, FILE_APPEND);

  if (!file) {
    Serial.println("[CACHE] Failed to open cache file");
    return false;
  }

  file.println(line);
  file.close();

  Serial.println("[CACHE] Appended line to cache");
  return true;
}

long readAverageRaw(HX711& scale, int samples = 15) {
  if (!scale.wait_ready_timeout(2000)) {
    Serial.println("[HX711] Not ready");
    return 0;
  }

  return scale.read_average(samples);
}

float weightFromRaw(long raw, long offset, float factor) {
  if (factor == 0.0f) return NAN;
  return ((float)(raw - offset)) / factor;
}

String createMeasurementJson() {
  Serial.println("[MEASURE] Reading sensors...");

  ds18b20.requestTemperatures();

  float hiveTemp1 = ds18b20.getTempCByIndex(0);
  float hiveTemp2 = ds18b20.getTempCByIndex(1);

  float ambientTemp = NAN;
  float ambientHumidity = NAN;

  if (shtOk) {
    sensors_event_t humidity, temp;

    if (sht4.getEvent(&humidity, &temp)) {
      ambientTemp = temp.temperature;
      ambientHumidity = humidity.relative_humidity;
    } else {
      Serial.println("[SHT4x] Read failed");
    }
  }

  long raw1 = readAverageRaw(scale1);
  long raw2 = readAverageRaw(scale2);

  float weight1 = weightFromRaw(raw1, scale1Offset, scale1Factor);
  float weight2 = weightFromRaw(raw2, scale2Offset, scale2Factor);

  Serial.printf("[MEASURE] raw1=%ld weight1=%.3f kg\n", raw1, weight1);
  Serial.printf("[MEASURE] raw2=%ld weight2=%.3f kg\n", raw2, weight2);
  Serial.printf("[MEASURE] hiveTemp1=%.2f hiveTemp2=%.2f\n", hiveTemp1, hiveTemp2);
  Serial.printf("[MEASURE] ambientTemp=%.2f humidity=%.2f\n", ambientTemp, ambientHumidity);

  JsonDocument doc;

  doc["device_id"] = DEVICE_ID;
  doc["timestamp"] = timestampNow();

  doc["scale_1_weight_kg"] = weight1;
  doc["scale_2_weight_kg"] = weight2;

  doc["hive_1_temp_c"] = hiveTemp1;
  doc["hive_2_temp_c"] = hiveTemp2;

  doc["ambient_temp_c"] = ambientTemp;
  doc["ambient_humidity_percent"] = ambientHumidity;

  doc["rssi_dbm"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["time_source"] = timeSource;

  doc["scale_1_raw"] = raw1;
  doc["scale_2_raw"] = raw2;

  String output;
  serializeJson(doc, output);

  Serial.print("[MEASURE] JSON: ");
  Serial.println(output);

  return output;
}

bool uploadLine(const String& line) {
  String response;
  bool ok = httpPostJson(apiUrl("/api/v1/measurements"), line, &response);

  if (!ok) {
    Serial.println("[UPLOAD] Upload failed");
  } else {
    Serial.println("[UPLOAD] Upload accepted by server");
  }

  return ok;
}

bool uploadCachedLines() {
  if (!sdOk) {
    Serial.println("[CACHE] No SD card, skipping cached upload");
    return true;
  }

  if (!SD.exists(CACHE_FILE)) {
    Serial.println("[CACHE] No cache file");
    return true;
  }

  File in = SD.open(CACHE_FILE, FILE_READ);

  if (!in) {
    Serial.println("[CACHE] Failed to open cache file for read");
    return false;
  }

  SD.remove(TEMP_FILE);

  File out = SD.open(TEMP_FILE, FILE_WRITE);

  if (!out) {
    Serial.println("[CACHE] Failed to open temp cache file");
    in.close();
    return false;
  }

  bool allOk = true;
  int total = 0;
  int uploaded = 0;
  int kept = 0;

  while (in.available()) {
    String line = in.readStringUntil('\n');
    line.trim();

    if (line.length() == 0) continue;

    total++;

    Serial.printf("[CACHE] Uploading cached line %d\n", total);

    if (allOk && uploadLine(line)) {
      uploaded++;
      delay(100);
    } else {
      allOk = false;
      kept++;
      out.println(line);
    }
  }

  in.close();
  out.close();

  SD.remove(CACHE_FILE);

  if (allOk) {
    SD.remove(TEMP_FILE);
  } else {
    SD.rename(TEMP_FILE, CACHE_FILE);
  }

  Serial.printf("[CACHE] Total=%d Uploaded=%d Kept=%d\n", total, uploaded, kept);

  return allOk;
}

void fetchRemoteConfig() {
  JsonDocument doc;
  String url = apiUrl(String("/api/v1/devices/") + DEVICE_ID + "/config");

  Serial.println("[CONFIG] Fetching remote config");

  if (!httpGetJson(url, doc)) {
    Serial.println("[CONFIG] Failed to fetch config");
    return;
  }

  sendIntervalMs = (unsigned long)(doc["send_interval_seconds"] | 600) * 1000UL;
  scale1Offset = doc["scale1_offset"] | scale1Offset;
  scale1Factor = doc["scale1_factor"] | scale1Factor;
  scale2Offset = doc["scale2_offset"] | scale2Offset;
  scale2Factor = doc["scale2_factor"] | scale2Factor;

  saveScaleConfig();

  Serial.println("[CONFIG] Remote config applied");
}

void runUploadCycle() {
  debugLine();
  Serial.println("[CYCLE] Starting measurement/upload cycle");

  String json = createMeasurementJson();

  if (sdOk) {
    appendCacheLine(json);
    uploadCachedLines();
  } else {
    Serial.println("[CYCLE] No SD card, trying direct upload only");
    uploadLine(json);
  }

  fetchRemoteConfig();

  Serial.println("[CYCLE] Done");
  debugLine();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  debugLine();
  Serial.println("Hive Scale ESP32 DEBUG firmware");
  Serial.printf("Firmware version: %s\n", FIRMWARE_VERSION);
  debugLine();

  loadConfigFromPrefs();

  Wire.begin(I2C_SDA, I2C_SCL);
  Serial.println("[I2C] Started");

  rtcOk = rtc.begin();
  Serial.printf("[RTC] %s\n", rtcOk ? "OK" : "MISSING");

  if (rtcOk && rtc.lostPower()) {
    Serial.println("[RTC] Lost power");
  }

  shtOk = sht4.begin();
  Serial.printf("[SHT4x] %s\n", shtOk ? "OK" : "MISSING");

  if (shtOk) {
    sht4.setPrecision(SHT4X_HIGH_PRECISION);
    sht4.setHeater(SHT4X_NO_HEATER);
  }

  ds18b20.begin();
  Serial.printf("[DS18B20] Device count: %d\n", ds18b20.getDeviceCount());

  scale1.begin(HX1_DOUT, HX1_SCK);
  scale2.begin(HX2_DOUT, HX2_SCK);
  Serial.println("[HX711] Initialized");

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  sdOk = SD.begin(SD_CS);
  Serial.printf("[SD] %s\n", sdOk ? "OK" : "MISSING");

  connectWifi(20000);
  syncTime();

  Serial.println("[SETUP] Running first upload cycle now");
  runUploadCycle();

  lastCycleMs = millis();
}

void loop() {
  if (millis() - lastCycleMs >= sendIntervalMs) {
    lastCycleMs = millis();
    runUploadCycle();
  }

  delay(1000);
}