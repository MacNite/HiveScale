# HiveScale

**ESP32-based dual beehive scale system** — monitors the weight, temperature, and humidity of two beehives simultaneously and sends measurements to a self-hosted FastAPI backend backed by PostgreSQL.

---

## Features

- **Dual load cells** (HX711) for weighing two hives independently
- **Per-hive temperature** via DS18B20 (Dallas 1-Wire) sensors
- **Ambient temperature & humidity** via Adafruit SHT4x
- **RTC (DS3231)** for accurate timestamping without NTP dependency
- **SD card cache** — measurements are buffered locally when Wi-Fi is unavailable and uploaded automatically on reconnect
- **Claim-code pairing** — devices identify themselves with a claim code until claimed by a user; no manual registration required
- **Remote configuration** — sampling interval, scale offsets, and calibration factors are pulled from the server on each cycle
- **Remote commands** — tare, calibrate, and reboot the device over the API
- **OTA firmware updates** — the device checks for a newer firmware version every 6 hours and updates itself automatically
- **Wi-Fi provisioning portal** — press the setup button to start a local AP for configuring Wi-Fi and backend settings without re-flashing
- **Multi-network Wi-Fi** — up to 3 Wi-Fi networks are tried in order on each cycle
- **HivePal integration** — a dedicated app API (`/api/v1/app/…`) allows HivePal (or any other app) to claim, manage, and read devices on behalf of its users
- **FastAPI + PostgreSQL backend** — a simple, self-hostable REST API with auto-provisioned schema
- **Docker Compose deployment** — single command to run the API and database

---

## Repository Structure

```
HiveScale/
├── firmware/               # ESP32 PlatformIO project
│   ├── src/main.cpp        # Main firmware source
│   ├── include/
│   │   └── secrets.example.h
│   └── platformio.ini
├── server/                 # Python FastAPI backend
│   ├── main.py
│   ├── requirements.txt
│   └── Dockerfile
├── docker/                 # Docker Compose deployment
│   ├── docker-compose.yml
│   └── .env.example
├── docs/                   # Additional documentation
│   ├── api.md              # Full API reference
│   ├── test-commands.md    # Quick curl test commands
│   ├── truenas-install.md  # TrueNAS Scale deployment guide
│   ├── docker-install.md   # Generic Docker deployment guide
│   └── wiring.md           # Hardware wiring reference
└── .github/workflows/      # CI: builds & pushes Docker image to GHCR
```

---

## Hardware

| Component | Role | Approx. Price |
|---|---|---|
| ESP32 Dev Board | Microcontroller | 8 € |
| MP1584EN | DC-DC converter for power input | 2 € |
| 2× HX711 + load cells | Weight measurement (scale 1 & 2) | 7–30 € |
| 2× DS18B20 (2 m cable) | Per-hive internal temperature (1-Wire bus) | 4 € |
| SHT4x (with cable) | Ambient temperature & humidity | 3 € |
| DS3231 RTC | Hardware real-time clock | 5 € |
| MicroSD card module + card | Local measurement cache | 10 € |
| Momentary pushbutton | Setup / factory-reset button (optional) | 1 € |
| IP67 electronics box (≥ 150 × 150 mm) | Weatherproof enclosure | 15 € |
| Wood for scale frame | Mounting sensors under hives | 0–10 € |
| Hardware, wires, connectors | Cabling and enclosure finishing | 10 € |

**Total: ~65–100 € for 2 complete scales**

### Pin Mapping

| Signal | GPIO |
|---|---|
| HX711 #1 DOUT | 16 |
| HX711 #1 SCK | 17 |
| HX711 #2 DOUT | 32 |
| HX711 #2 SCK | 33 |
| DS18B20 (1-Wire data) | 4 |
| I²C SDA (RTC + SHT4x) | 21 |
| I²C SCL | 22 |
| SD CS | 5 |
| SD SCK | 18 |
| SD MISO | 19 |
| SD MOSI | 23 |
| Setup button | 27 (INPUT_PULLUP — connect to GND) |

> See [docs/wiring.md](docs/wiring.md) for a detailed wiring diagram and component-level notes.

---

## Firmware Setup

### Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)

### Configuration

Copy the secrets template and fill in your values:

```bash
cp firmware/include/secrets.example.h firmware/include/secrets.h
```

