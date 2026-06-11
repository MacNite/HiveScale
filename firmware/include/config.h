// config.h — compile-time configuration: pins, timeouts, file paths and
// optional-feature defaults. Pure preprocessor + constants, no globals.
//
// secrets.h is included first so that per-device overrides (feature flags,
// pin choices, sample rates) take effect before the defaults below.
#pragma once

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

// ==============================
// OPTIONAL OFF-GRID FEATURES
// ==============================
// Keep all optional hardware compiled out by default. Enable per device in
// secrets.h with 0/1 values.
#ifndef ENABLE_INA219_SOLAR
#define ENABLE_INA219_SOLAR 0
#endif

#ifndef ENABLE_MAX17048_BATTERY
#define ENABLE_MAX17048_BATTERY 0
#endif

#ifndef INA219_I2C_ADDRESS
#define INA219_I2C_ADDRESS 0x40
#endif

#ifndef MAX17048_ALERT_PERCENT
#define MAX17048_ALERT_PERCENT 20
#endif

// ==============================
// INMP441 STEREO MICS (defaults)
// ==============================
#ifndef ENABLE_INMP441_MICS
#define ENABLE_INMP441_MICS 0
#endif

#ifndef INMP441_BCLK_PIN
#define INMP441_BCLK_PIN 14
#endif

#ifndef INMP441_WS_PIN
#define INMP441_WS_PIN 13
#endif

#ifndef INMP441_SD_PIN
#define INMP441_SD_PIN 34
#endif

#ifndef INMP441_SAMPLE_RATE
#define INMP441_SAMPLE_RATE 16000
#endif

#ifndef INMP441_SAMPLE_FRAMES
#define INMP441_SAMPLE_FRAMES 8000
#endif

// Use I2S port 0. Port 0 has access to the most peripherals on the ESP32.
#ifndef INMP441_I2S_PORT
#define INMP441_I2S_PORT I2S_NUM_0
#endif

// ==============================
// LIS3DH / LIS2DH12 ACCELEROMETER (defaults)
// ==============================
// One low-g MEMS accelerometer per hive on the shared I2C bus, for capturing
// low-frequency comb/wall vibration (notably the ~20 Hz pre-swarm signal the
// microphones can't reach — see accel.h and docs/accelerometer.md). The LIS3DH
// (prototype) and LIS2DH12 (final BOM) share a register map and addresses, so
// the same driver handles both. Compiled out unless enabled in secrets.h.
#ifndef ENABLE_LIS3DH_ACCEL
#define ENABLE_LIS3DH_ACCEL 0
#endif

// I2C addresses, set in hardware by each board's SDO/SA0 pin:
//   SDO/SA0 -> GND = 0x18 (hive 1), SDO/SA0 -> VCC = 0x19 (hive 2).
#ifndef LIS3DH_ADDR_SLOT_1
#define LIS3DH_ADDR_SLOT_1 0x18
#endif
#ifndef LIS3DH_ADDR_SLOT_2
#define LIS3DH_ADDR_SLOT_2 0x19
#endif

// Output data rate in Hz. 400 Hz (Nyquist 200 Hz) cleanly resolves the swarm
// (8–30 Hz), fanning (30–100 Hz) and activity (100–200 Hz) bands while staying
// power-frugal. Supported: 10/25/50/100/200/400 (others fall back to 400).
#ifndef LIS3DH_ODR_HZ
#define LIS3DH_ODR_HZ 400
#endif

// Samples captured per hive per cycle. Clamped to a power of two for the FFT;
// 256 @ 400 Hz ≈ 640 ms and 1.56 Hz/bin resolution.
#ifndef LIS3DH_SAMPLE_COUNT
#define LIS3DH_SAMPLE_COUNT 256
#endif

// Full-scale range in g (±2/4/8/16). ±2 g maximises sensitivity for the small
// substrate-borne vibrations of interest.
#ifndef LIS3DH_RANGE_G
#define LIS3DH_RANGE_G 2
#endif

// ==============================
// PIN MAP
// ==============================
#define HX1_DOUT 16
#define HX1_SCK  17
#define HX2_DOUT 32
#define HX2_SCK  33
#define ONE_WIRE_PIN 4
#define I2C_SDA 21
#define I2C_SCL 22
#define SD_CS   5
#define SD_SCK  18
#define SD_MISO 23
#define SD_MOSI 19

// External button. Wire button between this pin and GND. Uses INPUT_PULLUP.
// Short press: start WiFi provisioning AP.
// Long press: reset Preferences and reboot.
#define SETUP_BUTTON_PIN 27
static const unsigned long BUTTON_DEBOUNCE_MS = 50;
static const unsigned long BUTTON_LONG_PRESS_MS = 10000;

static const int MAX_WIFI_NETWORKS = 3;
static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
static const unsigned long PROVISIONING_TIMEOUT_MS = 10UL * 60UL * 1000UL;
static const unsigned long OTA_CHECK_INTERVAL_MS = 6UL * 60UL * 60UL * 1000UL;
static const unsigned long COMMAND_CHECK_INTERVAL_MS = 5UL * 60UL * 1000UL;
static const unsigned long CALIBRATION_MODE_DEFAULT_INTERVAL_MS = 5UL * 1000UL;
static const unsigned long CALIBRATION_MODE_MIN_INTERVAL_MS = 2UL * 1000UL;
static const unsigned long CALIBRATION_MODE_MAX_INTERVAL_MS = 30UL * 1000UL;
static const unsigned long CALIBRATION_MODE_DEFAULT_TIMEOUT_MS = 10UL * 60UL * 1000UL;
static const unsigned long CALIBRATION_MODE_MAX_TIMEOUT_MS = 30UL * 60UL * 1000UL;

// Power saving behavior. With deep sleep enabled, the ESP32 wakes for one
// measurement/upload cycle, then sleeps until the next send interval.
static const bool DEEP_SLEEP_ENABLED = true;
static const bool WAKE_BUTTON_FROM_DEEP_SLEEP = true;
static const unsigned long MIN_DEEP_SLEEP_MS = 30UL * 1000UL;
static const uint64_t US_PER_MS = 1000ULL;

static const char* CACHE_FILE = "/cache.ndjson";
static const char* TEMP_FILE = "/cache.tmp";
static const char* CACHE_BAD_FILE = "/cache_bad.ndjson";
static const char* BACKUP_FILE = "/measurements.ndjson";

// SD behavior:
// - BACKUP_FILE is append-only and is never deleted by the firmware.
// - CACHE_FILE is ONLY the retry queue for rows that still need backend upload.
//   Successful live uploads are not written to the cache file.
static const bool SD_KEEP_PERSISTENT_BACKUP = true;
static const size_t BACKUP_WARN_SIZE_BYTES = 50UL * 1024UL * 1024UL;
static const size_t CACHE_MAX_BYTES = 512UL * 1024UL;
static const size_t CACHE_MAX_LINE_BYTES = 4096UL;
static const uint16_t CACHE_UPLOAD_MAX_LINES_PER_CYCLE = 25;
static const uint16_t CAPTIVE_DNS_PORT = 53;
static const size_t LAST_MEASUREMENT_TAIL_BYTES = CACHE_MAX_LINE_BYTES * 2;
