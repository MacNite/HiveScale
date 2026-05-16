# HiveScale

**ESP32-based dual beehive scale system** for monitoring the weight, temperature, humidity, power state, and network state of two beehives. Measurements are sent to a self-hosted FastAPI backend backed by PostgreSQL and can be displayed in HivePal.

**Notice: The V0 PCB and system design will soon be split into:
 - Power Module (with battery and / or solar power and MODEM, connected to other componentens via i2c or ESPnow)
 - Scale Module including the most sensors (connected to other componentens via i2c or ESPnow)
 - beecounter (connected to other componentens via i2c or ESPnow)

---

## Features

- **Dual load cells** using two HX711 amplifiers for two independent hive scales.
- **Per-hive temperature** using DS18B20 probes on a shared 1-Wire bus.
- **Ambient temperature and humidity** using an SHT4x sensor on I2C.
- **RTC timekeeping** using a DS3231 so the device can timestamp measurements without depending on NTP.
- **SD card cache and backup** for local buffering when uploads fail and for persistent measurement backup.
- **Claim-code pairing** so devices can be claimed from HivePal without manual database setup.
- **Remote configuration** for sampling interval, tare offsets, calibration factors, and config versioning.
- **Remote commands** for tare, calibration, OTA checks, provisioning, reboot, Wi-Fi reset, and factory reset.
- **OTA firmware updates** with server-side release registration.
- **Wi-Fi provisioning portal** opened by the setup button for field configuration.
- **Multi-network Wi-Fi** with up to three saved networks.
- **Optional off-grid mode** with SIM7080G LTE/NB-IoT transport, INA219 solar telemetry, and MAX17048 LiPo telemetry.
- **LTE modem power control** through configurable PWRKEY and power-enable pins, including hardware reset and sleep shutdown handling.
- **HivePal integration** through dedicated `/api/v1/app/...` endpoints using a HivePal service key and per-user access roles.
- **Breakout PCB design** in KiCad, including fabrication outputs and PCB-specific TODOs.
- **Docker Compose deployment** for the API and PostgreSQL database.

---

## Repository structure

```text
HiveScale/
├── firmware/                   # ESP32 PlatformIO project
│   ├── src/main.cpp            # Main firmware source
│   ├── include/secrets.example.h
│   ├── partitions_4mb_ota_no_fs.csv
│   └── platformio.ini
├── server/                     # Python FastAPI backend
│   ├── main.py
│   ├── migrations/001_offgrid_telemetry.sql
│   ├── requirements.txt
│   └── Dockerfile
├── docker/                     # Docker Compose deployment
│   ├── docker-compose.yml
│   └── .env.example
├── docs/                       # Hardware, API, deployment, and test docs
│   ├── api.md
│   ├── offgrid-firmware-notes.md
│   ├── test-commands.md
│   ├── truenas-install.md
│   ├── docker-install.md
│   └── wiring.md
├── pcb-design/                 # KiCad breakout PCB design and fabrication files
│   ├── README.md
│   ├── todo-list.md
│   ├── HiveScale_V0.kicad_sch
│   ├── HiveScale_V0.kicad_pcb
│   └── fabrication/
└── .github/workflows/          # CI: builds and pushes the backend image
```

---

## Hardware

### Core components

| Component | Role |
|---|---|
| ESP32 Dev Board | Main controller |
| 2x HX711 + load cells | Weight measurement for scale 1 and scale 2 |
| 2x DS18B20 waterproof probes | Internal hive temperature probes |
| SHT4x | Ambient temperature and humidity |
| DS3231 RTC | Offline timekeeping |
| MicroSD card module + card | Local cache and backup storage |
| Momentary pushbutton | Provisioning and factory reset |
| 5 V power supply / DC-DC converter | ESP32 and peripheral supply |
| IP-rated enclosure, glands, wiring, frame hardware | Outdoor installation |

### Optional off-grid components

| Component | Firmware flag | Role |
|---|---|---|
| SIM7080G LTE/NB-IoT modem | `ENABLE_SIM7080G` | Cellular upload, config polling, command polling, and cellular time sync |
| INA219 | `ENABLE_INA219_SOLAR` | Solar/load voltage, shunt voltage, current, and power telemetry |
| MAX17048 | `ENABLE_MAX17048_BATTERY` | LiPo voltage, state-of-charge, and low-battery alert |
| CN3971 / solar charger module | Hardware only | Solar charging path used by the breakout PCB design |
| TPS63020 buck-boost module | Hardware only | Stable 3.3 V rail for low-power/off-grid builds |

