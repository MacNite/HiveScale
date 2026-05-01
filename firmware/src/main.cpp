#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
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

#include "secrets.h"

static const char* FIRMWARE_VERSION = "0.2.0";

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

long scale1Offset = 0;
long scale2Offset = 0;
float scale1Factor = -7050.0f;
float scale2Factor = -7050.0f;

String apiUrl(const String& path) {
  String base = API_BASE_URL;
  if (base.endsWith("/")) base.remove(base.length() - 1);
  return base + path;
}

void loadConfigFromPrefs() {
  prefs.begin("hivescale", false);
  sendIntervalMs = prefs.getUInt("interval", 600) * 1000UL;
  scale1Offset = prefs.getLong("s1_offset", 0);
  scale2Offset = prefs.getLong("s2_offset", 0);
  scale1Factor = prefs.getFloat("s1_factor", -7050.0f);
  scale2Factor = prefs.getFloat("s2_factor", -7050.0f);
  prefs.end();
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

String timestampNow() {
  if (!rtcOk) return String("1970-01-01T00:00:00Z");
  DateTime now = rtc.now();
  char buf[25];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
           now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
  return String(buf);
}

bool connectWifi(unsigned long timeoutMs = 20000) {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool httpGetJson(const String& url, JsonDocument& doc) {
  if (!connectWifi()) return false;
  WiFiClientSecure client;
  client.setInsecure(); // Replace with a CA certificate for production.
  HTTPClient http;
  if (!http.begin(client, url)) return false;
  http.addHeader("X-API-Key", API_KEY);
  int code = http.GET();
  if (code < 200 || code >= 300) {
    http.end();
    return false;
  }
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  return !err;
}

bool httpPostJson(const String& url, const String& json, String* response = nullptr) {
  if (!connectWifi()) return false;
  WiFiClientSecure client;
  client.setInsecure(); // Replace with a CA certificate for production.
  HTTPClient http;
  if (!http.begin(client, url)) return false;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", API_KEY);
  int code = http.POST(json);
  if (response) *response = http.getString();
  http.end();
  return code >= 200 && code < 300;
}

bool appendCacheLine(const String& line) {
  if (!sdOk) return false;
  File file = SD.open(CACHE_FILE, FILE_APPEND);
  if (!file) return false;
  file.println(line);
  file.close();
  return true;
}

long readAverageRaw(HX711& scale, int samples = 15) {
  if (!scale.wait_ready_timeout(2000)) return 0;
  return scale.read_average(samples);
}

float weightFromRaw(long raw, long offset, float factor) {
  if (factor == 0.0f) return NAN;
  return ((float)(raw - offset)) / factor;
}

String createMeasurementJson() {
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
    }
  }

  long raw1 = readAverageRaw(scale1);
  long raw2 = readAverageRaw(scale2);
  float weight1 = weightFromRaw(raw1, scale1Offset, scale1Factor);
  float weight2 = weightFromRaw(raw2, scale2Offset, scale2Factor);

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
  doc["scale_1_raw"] = raw1;
  doc["scale_2_raw"] = raw2;

  String output;
  serializeJson(doc, output);
  return output;
}

bool uploadLine(const String& line) {
  return httpPostJson(apiUrl("/api/v1/measurements"), line);
}

