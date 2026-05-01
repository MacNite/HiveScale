# HiveScale

**ESP32-based dual beehive scale system** — monitors the weight, temperature, and humidity of two beehives simultaneously and sends measurements to a self-hosted API backed by PostgreSQL.

---

## Features

- **Dual load cells** (HX711) for weighing two hives independently
- **Per-hive temperature** via DS18B20 (Dallas 1-Wire) sensors
- **Ambient temperature & humidity** via Adafruit SHT4x
- **RTC (DS3231)** for accurate timestamping without NTP
- **SD card cache** — measurements are buffered locally when Wi-Fi is unavailable and uploaded automatically on reconnect
- **Remote configuration** — sampling interval, scale offsets, and calibration factors are pulled from the server on each cycle
- **Remote commands** — tare, calibrate, and reboot the device over the API
- **OTA firmware updates** — the device checks for a newer firmware version on each cycle and updates itself automatically
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
│   └── test-commands.md
└── .github/workflows/      # CI: builds & pushes Docker image to GHCR
```

---

## Hardware

| Component | Role |
|---|---|
| ESP32 Dev Board | Microcontroller |
| 2× HX711 + load cells | Weight measurement (scale 1 & 2) |
| 2× DS18B20 | Per-hive internal temperature |
| Adafruit SHT4x | Ambient temperature & humidity |
| DS3231 RTC | Hardware real-time clock |
| MicroSD card module | Local measurement cache |

### Pin Mapping

| Signal | GPIO |
|---|---|
| HX711 #1 DOUT | 16 |
| HX711 #1 SCK | 17 |
| HX711 #2 DOUT | 32 |
| HX711 #2 SCK | 33 |
| DS18B20 (1-Wire) | 4 |
| I²C SDA (RTC, SHT4x) | 21 |
| I²C SCL | 22 |
| SD CS | 5 |
| SD SCK | 18 |
| SD MISO | 19 |
| SD MOSI | 23 |

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
#define WIFI_SSID     "YOUR_WIFI"
#define WIFI_PASS     "YOUR_WIFI_PASSWORD"
#define API_BASE_URL  "https://your-domain.example.com"
#define API_KEY       "CHANGE_ME_SECRET"
#define DEVICE_ID     "hive_scale_dual_01"
```

### Flash

```bash
cd firmware
pio run --target upload
pio device monitor   # 115200 baud
```

### PlatformIO Dependencies

The following libraries are installed automatically:

- `bogde/HX711` ^0.7.5
- `paulstoffregen/OneWire` ^2.3.8
- `milesburton/DallasTemperature` ^4.0.6
- `adafruit/Adafruit SHT4x Library` ^1.0.5
- `adafruit/RTClib` ^2.1.4
- `bblanchon/ArduinoJson` ^7.2.2

---

## Server Setup

### Docker Compose (recommended)

```bash
cd docker
cp .env.example .env          # edit API_KEY, passwords, volume path
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

> **Important:** Change `API_KEY` and the PostgreSQL password before exposing the service to a network.

The database schema (tables and indexes) is created automatically on first startup.

### Manual / Local

```bash
cd server
pip install -r requirements.txt
DATABASE_URL="postgresql://hivescale:password@localhost:5432/hivescale" \
API_KEY="your-secret-key" \
uvicorn main:app --host 0.0.0.0 --port 8000
```

---

## API Overview

All device-facing endpoints require the `X-API-Key` header.

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/health` | Health check |
| `POST` | `/api/v1/measurements` | Submit a measurement |
| `GET` | `/api/v1/measurements/latest` | Retrieve recent measurements |
| `GET` | `/api/v1/devices/{id}/config` | Get device configuration |
| `PATCH` | `/api/v1/devices/{id}/config` | Update device configuration |
| `GET` | `/api/v1/devices/{id}/firmware` | Check for firmware update |
| `POST` | `/api/v1/firmware/releases` | Register a firmware release |
| `GET` | `/firmware/{filename}` | Download a firmware binary |
| `POST` | `/api/v1/devices/{id}/commands` | Queue a command |
| `GET` | `/api/v1/devices/{id}/commands/next` | Claim next pending command |
| `POST` | `/api/v1/devices/{id}/commands/{cmd_id}/result` | Report command result |

Interactive API docs are available at `http://<host>:31115/docs`.

### Measurement Payload

```json
{
  "device_id": "hive_scale_dual_01",
  "timestamp": "2026-05-01T12:00:00Z",
  "scale_1_weight_kg": 42.5,
  "scale_2_weight_kg": 38.2,
  "hive_1_temp_c": 34.1,
  "hive_2_temp_c": 33.7,
  "ambient_temp_c": 18.4,
  "ambient_humidity_percent": 61.2,
  "rssi_dbm": -65,
  "firmware_version": "0.2.0"
}
```

---

## Remote Commands

Commands are queued via the API and picked up by the device on its next cycle.

| Command type | Payload | Description |
|---|---|---|
| `tare_scale_1` | — | Zero scale 1 |
| `tare_scale_2` | — | Zero scale 2 |
| `calibrate_scale_1` | `{"known_weight_kg": 10.0}` | Calibrate scale 1 with a known weight |
| `calibrate_scale_2` | `{"known_weight_kg": 10.0}` | Calibrate scale 2 with a known weight |
| `reboot` | — | Restart the ESP32 |

---

## OTA Firmware Updates

1. Place a compiled `.bin` file in the `FIRMWARE_DIR` on the server (default: `/app/firmware`).
2. Register the release via the API:

```bash
curl -X POST http://<host>:31115/api/v1/firmware/releases \
  -H "X-API-Key: <key>" \
  -H "Content-Type: application/json" \
  -d '{"version": "0.3.0", "filename": "hivescale-0.3.0.bin", "active": true}'
```

The device will detect the newer version on its next cycle, download the binary, flash it, and reboot automatically.

---

## Quick Test Commands

Check the API is running:

```bash
curl http://<host>:31115/health
```

Submit a test measurement:

```bash
curl -X POST http://<host>:31115/api/v1/measurements \
  -H "Content-Type: application/json" \
  -H "X-API-Key: CHANGE_THIS_LONG_RANDOM_API_KEY" \
  -d '{
    "device_id": "hive_scale_dual_01",
    "scale_1_weight_kg": 42.5,
    "scale_2_weight_kg": 38.2,
    "hive_1_temp_c": 34.1,
    "hive_2_temp_c": 33.7,
    "ambient_temp_c": 18.4,
    "ambient_humidity_percent": 61.2
  }'
```

---

## CI / CD

A GitHub Actions workflow (`.github/workflows/backend-image.yml`) builds and pushes the Docker image to the GitHub Container Registry (`ghcr.io/macnite/hivescale-api`) on every push to `main` that touches the `server/` directory.

---

## License

MIT © 2026 Maximilian Nitschke