### Firmware pin mapping

The current firmware pin mapping is defined in `firmware/src/main.cpp`.

| Signal | GPIO | Notes |
|---|---:|---|
| HX711 #1 DOUT | 16 | Scale 1 data |
| HX711 #1 SCK | 17 | Scale 1 clock; held high during deep sleep to power down HX711 |
| HX711 #2 DOUT | 32 | Scale 2 data |
| HX711 #2 SCK | 33 | Scale 2 clock; held high during deep sleep to power down HX711 |
| DS18B20 1-Wire data | 4 | Shared bus for both hive temperature probes; use 4.7 kOhm pull-up to 3.3 V |
| I2C SDA | 21 | RTC, SHT4x, optional INA219, optional MAX17048 |
| I2C SCL | 22 | RTC, SHT4x, optional INA219, optional MAX17048 |
| SD CS | 5 | SD card chip select |
| SD SCK | 18 | SD card SPI clock |
| SD MISO | 23 | Updated mapping |
| SD MOSI | 19 | Updated mapping |
| Setup button | 27 | `INPUT_PULLUP`; short press opens provisioning AP, long press factory resets Preferences |
| SIM7080G RX | 26 | ESP32 RX connected to modem TX; configurable with `SIM7080G_RX_PIN` |
| SIM7080G TX | 25 | ESP32 TX connected to modem RX; configurable with `SIM7080G_TX_PIN` |
| SIM7080G PWRKEY / control | Optional, PCB exposes GPIO14 | Set `SIM7080G_PWRKEY_PIN` or `SIM7080G_POWER_EN_PIN` in `secrets.h` if wired |

> See [docs/wiring.md](docs/wiring.md) for detailed wiring and [pcb-design/README.md](pcb-design/README.md) for the KiCad breakout PCB pinout.

---

## Firmware setup

### Prerequisites

- PlatformIO, either the VS Code extension or the CLI.

### Configuration

Copy the local configuration template:

```bash
cp firmware/include/secrets.example.h firmware/include/secrets.h
```

Edit `firmware/include/secrets.h`:

```cpp
#define DEVICE_ID           "hive-001"
#define API_KEY             "your-api-key-here"
#define CLAIM_CODE          "ABCD-1234"
#define CLAIM_CODE_REVISION 1
#define API_BASE_URL        "https://your-backend-domain.com"

#define WIFI1_SSID          "your-wifi-ssid-1"
#define WIFI1_PASS          "your-wifi-password-1"
#define WIFI2_SSID          "your-wifi-ssid-2"
#define WIFI2_PASS          "your-wifi-password-2"
#define WIFI3_SSID          "your-wifi-ssid-3"
#define WIFI3_PASS          "your-wifi-password-3"
```

Values in `secrets.h` are used to seed the device's persistent `Preferences` storage on first boot. Later changes are usually made through the backend or provisioning portal. Set `FORCE_RESEED true` only when you intentionally want to overwrite stored preferences from the build file.

### Optional off-grid configuration

Off-grid modules are disabled by default and enabled per build:

```cpp
#define ENABLE_INA219_SOLAR      1
#define ENABLE_MAX17048_BATTERY  1
#define ENABLE_SIM7080G          1
#define CELLULAR_OTA_ENABLED     0

#define SIM7080G_APN             "your-apn"
#define SIM7080G_USER            ""
#define SIM7080G_PASS            ""
#define SIM7080G_PIN             ""
#define SIM7080G_RX_PIN          26
#define SIM7080G_TX_PIN          25

// Use one of these when your modem board exposes a usable control pin.
#define SIM7080G_PWRKEY_PIN      14
#define SIM7080G_POWER_EN_PIN    -1
#define SIM7080G_POWER_EN_ACTIVE_HIGH 1
```

When `ENABLE_SIM7080G` is enabled, normal measurement upload, config polling, command polling, and time sync use cellular transport. Wi-Fi station mode is skipped during normal operation to save power, but the Wi-Fi provisioning AP remains available through the setup button.

OTA over cellular is disabled by default because firmware binaries are large compared with normal measurement payloads. Enable it only for SIM plans and antennas that can handle the traffic reliably.

### Flash

```bash
cd firmware
pio run --target upload
pio device monitor   # 115200 baud
```

### PlatformIO dependencies