bool uploadCachedLines() {
  if (!sdOk || !SD.exists(CACHE_FILE)) return true;
  File in = SD.open(CACHE_FILE, FILE_READ);
  if (!in) return false;
  SD.remove(TEMP_FILE);
  File out = SD.open(TEMP_FILE, FILE_WRITE);
  if (!out) {
    in.close();
    return false;
  }

  bool allOk = true;
  while (in.available()) {
    String line = in.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    if (allOk && uploadLine(line)) {
      delay(100);
    } else {
      allOk = false;
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
  return allOk;
}

void fetchRemoteConfig() {
  JsonDocument doc;
  if (!httpGetJson(apiUrl(String("/api/v1/devices/") + DEVICE_ID + "/config"), doc)) return;
  sendIntervalMs = (unsigned long)(doc["send_interval_seconds"] | 600) * 1000UL;
  scale1Offset = doc["scale1_offset"] | scale1Offset;
  scale1Factor = doc["scale1_factor"] | scale1Factor;
  scale2Offset = doc["scale2_offset"] | scale2Offset;
  scale2Factor = doc["scale2_factor"] | scale2Factor;
  saveScaleConfig();
}

void reportCommandResult(long id, bool success, const String& message, JsonDocument* resultDoc = nullptr) {
  JsonDocument doc;
  doc["success"] = success;
  doc["message"] = message;
  if (resultDoc) doc["result"] = (*resultDoc).as<JsonVariant>();
  String body;
  serializeJson(doc, body);
  httpPostJson(apiUrl(String("/api/v1/devices/") + DEVICE_ID + "/commands/" + String(id) + "/result"), body);
}

void executeCommand(JsonDocument& cmd) {
  if (!(cmd["command"] | false)) return;
  long id = cmd["id"] | 0;
  String type = cmd["command_type"] | "";
  JsonObject payload = cmd["payload"].as<JsonObject>();
  JsonDocument result;

  if (type == "tare_scale_1") {
    scale1Offset = readAverageRaw(scale1, 25);
    saveScaleConfig();
    result["scale1_offset"] = scale1Offset;
    reportCommandResult(id, true, "scale 1 tared", &result);
  } else if (type == "tare_scale_2") {
    scale2Offset = readAverageRaw(scale2, 25);
    saveScaleConfig();
    result["scale2_offset"] = scale2Offset;
    reportCommandResult(id, true, "scale 2 tared", &result);
  } else if (type == "calibrate_scale_1") {
    float known = payload["known_weight_kg"] | 0.0f;
    if (known <= 0) { reportCommandResult(id, false, "known_weight_kg missing or invalid"); return; }
    long raw = readAverageRaw(scale1, 25);
    scale1Factor = ((float)(raw - scale1Offset)) / known;
    saveScaleConfig();
    result["scale1_factor"] = scale1Factor;
    result["raw"] = raw;
    reportCommandResult(id, true, "scale 1 calibrated", &result);
  } else if (type == "calibrate_scale_2") {
    float known = payload["known_weight_kg"] | 0.0f;
    if (known <= 0) { reportCommandResult(id, false, "known_weight_kg missing or invalid"); return; }
    long raw = readAverageRaw(scale2, 25);
    scale2Factor = ((float)(raw - scale2Offset)) / known;
    saveScaleConfig();
    result["scale2_factor"] = scale2Factor;
    result["raw"] = raw;
    reportCommandResult(id, true, "scale 2 calibrated", &result);
  } else if (type == "reboot") {
    reportCommandResult(id, true, "rebooting");
    delay(500);
    ESP.restart();
  } else {
    reportCommandResult(id, false, "unknown command");
  }
}

void pollCommand() {
  JsonDocument doc;
  if (!httpGetJson(apiUrl(String("/api/v1/devices/") + DEVICE_ID + "/commands/next"), doc)) return;
  executeCommand(doc);
}

void checkFirmwareUpdate() {
  JsonDocument doc;
  String url = apiUrl(String("/api/v1/devices/") + DEVICE_ID + "/firmware?version=" + FIRMWARE_VERSION);
  if (!httpGetJson(url, doc)) return;
  if (!(doc["update"] | false)) return;
  String firmwareUrl = doc["url"] | "";
  if (firmwareUrl.length() == 0) return;

  WiFiClientSecure client;
  client.setInsecure(); // Replace with a CA certificate for production.
  t_httpUpdate_return ret = httpUpdate.update(client, firmwareUrl);
  if (ret == HTTP_UPDATE_OK) {
    ESP.restart();
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  loadConfigFromPrefs();

  Wire.begin(I2C_SDA, I2C_SCL);
  rtcOk = rtc.begin();
  if (rtcOk && rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  shtOk = sht4.begin();
  if (shtOk) {
    sht4.setPrecision(SHT4X_HIGH_PRECISION);
    sht4.setHeater(SHT4X_NO_HEATER);
  }

  ds18b20.begin();
  scale1.begin(HX1_DOUT, HX1_SCK);
  scale2.begin(HX2_DOUT, HX2_SCK);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  sdOk = SD.begin(SD_CS);

  connectWifi(15000);
  fetchRemoteConfig();
}

void loop() {
  if (lastCycleMs == 0 || millis() - lastCycleMs >= sendIntervalMs) {
    lastCycleMs = millis();

    String json = createMeasurementJson();
    appendCacheLine(json);

    if (connectWifi(15000)) {
      uploadCachedLines();
      fetchRemoteConfig();
      pollCommand();
      checkFirmwareUpdate();
    }
  }
  delay(1000);
}