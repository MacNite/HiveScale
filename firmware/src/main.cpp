#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Update.h>
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

#ifndef CLAIM_CODE
#define CLAIM_CODE ""
#endif

#ifndef CLAIM_CODE_REVISION
#define CLAIM_CODE_REVISION 1
#endif

#ifndef FORCE_RESEED
#define FORCE_RESEED false
#endif

static const char* FIRMWARE_VERSION = "0.4.2-ota-test";

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

// External button. Wire button between this pin and GND. Uses INPUT_PULLUP.
// Short press: start WiFi provisioning AP.
// Long press: reset Preferences and reboot.
#define SETUP_BUTTON_PIN 27
static const unsigned long BUTTON_DEBOUNCE_MS = 50;
static const unsigned long BUTTON_LONG_PRESS_MS = 5000;

static const int MAX_WIFI_NETWORKS = 3;
static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
static const unsigned long PROVISIONING_TIMEOUT_MS = 10UL * 60UL * 1000UL;
static const unsigned long OTA_CHECK_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
static const unsigned long COMMAND_CHECK_INTERVAL_MS = 5UL * 60UL * 1000UL;

static const char* CACHE_FILE = "/cache.ndjson";
static const char* TEMP_FILE = "/cache.tmp";

HX711 scale1;
HX711 scale2;
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature ds18b20(&oneWire);
Adafruit_SHT4x sht4;
RTC_DS3231 rtc;
Preferences prefs;
WebServer setupServer(80);

bool sdOk = false;
bool shtOk = false;
bool rtcOk = false;
bool provisioningActive = false;

unsigned long lastCycleMs = 0;
unsigned long lastOtaCheckMs = 0;
unsigned long lastCommandCheckMs = 0;
unsigned long provisioningStartedMs = 0;
unsigned long sendIntervalMs = 10UL * 60UL * 1000UL;

String timeSource = "unknown";
String apiBaseUrl;
String apiKey;
String deviceId;
String claimCode;
String activeWifiSsid;

long scale1Offset = 0;
long scale2Offset = 0;
float scale1Factor = -7050.0f;
float scale2Factor = -7050.0f;

bool buttonWasDown = false;
unsigned long buttonDownMs = 0;
bool longPressHandled = false;

void debugLine() {
  Serial.println("----------------------------------------");
}

String trimTrailingSlash(String value) {
  value.trim();
  while (value.endsWith("/")) value.remove(value.length() - 1);
  return value;
}

String apiUrl(const String& path) {
  String base = trimTrailingSlash(apiBaseUrl);
  return base + path;
}

bool isBlank(const String& s) {
  return s.length() == 0;
}

String prefString(const char* key, const char* fallback = "") {
  prefs.begin("hivescale", true);
  String value = prefs.getString(key, fallback);
  prefs.end();
  return value;
}

void putPrefString(const char* key, const String& value) {
  prefs.begin("hivescale", false);
  prefs.putString(key, value);
  prefs.end();
}

String wifiSsidKey(int index) { return String("wifi") + index + "_ssid"; }
String wifiPassKey(int index) { return String("wifi") + index + "_pass"; }

