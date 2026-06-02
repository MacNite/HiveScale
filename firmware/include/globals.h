// globals.h — hardware driver objects and mutable runtime state shared across
// modules. Declarations only; definitions live in globals.cpp.
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HX711.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_SHT4x.h>
#include <RTClib.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_sleep.h>

#include "config.h"

#if ENABLE_INA219_SOLAR
#include <Adafruit_INA219.h>
#endif
#if ENABLE_MAX17048_BATTERY
#include <SparkFun_MAX1704x_Fuel_Gauge_Arduino_Library.h>
#endif

extern const char* const FIRMWARE_VERSION;

// ---- Hardware driver instances -------------------------------------------
extern HX711 scale1;
extern HX711 scale2;
extern OneWire oneWire;
extern DallasTemperature ds18b20;
extern Adafruit_SHT4x sht4;
extern RTC_DS3231 rtc;
extern Preferences prefs;
extern WebServer setupServer;
extern DNSServer setupDnsServer;

#if ENABLE_INA219_SOLAR
extern Adafruit_INA219 solarMonitor;
extern bool solarMonitorOk;
#endif
#if ENABLE_MAX17048_BATTERY
extern SFE_MAX1704X batteryGauge;
extern bool batteryMonitorOk;
#endif

// ---- Runtime flags --------------------------------------------------------
extern bool sdOk;
extern bool sdBusInitialized;
extern bool shtOk;
extern bool rtcOk;
extern bool provisioningActive;
extern bool calibrationModeActive;

// ---- Timing / scheduling --------------------------------------------------
extern unsigned long lastCycleMs;
extern unsigned long lastOtaCheckMs;
extern unsigned long lastCommandCheckMs;
extern unsigned long provisioningStartedMs;
extern unsigned long sendIntervalMs;
extern unsigned long calibrationModeStartedMs;
extern unsigned long calibrationModeIntervalMs;
extern unsigned long calibrationModeTimeoutMs;

// ---- Config / identity ----------------------------------------------------
extern String timeSource;
extern String apiBaseUrl;
extern String apiKey;
extern String deviceId;
extern String claimCode;
extern String activeWifiSsid;
extern String lastMeasurementJson;
extern unsigned long lastMeasurementUpdatedMs;

// ---- Scale calibration ----------------------------------------------------
extern long scale1Offset;
extern long scale2Offset;
extern float scale1Factor;
extern float scale2Factor;

// ---- Button state ---------------------------------------------------------
extern bool buttonWasDown;
extern unsigned long buttonDownMs;
extern bool longPressHandled;

// ---- Values that survive deep sleep --------------------------------------
// The RTC_DATA_ATTR (section) attribute belongs only on the definitions in
// globals.cpp. Repeating it here would generate a second, auto-numbered RTC
// section that conflicts with the definition's, which the compiler then
// discards with a -Wattributes warning. Plain extern declarations are enough.
extern uint32_t rtcCyclesUntilOta;
extern uint32_t rtcBootCount;

// ---- Small shared utilities ----------------------------------------------
void debugLine();
bool isBlank(const String& s);
String trimTrailingSlash(String value);
