#ifndef SECRETS_H
#define SECRETS_H

// ==============================
// DEVICE IDENTITY
// ==============================
// Unique per device (can be overwritten later via backend provisioning)
#define DEVICE_ID        "hive-001"

// Per-device API key (recommended) OR shared key (your current setup)
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
#define WIFI2_SSID       "your-wifi-ssid-2"
#define WIFI2_PASS       "your-wifi-password-2"

// --- WiFi Set 3 ---
#define WIFI3_SSID       "your-wifi-ssid-3"
#define WIFI3_PASS       "your-wifi-password-3"

// ==============================
// OPTIONAL OFF-GRID MODULES
// ==============================
// Keep these as numeric 0/1 values because the firmware uses preprocessor #if.
// They are per-device build configuration rather than secrets, but this project
// already uses secrets.h as the local, untracked per-device config file.
#define ENABLE_INA219_SOLAR      0
#define ENABLE_MAX17048_BATTERY  0
#define ENABLE_SIM7080G          0

// OTA over cellular is intentionally disabled by default to avoid large data use.
// Normal measurement upload and command polling work over SIM7080G when enabled.
#define CELLULAR_OTA_ENABLED     0

// INA219 solar monitor. Default address is 0x40 on most breakout boards.
#define INA219_I2C_ADDRESS       0x40

// MAX17048 LiPo fuel gauge alert threshold, in percent.
#define MAX17048_ALERT_PERCENT   20

// SIM7080G network settings. APN is required for most SIMs.
#define SIM7080G_APN             ""
#define SIM7080G_USER            ""
#define SIM7080G_PASS            ""
#define SIM7080G_PIN             ""

// SIM7080G UART and power-control pins.
// Adjust RX/TX to match your ESP32 wiring. RX means ESP32 RX connected to modem TX.
#define SIM7080G_BAUD            115200
#define SIM7080G_RX_PIN          26
#define SIM7080G_TX_PIN          25

// Set to a GPIO if your board exposes modem PWRKEY / power enable, otherwise -1.
#define SIM7080G_PWRKEY_PIN      -1
#define SIM7080G_POWER_EN_PIN    -1
#define SIM7080G_POWER_EN_ACTIVE_HIGH 1

// Cellular attach timeouts. NB-IoT/LTE-M registration can be slow off-grid.
#define SIM7080G_NETWORK_TIMEOUT_MS 180000UL
#define SIM7080G_GPRS_TIMEOUT_MS    60000UL

// ==============================
// OPTIONAL FLAGS
// ==============================

// If true, always reseed Preferences from secrets on boot (DANGEROUS)
#define FORCE_RESEED     false

// If true, enable extra serial debug logs
#define DEBUG_MODE       true

#endif // SECRETS_H