Edit `firmware/include/secrets.h`:

```cpp
// Device identity
#define DEVICE_ID           "hive-001"
#define API_KEY             "your-api-key-here"

// Claim code — sent with every measurement until the device is claimed
#define CLAIM_CODE          "ABCD-1234"
#define CLAIM_CODE_REVISION 1   // increment to push a new claim code via OTA

// Backend
#define API_BASE_URL        "https://your-backend-domain.com"

// Wi-Fi — up to 3 networks, tried in order
#define WIFI1_SSID          "your-wifi-ssid-1"
#define WIFI1_PASS          "your-wifi-password-1"
#define WIFI2_SSID          "your-wifi-ssid-2"
#define WIFI2_PASS          "your-wifi-password-2"
```

> **Note:** Values in `secrets.h` are only used on first boot to seed the device's persistent storage (`Preferences`). Subsequent configuration is managed remotely or via the provisioning portal. Set `FORCE_RESEED true` if you need to overwrite stored values.

### Flash

```bash
cd firmware
pio run --target upload
pio device monitor   # 115200 baud
```

### PlatformIO Dependencies

The following libraries are installed automatically via `platformio.ini`:

- `bogde/HX711` ^0.7.5
- `paulstoffregen/OneWire` ^2.3.8
- `milesburton/DallasTemperature` ^4.0.6
- `adafruit/Adafruit SHT4x Library` ^1.0.5
- `adafruit/RTClib` ^2.1.4
- `bblanchon/ArduinoJson` ^7.2.2

---

## Wi-Fi Provisioning Portal

If you need to change Wi-Fi credentials or backend settings without re-flashing, the firmware includes a built-in provisioning portal.

**Short press** the setup button (GPIO 27): the device starts a Wi-Fi access point named `HiveScale-Setup-XXXX`. Connect to it and open `http://192.168.4.1` to update Wi-Fi networks, device ID, claim code, and API settings. Saving reboots the device automatically.

**Long press (5 s):** factory-resets all stored Preferences and reboots.

The provisioning portal closes automatically after 10 minutes.

---

## Server Setup

### Docker Compose (recommended)

```bash
cd docker
cp .env.example .env   # edit API_KEY, HIVEPAL_SERVICE_API_KEY, passwords, volume path
docker compose up -d
```

The API will be available on port `31115` by default.

**docker-compose.yml defaults:**

| Setting | Value |
|---|---|
| API image | `ghcr.io/macnite/hivescale-api:latest` |
| API port | `31115` |
| DB image | `postgres:16-alpine` |
| DB name / user | `hivescale` |

> **Important:** Change `API_KEY`, `HIVEPAL_SERVICE_API_KEY`, and the PostgreSQL password before exposing the service to a network.

The database schema is created automatically on first startup — no migrations to run.

For platform-specific deployment guides see:
- [docs/docker-install.md](docs/docker-install.md) — generic Linux / VPS setup
- [docs/truenas-install.md](docs/truenas-install.md) — TrueNAS Scale (Custom App)

### Manual / Local

```bash
cd server
pip install -r requirements.txt
DATABASE_URL="postgresql://hivescale:password@localhost:5432/hivescale" \
API_KEY="your-secret-key" \
HIVEPAL_SERVICE_API_KEY="your-hivepal-service-key" \
PUBLIC_BASE_URL="https://your-domain.example.com" \
uvicorn main:app --host 0.0.0.0 --port 8000
```

### Environment Variables

| Variable | Required | Description |
|---|---|---|
| `DATABASE_URL` | ✅ | PostgreSQL connection string |
| `API_KEY` | ✅ | Key used by ESP32 firmware (`X-API-Key` header) |
| `HIVEPAL_SERVICE_API_KEY` | ✅ (for HivePal) | Key used by HivePal backend (`X-HivePal-Service-Key` header) |
| `PUBLIC_BASE_URL` | Recommended | Base URL used to build OTA firmware download links |
| `FIRMWARE_DIR` | Optional | Path to firmware binaries (default: `/app/firmware`) |
| `TZ` | Optional | Server timezone (e.g. `Europe/Berlin`) |

---

## API Overview

Interactive docs (Swagger UI) are available at `http://<host>:31115/docs`.
See [docs/api.md](docs/api.md) for the full reference including request/response schemas.

### Device-facing endpoints

