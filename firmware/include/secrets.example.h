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
// OPTIONAL FLAGS
// ==============================

// If true, always reseed Preferences from secrets on boot (DANGEROUS)
#define FORCE_RESEED     false

// If true, enable extra serial debug logs
#define DEBUG_MODE       true

#endif // SECRETS_H