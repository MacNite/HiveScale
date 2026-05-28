// network.h — connectivity layer: WiFi association, HTTP(S) JSON requests,
// measurement upload + retry-cache drain, remote config, OTA and the
// device command queue.
#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// ---- URL helpers ----------------------------------------------------------
String trimTrailingSlash(String value);
String apiUrl(const String& path);
String absoluteUrl(String maybeRelativeUrl);

// ---- WiFi -----------------------------------------------------------------
bool connectWifi(unsigned long timeoutMs = WIFI_CONNECT_TIMEOUT_MS);
bool connectNetwork();

// ---- HTTP -----------------------------------------------------------------
bool httpGetJson(const String& url, JsonDocument& doc);
bool httpPostJson(const String& url, const String& json, String* response = nullptr);

// ---- Upload ---------------------------------------------------------------
bool uploadLine(const String& line);
bool uploadCachedLines();

// ---- Config / OTA / commands ---------------------------------------------
void fetchRemoteConfig();
bool performFirmwareUpdate(const String& firmwareUrl);
void checkForOtaUpdate();
void postCommandResult(int commandId, bool success, const String& message);
void checkCommands();