`platformio.ini` installs the required libraries automatically:

- `bogde/HX711`
- `paulstoffregen/OneWire`
- `milesburton/DallasTemperature`
- `adafruit/Adafruit SHT4x Library`
- `adafruit/RTClib`
- `bblanchon/ArduinoJson`
- `adafruit/Adafruit INA219`
- `sparkfun/SparkFun MAX1704x Fuel Gauge Arduino Library`
- `vshymanskyy/TinyGSM`
- `arduino-libraries/ArduinoHttpClient`

---

## Wi-Fi provisioning portal

Press the setup button on GPIO27 to manage field configuration without reflashing.

| Action | Result |
|---|---|
| Short press | Starts `HiveScale-Setup-XXXX` AP; open `http://192.168.4.1` |
| Long press, 10 seconds | Clears stored Preferences and reboots |

The portal can edit Wi-Fi networks, backend URL, device ID, claim code, and API settings. It closes automatically after 10 minutes.

---

## Server setup

### Docker Compose

```bash
cd docker
cp .env.example .env
# edit API_KEY, HIVEPAL_SERVICE_API_KEY, database password, and volume paths
docker compose up -d
```

The API listens on port `31115` by default.

| Setting | Default |
|---|---|
| API image | `ghcr.io/macnite/hivescale-api:latest` |
| API port | `31115` |
| Database image | `postgres:16-alpine` |
| Database name/user | `hivescale` |

Change `API_KEY`, `HIVEPAL_SERVICE_API_KEY`, and the PostgreSQL password before exposing the service.

### Manual / local

```bash
cd server
pip install -r requirements.txt
DATABASE_URL="postgresql://hivescale:password@localhost:5432/hivescale" \
API_KEY="your-secret-key" \
HIVEPAL_SERVICE_API_KEY="your-hivepal-service-key" \
PUBLIC_BASE_URL="https://your-domain.example.com" \
uvicorn main:app --host 0.0.0.0 --port 8000
```

### Environment variables

| Variable | Required | Description |
|---|---|---|
| `DATABASE_URL` | Yes | PostgreSQL connection string |
| `API_KEY` | Yes | Device API key used by ESP32 firmware in `X-API-Key` |
| `HIVEPAL_SERVICE_API_KEY` | Yes, for HivePal | Service key used by HivePal in `X-HivePal-Service-Key` |
| `PUBLIC_BASE_URL` | Recommended | Public base URL used for OTA firmware download links |
| `FIRMWARE_DIR` | Optional | Firmware binary directory, default `/app/firmware` |
| `TZ` | Optional | Server timezone, for example `Europe/Berlin` |

The backend auto-creates tables and runs idempotent `ALTER TABLE` statements for off-grid telemetry columns on startup. The SQL migration in `server/migrations/001_offgrid_telemetry.sql` can also be used manually on older deployments.

---

## API overview

Interactive Swagger docs are available at `http://<host>:31115/docs`. See [docs/api.md](docs/api.md) for the full endpoint reference and schemas.

### Device-facing endpoints