All require the `X-API-Key` header.

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/health` | Health check |
| `GET` | `/api/v1/time` | Current UTC server time |
| `POST` | `/api/v1/measurements` | Submit a measurement |
| `GET` | `/api/v1/measurements/latest` | Recent measurements (no auth) |
| `GET` | `/api/v1/devices/{id}/config` | Get device configuration |
| `PATCH` | `/api/v1/devices/{id}/config` | Update device configuration |
| `GET` | `/api/v1/devices/{id}/firmware` | Check for a firmware update |
| `POST` | `/api/v1/firmware/releases` | Register a firmware release |
| `GET` | `/firmware/{filename}` | Download a firmware binary |
| `POST` | `/api/v1/devices/{id}/commands` | Queue a remote command |
| `GET` | `/api/v1/devices/{id}/commands/next` | Claim next pending command |
| `POST` | `/api/v1/devices/{id}/commands/{cmd_id}/result` | Report command result |

### App endpoints (HivePal integration)

All require `X-HivePal-Service-Key` and `X-User-Id` headers.

| Method | Endpoint | Description |
|---|---|---|
| `POST` | `/api/v1/app/devices/claim` | Claim a device by claim code |
| `GET` | `/api/v1/app/devices` | List devices for the current user |
| `DELETE` | `/api/v1/app/devices/{id}` | Remove a device from the current user |
| `GET` | `/api/v1/app/devices/{id}/config` | Get device config |
| `PATCH` | `/api/v1/app/devices/{id}/config` | Update device config |
| `GET` | `/api/v1/app/devices/{id}/channels` | List channel names |
| `PATCH` | `/api/v1/app/devices/{id}/channels` | Update channel display names |
| `GET` | `/api/v1/app/devices/{id}/measurements` | Measurements (with date-range filter) |
| `GET` | `/api/v1/app/devices/{id}/measurements/latest` | Latest measurements |
| `GET` | `/api/v1/app/devices/{id}/members` | List device members |
| `POST` | `/api/v1/app/devices/{id}/members` | Share device with another user |
| `DELETE` | `/api/v1/app/devices/{id}/members/{user_id}` | Revoke a member's access |

---

## Claim Code Pairing

HiveScale uses a claim-code model to pair devices with users without manual database entries.

1. Set `CLAIM_CODE` in `secrets.h` before flashing (e.g. `ABCD-1234`).
2. The firmware includes the claim code in every measurement payload until the device is claimed.
3. The server stores the hashed claim code and creates an unclaimed device entry on the first measurement.
4. A user claiming the device in HivePal (or via `POST /api/v1/app/devices/claim`) matches the hash, marks the device as claimed, and becomes the owner.

To update the claim code via OTA (e.g. for reprovisioning), increment `CLAIM_CODE_REVISION` in `secrets.h` and flash a new build.

---

## Remote Commands

Commands are queued via the API and picked up by the device on its next cycle.

| Command type | Payload | Description |
|---|---|---|
| `tare_scale_1` | `{}` | Zero scale 1 |
| `tare_scale_2` | `{}` | Zero scale 2 |
| `calibrate_scale_1` | `{"known_weight_kg": 10.0}` | Calibrate scale 1 with a known weight |
| `calibrate_scale_2` | `{"known_weight_kg": 10.0}` | Calibrate scale 2 with a known weight |
| `reboot` | `{}` | Restart the ESP32 |
| `check_ota` / `ota_update` | `{}` | Trigger an immediate OTA check |
| `start_provisioning` | `{}` | Start the Wi-Fi provisioning AP |
| `reset_wifi` | `{}` | Clear all saved Wi-Fi credentials and reboot |
| `factory_reset` | `{}` | Clear all Preferences and reboot |

---

## OTA Firmware Updates


    "device_id": "hive_scale_dual_01",
    "scale_1_weight_kg": 42.5,
    "scale_2_weight_kg": 38.2,
    "hive_1_temp_c": 34.1,
    "hive_2_temp_c": 33.7,
    "ambient_temp_c": 18.4,
    "ambient_humidity_percent": 61.2
  }'

# Get the latest 10 measurements
curl "http://<host>:31115/api/v1/measurements/latest?limit=10"
```

See [docs/test-commands.md](docs/test-commands.md) for a complete set of test commands.

---

## License

MIT © 2026 Maximilian Nitschke