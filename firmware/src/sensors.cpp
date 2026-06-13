// sensors.cpp — time sync, scale reads and measurement JSON assembly.
#include "sensors.h"
#include "globals.h"
#include "config.h"
#include "hivescale_network.h"
#include "storage_power.h"
#include "bee_counter_client.h"

#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>

#if ENABLE_INMP441_MICS
#include "mics.h"
#endif

#if ENABLE_HOLYIOT_BLE
#include "ble_sensor.h"
#endif

void initializeTime(bool wokeFromDeepSleep) {
  if (rtcHasValidTime()) {
    timeSource = "rtc";
    Serial.print("[TIME] Using RTC: ");
    Serial.println(timestampNow());

    // Refresh RTC from NTP on cold/manual boots, but avoid doing this on every
    // timer wake because WiFi will already be used for upload and RTC is valid.
    if (!wokeFromDeepSleep) {
      syncTime();
    }
    return;
  }

  syncTime();
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

long readAverageRaw(HX711& scale, int samples) {
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

  powerUpScales();

  // Wired DS18B20 in-hive probes are now optional. When disabled the hive
  // temperatures default to NAN and are filled from a paired HolyIot 25015 BLE
  // sensor further down (see the BLE block below).
  float hiveTemp1 = NAN;
  float hiveTemp2 = NAN;
#if ENABLE_DS18B20_HIVE_TEMP
  ds18b20.requestTemperatures();
  hiveTemp1 = ds18b20.getTempCByIndex(0);
  hiveTemp2 = ds18b20.getTempCByIndex(1);
#endif
  float ambientTemp = NAN;
  float ambientHumidity = NAN;

#if ENABLE_INA219_SOLAR
  float solarBusVoltage = NAN;
  float solarShuntVoltageMv = NAN;
  float solarLoadVoltage = NAN;
  float solarCurrentMa = NAN;
  float solarPowerMw = NAN;
#endif

#if ENABLE_MAX17048_BATTERY
  float batteryVoltage = NAN;
  float batterySoc = NAN;
  bool batteryAlert = false;
#endif

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

#if ENABLE_INA219_SOLAR
  if (solarMonitorOk) {
    solarMonitor.powerSave(false);
    delay(10);
    solarBusVoltage = solarMonitor.getBusVoltage_V();
    solarShuntVoltageMv = solarMonitor.getShuntVoltage_mV();
    solarCurrentMa = solarMonitor.getCurrent_mA();
    solarPowerMw = solarMonitor.getPower_mW();
    solarLoadVoltage = solarBusVoltage + (solarShuntVoltageMv / 1000.0f);
    solarMonitor.powerSave(true);
  }
#endif

#if ENABLE_MAX17048_BATTERY
  if (batteryMonitorOk) {
    batteryVoltage = batteryGauge.getVoltage();
    batterySoc = batteryGauge.getSOC();
    batteryAlert = batteryGauge.getAlert();
    // The ALRT bit is sticky — clear it after reading so the chip can
    // re-assert it on the next internal cycle if SOC is still below threshold.
    if (batteryAlert) batteryGauge.clearAlert();
  }
#endif
#if ENABLE_INMP441_MICS
  MicMeasurement micResult = readMicSamples();
#endif

#if ENABLE_HOLYIOT_BLE
  // Passive BLE bridge for the in-hive HolyIot 25015 sensors. One short scan
  // fills both slots (slot 1 -> hive 1, slot 2 -> hive 2) from the MACs paired
  // in the provisioning portal. Temperature, humidity, pressure and a per-cycle
  // acceleration magnitude come back per slot; a missing/unpaired sensor just
  // reports present=false. Done before the WiFi upload so the BLE controller is
  // released first.
  blesensor::Snapshot bleSnap1;
  blesensor::Snapshot bleSnap2;
  blesensor::scanPairedSensors(bleSensorMac0, bleSensorMac1, bleSnap1, bleSnap2);

  // In-hive temperature source: prefer the wired DS18B20 when it produced a
  // valid reading; otherwise fall back to the BLE sensor's SHT40 temperature.
  if (isnan(hiveTemp1) && bleSnap1.present && !isnan(bleSnap1.temp_c)) hiveTemp1 = bleSnap1.temp_c;
  if (isnan(hiveTemp2) && bleSnap2.present && !isnan(bleSnap2.temp_c)) hiveTemp2 = bleSnap2.temp_c;
#endif

// ---- BeeCounter polling -------------------------------------------------
  // Poll both possible BeeCounters on the shared I2C bus. Each slot is
  // independent — a missing counter just reports "ok=false". Reading both
  // takes roughly 30–80 ms; well within our wake budget.
  beecnt::Snapshot beeSnap1;
  beecnt::Snapshot beeSnap2;
  (void)beecnt::pollSlot(beecnt::SLAVE_ADDR_SLOT_1, beeSnap1);
  (void)beecnt::pollSlot(beecnt::SLAVE_ADDR_SLOT_2, beeSnap2);

  Serial.printf("[MEASURE] raw1=%ld weight1=%.3f kg\n", raw1, weight1);
  Serial.printf("[MEASURE] raw2=%ld weight2=%.3f kg\n", raw2, weight2);
  Serial.printf("[MEASURE] hiveTemp1=%.2f hiveTemp2=%.2f\n", hiveTemp1, hiveTemp2);
  Serial.printf("[MEASURE] ambientTemp=%.2f humidity=%.2f\n", ambientTemp, ambientHumidity);
#if ENABLE_INA219_SOLAR
  Serial.printf("[MEASURE] solar load=%.3f V current=%.2f mA power=%.2f mW\n", solarLoadVoltage, solarCurrentMa, solarPowerMw);
#endif
#if ENABLE_MAX17048_BATTERY
  Serial.printf("[MEASURE] battery=%.3f V soc=%.1f%% alert=%s\n", batteryVoltage, batterySoc, batteryAlert ? "yes" : "no");
#endif

  JsonDocument doc;
  doc["device_id"] = deviceId;
  if (claimCode.length() > 0 && !claimRegistered) doc["claim_code"] = claimCode;
  // Only attach a client timestamp when the device actually knows the time.
  // When RTC and NTP have both failed (timeSource == "invalid"), timestampNow()
  // returns the 1970-01-01 epoch fallback. The server stores the client
  // timestamp verbatim, so sending 1970 silently freezes "last data" in the
  // dashboard at the last good reading even though uploads keep succeeding.
  // Omitting the field instead lets the server stamp the row with its own clock.
  if (timeSource != "invalid") {
    doc["timestamp"] = timestampNow();
  }
  doc["scale_1_weight_kg"] = weight1;
  doc["scale_2_weight_kg"] = weight2;
  doc["hive_1_temp_c"] = hiveTemp1;
  doc["hive_2_temp_c"] = hiveTemp2;
  doc["ambient_temp_c"] = ambientTemp;
  doc["ambient_humidity_percent"] = ambientHumidity;
  doc["network_transport"] = "wifi";
  doc["rssi_dbm"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
  doc["firmware_version"] = FIRMWARE_VERSION;
  doc["calibration_mode"] = calibrationModeActive;
  doc["boot_count"] = rtcBootCount;
  doc["time_source"] = timeSource;
  doc["scale_1_raw"] = raw1;
  doc["scale_2_raw"] = raw2;
  doc["sd_ok"] = sdOk;
  doc["rtc_ok"] = rtcOk;
  doc["sht_ok"] = shtOk;
#if ENABLE_INA219_SOLAR
  doc["solar_monitor_ok"] = solarMonitorOk;
  doc["solar_bus_voltage_v"] = solarBusVoltage;
  doc["solar_shunt_voltage_mv"] = solarShuntVoltageMv;
  doc["solar_load_voltage_v"] = solarLoadVoltage;
  doc["solar_current_ma"] = solarCurrentMa;
  doc["solar_power_mw"] = solarPowerMw;
#endif
#if ENABLE_MAX17048_BATTERY
  doc["battery_monitor_ok"] = batteryMonitorOk;
  doc["battery_voltage_v"] = batteryVoltage;
  doc["battery_soc_percent"] = batterySoc;
  doc["battery_alert"] = batteryAlert;
#endif
#if ENABLE_INMP441_MICS
  doc["mic_ok"]                       = micResult.ok;
  doc["mic_sample_rate_hz"]           = (uint32_t)INMP441_SAMPLE_RATE;
  doc["mic_sample_frames"]            = (uint32_t)INMP441_SAMPLE_FRAMES;
  doc["mic_left_ok"]                  = micResult.left.ok;
  doc["mic_left_rms_dbfs"]            = micResult.left.rmsDbfs;
  doc["mic_left_peak_dbfs"]           = micResult.left.peakDbfs;
  doc["mic_left_rms_normalized"]      = micResult.left.rmsNormalized;
  doc["mic_right_ok"]                 = micResult.right.ok;
  doc["mic_right_rms_dbfs"]           = micResult.right.rmsDbfs;
  doc["mic_right_peak_dbfs"]          = micResult.right.peakDbfs;
  doc["mic_right_rms_normalized"]     = micResult.right.rmsNormalized;
  doc["mic_left_band_sub_bass_dbfs"]  = micResult.left.bands.sub_bass_dbfs;
  doc["mic_left_band_hum_dbfs"]       = micResult.left.bands.hum_dbfs;
  doc["mic_left_band_piping_dbfs"]    = micResult.left.bands.piping_dbfs;
  doc["mic_left_band_stress_dbfs"]    = micResult.left.bands.stress_dbfs;
  doc["mic_left_band_high_dbfs"]      = micResult.left.bands.high_dbfs;
  doc["mic_right_band_sub_bass_dbfs"] = micResult.right.bands.sub_bass_dbfs;
  doc["mic_right_band_hum_dbfs"]      = micResult.right.bands.hum_dbfs;
  doc["mic_right_band_piping_dbfs"]   = micResult.right.bands.piping_dbfs;
  doc["mic_right_band_stress_dbfs"]   = micResult.right.bands.stress_dbfs;
  doc["mic_right_band_high_dbfs"]     = micResult.right.bands.high_dbfs;
#endif

  beecnt::writeSnapshotToJson(doc, 1, beeSnap1);
  beecnt::writeSnapshotToJson(doc, 2, beeSnap2);

#if ENABLE_HOLYIOT_BLE
  blesensor::writeSnapshotToJson(doc, 1, bleSnap1);
  blesensor::writeSnapshotToJson(doc, 2, bleSnap2);
#endif

  String output;
  serializeJson(doc, output);
  rememberLastMeasurement(output);

  Serial.print("[MEASURE] JSON: ");
  Serial.println(output);
  return output;
}