void seedPrefsFromSecretsIfNeeded() {
  prefs.begin("hivescale", false);
  bool seeded = prefs.getBool("seeded", false);

  if (!seeded) {
    Serial.println("[PREFS] First boot seed from secrets.h");

    prefs.putString("api_base", API_BASE_URL);
    prefs.putString("api_key", API_KEY);
    prefs.putString("device_id", DEVICE_ID);

    #ifdef CLAIM_CODE
      prefs.putString("claim_code", CLAIM_CODE);
    #endif

    #ifdef CLAIM_CODE_REVISION
      prefs.putUInt("claim_rev", CLAIM_CODE_REVISION);
    #else
      prefs.putUInt("claim_rev", 1);
    #endif

    int wifiCount = 0;

    // --- NEW: support WIFI1 / WIFI2 / WIFI3 ---
    #ifdef WIFI1_SSID
      prefs.putString("wifi0_ssid", WIFI1_SSID);
      prefs.putString("wifi0_pass", WIFI1_PASS);
      wifiCount++;
    #endif

    #ifdef WIFI2_SSID
      prefs.putString("wifi1_ssid", WIFI2_SSID);
      prefs.putString("wifi1_pass", WIFI2_PASS);
      wifiCount++;
    #endif

    #ifdef WIFI3_SSID
      prefs.putString("wifi2_ssid", WIFI3_SSID);
      prefs.putString("wifi2_pass", WIFI3_PASS);
      wifiCount++;
    #endif

    // --- fallback: old single-WiFi format ---
    #if defined(WIFI_SSID) && defined(WIFI_PASS)
      if (wifiCount == 0 && String(WIFI_SSID).length() > 0) {
        prefs.putString("wifi0_ssid", WIFI_SSID);
        prefs.putString("wifi0_pass", WIFI_PASS);
        wifiCount = 1;
      }
    #endif

    prefs.putUInt("wifi_count", wifiCount);
    prefs.putBool("seeded", true);
    prefs.putBool("provisioned", true);
  }

  #ifdef CLAIM_CODE
    uint32_t storedClaimRevision = prefs.getUInt("claim_rev", 0);
    #ifdef CLAIM_CODE_REVISION
      uint32_t firmwareClaimRevision = CLAIM_CODE_REVISION;
    #else
      uint32_t firmwareClaimRevision = 1;
    #endif

    if (FORCE_RESEED || storedClaimRevision < firmwareClaimRevision) {
      Serial.println("[PREFS] Updating claim code from secrets.h revision");
      prefs.putString("claim_code", CLAIM_CODE);
      prefs.putUInt("claim_rev", firmwareClaimRevision);
    }
  #endif

  prefs.end();
}

void loadConfigFromPrefs() {
  prefs.begin("hivescale", false);

  apiBaseUrl = prefs.getString("api_base", API_BASE_URL);
  apiKey = prefs.getString("api_key", API_KEY);
  deviceId = prefs.getString("device_id", DEVICE_ID);
  claimCode = prefs.getString("claim_code", CLAIM_CODE);

  sendIntervalMs = prefs.getUInt("interval", 600) * 1000UL;
  scale1Offset = prefs.getLong("s1_offset", 0);
  scale2Offset = prefs.getLong("s2_offset", 0);
  scale1Factor = prefs.getFloat("s1_factor", -7050.0f);
  scale2Factor = prefs.getFloat("s2_factor", -7050.0f);

  prefs.end();

  Serial.println("[PREFS] Loaded config");
  Serial.printf("[PREFS] device_id: %s\n", deviceId.c_str());
  Serial.printf("[PREFS] claim_code present: %s\n", claimCode.length() > 0 ? "yes" : "no");
  Serial.printf("[PREFS] api_base: %s\n", apiBaseUrl.c_str());
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

int getWifiCount() {
  prefs.begin("hivescale", true);
  int count = prefs.getUInt("wifi_count", 0);
  prefs.end();
  if (count < 0) count = 0;
  if (count > MAX_WIFI_NETWORKS) count = MAX_WIFI_NETWORKS;
  return count;
}

bool saveWifiNetwork(int index, const String& ssid, const String& pass) {
  if (index < 0 || index >= MAX_WIFI_NETWORKS || ssid.length() == 0) return false;

  prefs.begin("hivescale", false);
  prefs.putString(wifiSsidKey(index).c_str(), ssid);
  prefs.putString(wifiPassKey(index).c_str(), pass);

  int count = prefs.getUInt("wifi_count", 0);
  if (index + 1 > count) prefs.putUInt("wifi_count", index + 1);
  prefs.putBool("provisioned", true);
  prefs.end();
  return true;
}

void clearWifiCredentials() {
  prefs.begin("hivescale", false);
  for (int i = 0; i < MAX_WIFI_NETWORKS; i++) {
    prefs.remove(wifiSsidKey(i).c_str());
    prefs.remove(wifiPassKey(i).c_str());
  }
  prefs.putUInt("wifi_count", 0);
  prefs.end();
  activeWifiSsid = "";
}

void factoryResetPreferences() {
  Serial.println("[PREFS] Factory reset requested");
  prefs.begin("hivescale", false);
  prefs.clear();
  prefs.end();
  delay(300);
  ESP.restart();
}

bool connectWifi(unsigned long timeoutMs = WIFI_CONNECT_TIMEOUT_MS) {
  if (WiFi.status() == WL_CONNECTED) return true;

  int count = getWifiCount();
  if (count <= 0) {
    Serial.println("[WIFI] No saved WiFi credentials");
    return false;
  }

  WiFi.mode(WIFI_STA);

  for (int i = 0; i < count; i++) {
    prefs.begin("hivescale", true);
    String ssid = prefs.getString(wifiSsidKey(i).c_str(), "");
    String pass = prefs.getString(wifiPassKey(i).c_str(), "");
    prefs.end();

    if (ssid.length() == 0) continue;

    Serial.printf("[WIFI] Trying saved network %d/%d: %s\n", i + 1, count, ssid.c_str());
    WiFi.disconnect(true, true);
    delay(200);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
      Serial.print(".");
      delay(500);
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      activeWifiSsid = ssid;
      Serial.println("[WIFI] Connected");
      Serial.print("[WIFI] IP: ");
      Serial.println(WiFi.localIP());
      Serial.printf("[WIFI] RSSI: %d dBm\n", WiFi.RSSI());
      return true;
    }

    Serial.printf("[WIFI] Failed network: %s status=%d\n", ssid.c_str(), WiFi.status());
  }

  Serial.println("[WIFI] All saved networks failed. Not starting AP automatically for power saving.");
  return false;
}

