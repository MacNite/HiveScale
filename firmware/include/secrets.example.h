#ifndef SECRETS_H
#define SECRETS_H

// ==============================
// DEVICE IDENTITY
// ==============================
// Unique per device (can be overwritten later via backend provisioning)
#define DEVICE_ID        "hive-001"

// Per-device API key - can be created via CLI:   openssl rand -hex 32
#define API_KEY          "your-api-key-here"

// Human-entered pairing code. The firmware seeds this into Preferences and sends it
// with every measurement so the backend can auto-create an unclaimed device.
// Change CLAIM_CODE_REVISION when you want an OTA firmware update to overwrite
// the claim_code stored in Preferences.
#define CLAIM_CODE       "ABCD-1234"
#define CLAIM_CODE_REVISION 1

// ==============================
// BACKEND CONFIG
// ==============================
// Base URL of your backend (no trailing slash required)
#define API_BASE_URL     "https://your-backend-domain.com"

// ==============================
// WIFI FALLBACK CREDENTIALS
// ==============================
// These are ONLY used on first boot (seed into Preferences)
// or as fallback if Preferences are empty

// --- WiFi Set 1 ---
#define WIFI1_SSID       "your-wifi-ssid-1"
#define WIFI1_PASS       "your-wifi-password-1"

// --- WiFi Set 2 ---
//#define WIFI2_SSID       "your-wifi-ssid-2"
//#define WIFI2_PASS       "your-wifi-password-2"

// --- WiFi Set 3 ---
//#define WIFI3_SSID       "your-wifi-ssid-3"
//#define WIFI3_PASS       "your-wifi-password-3"

// ==============================
// OPTIONAL OFF-GRID MODULES
// ==============================
// Keep these as numeric 0/1 values because the firmware uses preprocessor #if.
// They are per-device build configuration rather than secrets, but this project
// already uses secrets.h as the local, untracked per-device config file.
#define ENABLE_INA219_SOLAR      0
#define ENABLE_MAX17048_BATTERY  0

// INA219 solar monitor. Default address is 0x40 on most breakout boards.
#define INA219_I2C_ADDRESS       0x40

// MAX17048 LiPo fuel gauge alert threshold, in percent.
#define MAX17048_ALERT_PERCENT   20

// ==============================
// INMP441 STEREO MICROPHONES
// ==============================
// Two INMP441 I2S MEMS microphones sharing a single I2S bus.
// Wire L/R on one mic to GND (left channel) and L/R on the other mic to 3.3V
// (right channel). Both mics share BCLK, WS (LRCLK) and SD (data) lines.
//
// Default pinout (free on this board):
//   GPIO 14 -> BCLK (SCK on the mic boards)
//   GPIO 13 -> WS   (LRCLK / WS on the mic boards)
//   GPIO 34 -> SD   (data out from both mics, ESP32 input-only pin)
//
// VDD on each mic -> 3.3V, GND -> GND.
#define ENABLE_INMP441_MICS      1

#define INMP441_BCLK_PIN         14
#define INMP441_WS_PIN           13
#define INMP441_SD_PIN           34

// Sample rate in Hz. 16 kHz is plenty for hive sounds (fundamental ~200 Hz,
// harmonics up to a few kHz) and keeps the buffer small.
#define INMP441_SAMPLE_RATE      16000

// Number of stereo frames captured per measurement cycle.
// 8000 frames at 16 kHz = ~500 ms of audio.
#define INMP441_SAMPLE_FRAMES    8000

// ==============================
// LIS3DH / LIS2DH12 ACCELEROMETERS (per-hive vibration)
// ==============================
// One MEMS accelerometer per hive on the shared I2C bus captures low-frequency
// comb/wall vibration — most importantly the ~20 Hz pre-swarm signal that hive
// microphones miss (Ramsey et al. 2020; Uthoff et al. 2023). The LIS3DH on the
// purple breakout (prototype) and the LIS2DH12TR (final BOM) are register- and
// address-compatible, so the same firmware drives both. See docs/wiring.md and
// docs/accelerometer.md.
//
// Wiring (I2C mode) per board:
//   VCC -> 3.3V, GND -> GND, SCL -> GPIO22, SDA -> GPIO21
//   CS  -> 3.3V            (forces I2C; LOW would select SPI)
//   SDO -> GND for hive 1 (address 0x18), SDO -> 3.3V for hive 2 (address 0x19)
//   INT1/INT2/ADC1-3      left unconnected (polled reads, no interrupts used)
#define ENABLE_LIS3DH_ACCEL      1

#define LIS3DH_ADDR_SLOT_1       0x18   // hive 1 (SDO/SA0 -> GND)
#define LIS3DH_ADDR_SLOT_2       0x19   // hive 2 (SDO/SA0 -> VCC)

// Output data rate (Hz): 10/25/50/100/200/400. 400 Hz gives a 200 Hz Nyquist,
// covering all three vibration bands; lower it to save power if you only care
// about the 8–30 Hz swarm band.
#define LIS3DH_ODR_HZ            400

// Samples per hive per cycle (clamped to a power of two for the FFT).
#define LIS3DH_SAMPLE_COUNT      256

// Full-scale range in g (2/4/8/16). 2 g maximises sensitivity.
#define LIS3DH_RANGE_G           2

// ==============================
// OPTIONAL FLAGS
// ==============================

// If true, always reseed Preferences from secrets on boot (DANGEROUS)
#define FORCE_RESEED     false

// If true, enable extra serial debug logs
#define DEBUG_MODE       true

#endif // SECRETS_H