Device endpoints require the `X-API-Key` header unless noted otherwise.

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/health` | Health check |
| `GET` | `/api/v1/time` | UTC server time for RTC sync |
| `POST` | `/api/v1/measurements` | Submit a measurement, including optional off-grid telemetry |
| `GET` | `/api/v1/measurements/latest` | Latest measurements for dashboards |
| `GET` | `/api/v1/devices/{id}/config` | Get device configuration |
| `PATCH` | `/api/v1/devices/{id}/config` | Update device configuration |
| `GET` | `/api/v1/devices/{id}/firmware` | Check for a firmware update |
| `POST` | `/api/v1/firmware/releases` | Register a firmware release |
| `GET` | `/firmware/{filename}` | Download a firmware binary |
| `POST` | `/api/v1/devices/{id}/commands` | Queue a remote command |
| `GET` | `/api/v1/devices/{id}/commands/next` | Claim next pending command |
| `POST` | `/api/v1/devices/{id}/commands/{cmd_id}/result` | Report command result |

### App endpoints for HivePal

App endpoints require both `X-HivePal-Service-Key` and `X-User-Id`.

| Method | Endpoint | Description |
|---|---|---|
| `POST` | `/api/v1/app/devices/claim` | Claim a device by claim code |
| `GET` | `/api/v1/app/devices` | List devices for the current user |
| `DELETE` | `/api/v1/app/devices/{id}` | Remove the current user's membership |
| `GET` | `/api/v1/app/devices/{id}/config` | Get device config |
| `PATCH` | `/api/v1/app/devices/{id}/config` | Update device config |
| `GET` | `/api/v1/app/devices/{id}/channels` | List channel names |
| `PATCH` | `/api/v1/app/devices/{id}/channels` | Update scale display names |
| `GET` | `/api/v1/app/devices/{id}/measurements` | Measurements with date range filter |
| `GET` | `/api/v1/app/devices/{id}/measurements/latest` | Latest measurements |
| `GET` | `/api/v1/app/devices/{id}/members` | List device members |
| `POST` | `/api/v1/app/devices/{id}/members` | Share a device with another user |
| `DELETE` | `/api/v1/app/devices/{id}/members/{user_id}` | Revoke a member's access |

---

## Measurement payload highlights

Core payload fields include weights, hive temperatures, ambient readings, raw HX711 values, firmware version, config version, sensor status, boot count, and time source.

Off-grid builds can also send:

- `network_transport`, `cellular_ok`, `cellular_csq`
- `solar_monitor_ok`, `solar_bus_voltage_v`, `solar_shunt_voltage_mv`, `solar_load_voltage_v`, `solar_current_ma`, `solar_power_mw`
- `battery_monitor_ok`, `battery_voltage_v`, `battery_soc_percent`, `battery_alert`
- `calibration_mode`, `boot_count`, `time_source`

These fields are stored in dedicated PostgreSQL columns and returned through the latest-measurements and HivePal app APIs.

---

## Claim-code pairing

1. Set `CLAIM_CODE` in `secrets.h` before flashing, for example `ABCD-1234`.
2. The firmware includes the claim code in every measurement until the device is claimed.
3. The backend stores a hash of the claim code and creates an unclaimed device record on the first measurement.
4. HivePal, or another app client, calls `POST /api/v1/app/devices/claim` with the code.
5. The matched device is assigned to the user as `owner`.

To push a new claim code through OTA, change `CLAIM_CODE`, increment `CLAIM_CODE_REVISION`, and publish a new firmware build.

---

## Remote commands

Commands are queued by the server and picked up by the device on its next cycle.

| Command type | Payload | Description |
|---|---|---|
| `tare_scale_1` | `{}` | Zero scale 1 |
| `tare_scale_2` | `{}` | Zero scale 2 |
| `calibrate_scale_1` | `{"known_weight_kg": 10.0}` | Calibrate scale 1 with a known weight |
| `calibrate_scale_2` | `{"known_weight_kg": 10.0}` | Calibrate scale 2 with a known weight |
| `start_calibration_mode` | `{"interval_seconds": 5, "timeout_seconds": 600}` | Temporarily use fast measurement cycles for calibration |
| `stop_calibration_mode` | `{}` | Return to normal interval and deep sleep behavior |
| `reboot` | `{}` | Restart the ESP32 |
| `check_ota` / `ota_update` | `{}` | Trigger an immediate OTA check |
| `start_provisioning` | `{}` | Start the Wi-Fi provisioning AP |
| `reset_wifi` | `{}` | Clear saved Wi-Fi credentials and reboot |
| `factory_reset` | `{}` | Clear all Preferences and reboot |

---

## PCB design

The `pcb-design/` directory contains the first KiCad breakout PCB for the HiveScale hardware. It breaks out the ESP32, HX711 modules, load cell terminals, I2C sensors, SD module, off-grid solar/battery modules, and SIM7080G connector.

Start with:

- [pcb-design/README.md](pcb-design/README.md) for connector pinout, design intent, fabrication files, and assembly notes.
- [pcb-design/todo-list.md](pcb-design/todo-list.md) for prototype and layout tasks.

The current PCB is a first revision and should be prototyped before field deployment.

---

## Useful docs

- [docs/wiring.md](docs/wiring.md) — full wiring reference.
- [docs/offgrid-firmware-notes.md](docs/offgrid-firmware-notes.md) — SIM7080G, solar, and LiPo firmware behavior.
- [docs/api.md](docs/api.md) — complete API reference.
- [docs/test-commands.md](docs/test-commands.md) — curl commands for testing the backend.
- [docs/docker-install.md](docs/docker-install.md) — generic Docker setup.
- [docs/truenas-install.md](docs/truenas-install.md) — TrueNAS Scale setup.

---

## License

MIT © 2026 Maximilian Nitschke