String htmlEscape(String s) {
  s.replace("&", "&amp;");
  s.replace("<", "&lt;");
  s.replace(">", "&gt;");
  s.replace("\"", "&quot;");
  return s;
}

void handleSetupRoot() {
  String html;
  html += "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>HiveScale Setup</title>";
  html += "<style>body{font-family:system-ui;margin:24px;max-width:760px}input{width:100%;padding:10px;margin:6px 0 14px}button{padding:12px 16px}fieldset{margin:16px 0;padding:16px}</style>";
  html += "</head><body><h1>HiveScale Setup</h1>";
  html += "<p>Firmware: " + String(FIRMWARE_VERSION) + "</p>";
  html += "<form method='POST' action='/save'>";
  html += "<fieldset><legend>Backend</legend>";
  html += "<label>Device ID</label><input name='device_id' value='" + htmlEscape(deviceId) + "'>";
  html += "<label>Claim code</label><input name='claim_code' value='" + htmlEscape(claimCode) + "'>";
  html += "<label>API base URL</label><input name='api_base' value='" + htmlEscape(apiBaseUrl) + "'>";
  html += "<label>API key</label><input name='api_key' value='" + htmlEscape(apiKey) + "'>";
  html += "</fieldset>";

  html += "<fieldset><legend>WiFi networks</legend>";
  for (int i = 0; i < MAX_WIFI_NETWORKS; i++) {
    prefs.begin("hivescale", true);
    String ssid = prefs.getString(wifiSsidKey(i).c_str(), "");
    prefs.end();
    html += "<h3>Network " + String(i + 1) + "</h3>";
    html += "<label>SSID</label><input name='ssid" + String(i) + "' value='" + htmlEscape(ssid) + "'>";
    html += "<label>Password</label><input type='password' name='pass" + String(i) + "' placeholder='Leave blank to keep current password'>";
  }
  html += "</fieldset>";
  html += "<button type='submit'>Save and reboot</button></form>";
  html += "<form method='POST' action='/reset' onsubmit='return confirm(\"Reset all Preferences?\")'><button type='submit'>Factory reset Preferences</button></form>";
  html += "</body></html>";
  setupServer.send(200, "text/html", html);
}

