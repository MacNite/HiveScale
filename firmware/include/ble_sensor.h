// ble_sensor.h — passive BLE bridge for the HolyIot 25015 in-hive sensor.
//
// The HolyIot 25015 (nRF54L15) is a battery BLE beacon that broadcasts the
// readings of three on-board sensors:
//   - SHT40   : temperature + relative humidity
//   - LPS22HB : barometric pressure
//   - LIS2DH12: 3-axis acceleration
//
// The ESP32 never connects to the device. Once per upload cycle it runs a short
// passive scan (HOLYIOT_BLE_SCAN_SECONDS), matches advertisements against the
// one or two MAC addresses paired in the provisioning portal, parses the
// manufacturer-specific payload and folds the values into the measurement JSON:
//
//   slot 1 -> hive 1   (bleSensorMac0)
//   slot 2 -> hive 2   (bleSensorMac1)
//
// Per the data-model decision, the acceleration is reported through the existing
// accel_{slot}_* measurement fields (ok / rms_mg / peak_mg / sample_count /
// range_g); temperature, humidity and pressure are reported through new
// ble_{slot}_* fields. Because a passive beacon only emits periodic single-shot
// samples, no FFT bands are produced — the server runs a low-rate pre-swarm
// detector on the per-cycle acceleration magnitude instead.
//
// The whole feature is compiled out unless ENABLE_HOLYIOT_BLE is set.
#pragma once

#include <Arduino.h>
#include "config.h"

#if ENABLE_HOLYIOT_BLE

#include <ArduinoJson.h>
#include <vector>

namespace blesensor {

// One per-hive sensor snapshot, captured each upload cycle. Acceleration is in
// milli-g (mg); *_rms_mg / *_peak_mg are the AC magnitude (gravity removed)
// across the advertisements seen during the scan window.
struct Snapshot {
  bool     present       = false;  // a matching advertisement was received
  int      rssi_dbm      = 0;      // last advertisement RSSI
  uint16_t sample_count  = 0;      // advertisements parsed during the scan

  float    temp_c        = NAN;
  float    humidity_pct  = NAN;
  float    pressure_hpa  = NAN;

  float    accel_x_mg    = NAN;    // last raw sample
  float    accel_y_mg    = NAN;
  float    accel_z_mg    = NAN;
  float    accel_rms_mg  = NAN;    // RMS of |a|-baseline over the samples seen
  float    accel_peak_mg = NAN;    // peak |a|-baseline over the samples seen

  int      battery_pct   = -1;     // -1 = not reported
};

// One discovered device during a portal pairing scan.
struct Discovered {
  String  mac;
  String  name;
  int     rssi_dbm = 0;
  bool    looks_like_holyiot = false;  // carried a parseable HolyIot payload
};

// Run a single passive scan and fill the snapshots for the two paired MACs.
// Either MAC may be empty (""), in which case that slot stays !present. Safe to
// call every cycle; it initialises and de-initialises the BLE stack each time
// so it coexists cleanly with the WiFi upload that follows.
void scanPairedSensors(const String& mac0, const String& mac1,
                       Snapshot& slot1, Snapshot& slot2);

// Portal helper: scan for all nearby BLE devices so the user can pick which to
// pair. HolyIot-looking devices are flagged. Used by the provisioning portal.
std::vector<Discovered> discover(uint32_t seconds);

// Serialize a snapshot into the measurement JSON. Writes the new ble_{slot}_*
// humidity/pressure/accel-raw/battery fields and mirrors the acceleration into
// the existing accel_{slot}_* fields (ok / rms_mg / peak_mg / sample_count /
// range_g). Temperature is NOT written here — sensors.cpp owns hive_{slot}_temp_c
// so it can choose between the wired DS18B20 and this sensor.
void writeSnapshotToJson(JsonDocument& doc, uint8_t slot, const Snapshot& snap);

// Normalise a MAC string ("aa:bb:..", upper/lower, spaces) to "AA:BB:CC:DD:EE:FF"
// or "" when it is not a valid 6-byte MAC. Shared by the portal and matcher.
String normalizeMac(const String& raw);

}  // namespace blesensor

#endif  // ENABLE_HOLYIOT_BLE
