// accel.h — low-frequency hive-vibration capture from one or two ST LIS3DH /
// LIS2DH12 MEMS accelerometers on the shared I2C bus.
//
// The whole feature is compiled out unless ENABLE_LIS3DH_ACCEL is set in
// secrets.h. One accelerometer is mounted per hive (mirroring the dual load
// cells, the stereo INMP441 mics and the two BeeCounters):
//
//   slot 1 -> hive 1, I2C address 0x18 (SDO/SA0 tied to GND)
//   slot 2 -> hive 2, I2C address 0x19 (SDO/SA0 tied to VCC)
//
// Why an accelerometer in addition to the microphones?
// ----------------------------------------------------
// Uthoff, Nabhan Homsi & von Bergen (2023), "Acoustic and vibration monitoring
// of honeybee colonies ...", Comput. Electron. Agric. 205:107589, review the
// field and highlight that the single most predictive swarming signal found to
// date — Ramsey et al. (2020) — is a substrate-borne vibration at ~20 Hz that
// rises days-to-weeks before the swarm and is strongest at night. Crucially
// that band is BELOW what hive microphones capture well (most have a ~50 Hz
// floor), so the review explicitly recommends adding a low-frequency
// accelerometer to "maximise the data quality". This module fills exactly that
// gap: it samples comb/wall vibration and reports the energy in three
// low-frequency bands plus a broadband RMS, per hive, every upload cycle.
//
// The LIS3DH (prototype) and LIS2DH12 (final BOM) share the same WHO_AM_I
// (0x33), the same control/data register map and the same 0x18/0x19 I2C
// addresses, so this register-level driver drives both unchanged.
#pragma once

#include <Arduino.h>
#include "config.h"

#if ENABLE_LIS3DH_ACCEL

#include <Wire.h>
#include <ArduinoJson.h>

namespace accel {

// Default I2C addresses (SDO/SA0 low / high). Overridable in secrets.h.
constexpr uint8_t SLAVE_ADDR_SLOT_1 = LIS3DH_ADDR_SLOT_1;  // hive 1
constexpr uint8_t SLAVE_ADDR_SLOT_2 = LIS3DH_ADDR_SLOT_2;  // hive 2

// Expected WHO_AM_I for LIS3DH and LIS2DH12 (identical).
constexpr uint8_t WHO_AM_I_VALUE = 0x33;

// ── Vibration analysis bands (Hz) ───────────────────────────────────────────
// Fixed in firmware (not per-device config) so a value means the same thing on
// every hive and across firmware versions, exactly like the mic FFT bands.
//
//   swarm    8 – 30 Hz   Ramsey et al. (2020) ~20 Hz pre-swarm vibration —
//                        the headline low-frequency signal the mics can't hear.
//   fanning 30 – 100 Hz  ventilation / fanning wing-beat & low worker activity.
//   activity 100 – 200 Hz general in-comb worker activity / buzz fundamentals.
//
// The upper edge (200 Hz) is the Nyquist limit at the default 400 Hz ODR; the
// queen-piping range (300–550 Hz) is already covered by the INMP441 "piping"
// mic band, so the accelerometer deliberately concentrates on the sub-audible
// part of the spectrum where it adds new information.
constexpr uint16_t BAND_SWARM_LO_HZ    = 8;
constexpr uint16_t BAND_SWARM_HI_HZ    = 30;
constexpr uint16_t BAND_FANNING_LO_HZ  = 30;
constexpr uint16_t BAND_FANNING_HI_HZ  = 100;
constexpr uint16_t BAND_ACTIVITY_LO_HZ = 100;
constexpr uint16_t BAND_ACTIVITY_HI_HZ = 200;

// One per-hive vibration snapshot, captured each upload cycle. All band/RMS
// values are AC (gravity / DC removed) and expressed in milli-g (mg).
struct AccelSnapshot {
  bool     present        = false;  // sensor acked and WHO_AM_I matched
  uint16_t sample_rate_hz = 0;      // output data rate actually configured
  uint16_t sample_count   = 0;      // vibration samples fed into the FFT
  uint8_t  range_g        = 0;      // full-scale (±2/4/8/16 g)
  float    rms_mg         = NAN;    // broadband AC RMS of |a| (DC removed)
  float    peak_mg        = NAN;    // peak |a − mean| over the capture
  float    band_swarm_mg    = NAN;  //   8 – 30 Hz  (pre-swarm predictor)
  float    band_fanning_mg  = NAN;  //  30 – 100 Hz (fanning / ventilation)
  float    band_activity_mg = NAN;  // 100 – 200 Hz (general worker activity)
};

// Configure the device at `address`, capture LIS3DH_SAMPLE_COUNT samples at the
// configured ODR, and fill `out` with the broadband RMS/peak and per-band
// energy. Returns true when the sensor was present (WHO_AM_I matched) and a
// capture completed. Safe to call every cycle: the control registers are
// (re)written each time so it recovers cleanly after a deep-sleep power cut.
bool readSlot(uint8_t address, AccelSnapshot& out);

// Serialize a snapshot into the measurement JSON under the per-slot key prefix
// "accel_{slot}_" (slot is 1 or 2), mirroring beecnt::writeSnapshotToJson.
void writeSnapshotToJson(JsonDocument& doc, uint8_t slot, const AccelSnapshot& snap);

}  // namespace accel

#endif  // ENABLE_LIS3DH_ACCEL