void handleSetupSave() {
  prefs.begin("hivescale", false);

  String newDeviceId = setupServer.arg("device_id");
  String newClaimCode = setupServer.arg("claim_code");
  String newApiBase = trimTrailingSlash(setupServer.arg("api_base"));
  String newApiKey = setupServer.arg("api_key");

  newClaimCode.trim();

  if (newDeviceId.length() > 0) prefs.putString("device_id", newDeviceId);
  prefs.putString("claim_code", newClaimCode);
  if (newApiBase.length() > 0) prefs.putString("api_base", newApiBase);
  if (newApiKey.length() > 0) prefs.putString("api_key", newApiKey);

  int savedCount = 0;
  for (int i = 0; i < MAX_WIFI_NETWORKS; i++) {
    String ssid = setupServer.arg("ssid" + String(i));
    String pass = setupServer.arg("pass" + String(i));
    ssid.trim();

    if (ssid.length() == 0) {
      prefs.remove(wifiSsidKey(i).c_str());
      prefs.remove(wifiPassKey(i).c_str());
      continue;
    }

    prefs.putString(wifiSsidKey(i).c_str(), ssid);
    if (pass.length() > 0) {
      prefs.putString(wifiPassKey(i).c_str(), pass);
    }
    savedCount = i + 1;
  }

  prefs.putUInt("wifi_count", savedCount);
  prefs.putBool("provisioned", true);
  prefs.putBool("seeded", true);
  prefs.end();

  setupServer.send(200, "text/html", "<html><body><h1>Saved</h1><p>Device will reboot now.</p></body></html>");
  delay(1000);
  ESP.restart();
}

void handleSetupReset() {
  setupServer.send(200, "text/html", "<html><body><h1>Resetting</h1></body></html>");
  delay(500);
  factoryResetPreferences();
}

void startProvisioningPortal() {
  if (provisioningActive) return;

  Serial.println("[SETUP] Starting provisioning AP");
  WiFi.disconnect(true, true);
  delay(200);
  WiFi.mode(WIFI_AP);

  String suffix = String((uint32_t)ESP.getEfuseMac(), HEX);
  suffix.toUpperCase();
  String apName = "HiveScale-Setup-" + suffix.substring(suffix.length() - 4);

  bool ok = WiFi.softAP(apName.c_str());
  if (!ok) {
    Serial.println("[SETUP] softAP failed");
    return;
  }

  setupServer.on("/", HTTP_GET, handleSetupRoot);
  setupServer.on("/save", HTTP_POST, handleSetupSave);
  setupServer.on("/reset", HTTP_POST, handleSetupReset);
  setupServer.onNotFound([]() {
    setupServer.sendHeader("Location", "/", true);
    setupServer.send(302, "text/plain", "");
  });
  setupServer.begin();

  provisioningActive = true;
  provisioningStartedMs = millis();

  Serial.printf("[SETUP] AP SSID: %s\n", apName.c_str());
  Serial.print("[SETUP] Open http://");
  Serial.println(WiFi.softAPIP());
}

void stopProvisioningPortal() {
  if (!provisioningActive) return;
  Serial.println("[SETUP] Stopping provisioning AP");
  setupServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  provisioningActive = false;
}

void handleButton() {
  bool down = digitalRead(SETUP_BUTTON_PIN) == LOW;
  unsigned long now = millis();

  if (down && !buttonWasDown) {
    buttonWasDown = true;
    buttonDownMs = now;
    longPressHandled = false;
  }

  if (down && buttonWasDown && !longPressHandled && now - buttonDownMs >= BUTTON_LONG_PRESS_MS) {
    longPressHandled = true;
    Serial.println("[BUTTON] Long press detected: factory reset Preferences");
    factoryResetPreferences();
  }

  if (!down && buttonWasDown) {
    unsigned long held = now - buttonDownMs;
    buttonWasDown = false;

    if (held > BUTTON_DEBOUNCE_MS && held < BUTTON_LONG_PRESS_MS && !longPressHandled) {
      Serial.println("[BUTTON] Short press detected: start provisioning AP");
      startProvisioningPortal();
    }
  }
}

String timestampNow() {
  if (rtcOk) {
    DateTime now = rtc.now();
    if (now.year() >= 2024 && now.year() <= 2099) {
      char buf[25];
      snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
      return String(buf);
    }
  }

  struct tm tmNow;
  if (getLocalTime(&tmNow, 100)) {
    char buf[25];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday, tmNow.tm_hour, tmNow.tm_min, tmNow.tm_sec);
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
          rtc.adjust(DateTime(utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday, utc->tm_hour, utc->tm_min, utc->tm_sec));
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

void addAuthHeader(HTTPClient& http) {
  if (apiKey.length() > 0) http.addHeader("X-API-Key", apiKey);
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

  addAuthHeader(http);

  int code = http.GET();
  String body = http.getString();

  Serial.printf("[HTTP GET] Status: %d\n", code);
  Serial.print("[HTTP GET] Body: ");
  Serial.println(body);

  http.end();

  if (code < 200 || code >= 300) return false;

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
  addAuthHeader(http);

  int code = http.POST((uint8_t*)json.c_str(), json.length());
  String body = http.getString();

  Serial.printf("[HTTP POST] Status: %d\n", code);
  Serial.print("[HTTP POST] Response: ");
  Serial.println(body);

  if (response) *response = body;

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
  doc["device_id"] = deviceId;
  if (claimCode.length() > 0) doc["claim_code"] = claimCode;
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
  doc["sd_ok"] = sdOk;
  doc["rtc_ok"] = rtcOk;
  doc["sht_ok"] = shtOk;

  String output;
  serializeJson(doc, output);

  Serial.print("[MEASURE] JSON: ");
  Serial.println(output);
  return output;
}

bool uploadLine(const String& line) {
  String response;
  bool ok = httpPostJson(apiUrl("/api/v1/measurements"), line, &response);

  if (!ok) Serial.println("[UPLOAD] Upload failed");
  else Serial.println("[UPLOAD] Upload accepted by server");

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
  if (allOk) SD.remove(TEMP_FILE);
  else SD.rename(TEMP_FILE, CACHE_FILE);

  Serial.printf("[CACHE] Total=%d Uploaded=%d Kept=%d\n", total, uploaded, kept);
  return allOk;
}

void fetchRemoteConfig() {
  JsonDocument doc;
  String url = apiUrl(String("/api/v1/devices/") + deviceId + "/config");

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

  if (doc["claim_code"].is<const char*>()) {
    String remoteClaimCode = doc["claim_code"].as<String>();
    remoteClaimCode.trim();
    if (remoteClaimCode.length() > 0 && remoteClaimCode != claimCode) {
      Serial.println("[CONFIG] Updating claim code from remote config");
      claimCode = remoteClaimCode;
      putPrefString("claim_code", claimCode);
    }
  }

  saveScaleConfig();
  Serial.println("[CONFIG] Remote config applied");
}

String absoluteUrl(String maybeRelativeUrl) {
  maybeRelativeUrl.trim();
  if (maybeRelativeUrl.startsWith("http://") || maybeRelativeUrl.startsWith("https://")) return maybeRelativeUrl;
  if (!maybeRelativeUrl.startsWith("/")) maybeRelativeUrl = "/" + maybeRelativeUrl;
  return trimTrailingSlash(apiBaseUrl) + maybeRelativeUrl;
}

bool performFirmwareUpdate(const String& firmwareUrl) {
  if (!connectWifi()) return false;

  String url = absoluteUrl(firmwareUrl);
  Serial.print("[OTA] Downloading firmware: ");
  Serial.println(url);

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!http.begin(client, url)) {
    Serial.println("[OTA] http.begin failed");
    return false;
  }

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[OTA] Download failed. HTTP %d\n", code);
    http.end();
    return false;
  }

  int contentLength = http.getSize();
  if (contentLength <= 0) {
    Serial.println("[OTA] Invalid content length");
    http.end();
    return false;
  }

  if (!Update.begin(contentLength)) {
    Serial.printf("[OTA] Update.begin failed. Error %d\n", Update.getError());
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  size_t written = Update.writeStream(*stream);

  if (written != (size_t)contentLength) {
    Serial.printf("[OTA] Written only %u/%d bytes\n", (unsigned)written, contentLength);
  }

  bool ok = Update.end();
  if (!ok) {
    Serial.printf("[OTA] Update.end failed. Error %d\n", Update.getError());
    http.end();
    return false;
  }

  if (!Update.isFinished()) {
    Serial.println("[OTA] Update not finished");
    http.end();
    return false;
  }

  http.end();
  Serial.println("[OTA] Update successful, rebooting");
  delay(1000);
  ESP.restart();
  return true;
}

void checkForOtaUpdate() {
  if (!connectWifi()) {
    Serial.println("[OTA] Skipping: WiFi unavailable");
    return;
  }

  JsonDocument doc;
  String url = apiUrl(String("/api/v1/devices/") + deviceId + "/firmware?version=" + FIRMWARE_VERSION);

  Serial.println("[OTA] Checking for update");
  if (!httpGetJson(url, doc)) {
    Serial.println("[OTA] Check failed");
    return;
  }

  bool updateAvailable = doc["update"] | false;
  if (!updateAvailable) {
    Serial.println("[OTA] No update available");
    return;
  }

  String version = doc["version"] | "unknown";
  String fwUrl = doc["url"] | "";

  if (fwUrl.length() == 0) {
    Serial.println("[OTA] Update response missing url");
    return;
  }

  Serial.printf("[OTA] Update available: %s\n", version.c_str());
  performFirmwareUpdate(fwUrl);
}

void postCommandResult(int commandId, bool success, const String& message) {
  JsonDocument result;
  result["success"] = success;
  result["message"] = message;

  String payload;
  serializeJson(result, payload);

  httpPostJson(apiUrl(String("/api/v1/devices/") + deviceId + "/commands/" + commandId + "/result"), payload);
}

void checkCommands() {
  if (!connectWifi()) return;

  JsonDocument doc;
  String url = apiUrl(String("/api/v1/devices/") + deviceId + "/commands/next");

  Serial.println("[CMD] Checking for command");
  if (!httpGetJson(url, doc)) {
    Serial.println("[CMD] Command check failed");
    return;
  }

  bool hasCommand = doc["command"] | false;
  if (!hasCommand) {
    Serial.println("[CMD] No pending command");
    return;
  }

  int commandId = doc["id"] | 0;
  String type = doc["command_type"] | "";
  Serial.printf("[CMD] Received command %d: %s\n", commandId, type.c_str());

  if (type == "reset_preferences" || type == "factory_reset") {
    postCommandResult(commandId, true, "Preferences reset; rebooting");
    delay(500);
    factoryResetPreferences();
  } else if (type == "reset_wifi") {
    clearWifiCredentials();
    postCommandResult(commandId, true, "WiFi credentials cleared");
    delay(500);
    ESP.restart();
  } else if (type == "check_ota" || type == "ota_update") {
    postCommandResult(commandId, true, "OTA check started");
    checkForOtaUpdate();
  } else if (type == "start_provisioning") {
    // This only makes sense while someone is physically near the device.
    postCommandResult(commandId, true, "Provisioning AP started");
    startProvisioningPortal();
  } else {
    postCommandResult(commandId, false, String("Unknown command: ") + type);
  }
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
  checkCommands();
  checkForOtaUpdate();

  Serial.println("[CYCLE] Done");
  debugLine();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(SETUP_BUTTON_PIN, INPUT_PULLUP);

  debugLine();
  Serial.println("Hive Scale ESP32 firmware with provisioning + OTA");
  Serial.printf("Firmware version: %s\n", FIRMWARE_VERSION);
  debugLine();

  seedPrefsFromSecretsIfNeeded();
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
  lastOtaCheckMs = millis();
  lastCommandCheckMs = millis();
}

void loop() {
  handleButton();

  if (provisioningActive) {
    setupServer.handleClient();
    if (millis() - provisioningStartedMs > PROVISIONING_TIMEOUT_MS) {
      stopProvisioningPortal();
    }
    delay(10);
    return;
  }

  unsigned long now = millis();

  if (now - lastCycleMs >= sendIntervalMs) {
    lastCycleMs = now;
    runUploadCycle();
  }

  if (now - lastCommandCheckMs >= COMMAND_CHECK_INTERVAL_MS) {
    lastCommandCheckMs = now;
    checkCommands();
  }

  if (now - lastOtaCheckMs >= OTA_CHECK_INTERVAL_MS) {
    lastOtaCheckMs = now;
    checkForOtaUpdate();
  }

  delay(1000);
}