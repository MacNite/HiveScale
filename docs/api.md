# HiveScale API reference

The HiveScale backend exposes a FastAPI REST API and Swagger UI at `http://<host>:31115/docs`.

---

## Base URL

```text
http://<host>:31115
```

Replace `<host>` with the server IP address or domain name.

---

## Authentication

HiveScale uses separate keys for device traffic and HivePal app traffic.

### Device key

ESP32 firmware and administrative device tooling use `X-API-Key`.

```text
X-API-Key: your-api-key
```

The value must match the server `API_KEY` environment variable and the firmware `API_KEY` define.

### HivePal service key

HivePal uses `X-HivePal-Service-Key` plus `X-User-Id` on all app endpoints.

```text
X-HivePal-Service-Key: your-hivepal-service-key
X-User-Id: hivepal-user-id
```

The service key must match HiveScale's `HIVEPAL_SERVICE_API_KEY`. HiveScale uses `X-User-Id` to enforce per-device ownership and roles.

---

## General endpoints

### `GET /health`

Health check. No authentication required.

```json
{ "status": "ok" }
```

### `GET /api/v1/time`

Returns current UTC server time for RTC sync.

**Auth:** `X-API-Key`

```json
{
  "timestamp": "2026-05-01T12:00:00+00:00",
  "unix": 1777636800,
  "timezone": "UTC"
}
```

---

## Measurements

### `POST /api/v1/measurements`

Submit a measurement from a device. On the first measurement from a new `device_id`, the backend creates a device record and a default config row. If a `claim_code` is included, it is hashed and stored so a HivePal user can claim the device.

**Auth:** `X-API-Key`

#### Request fields

| Field | Type | Required | Description |
|---|---|---:|---|
| `device_id` | string | Yes | Unique device identifier |
| `claim_code` | string | No | Pairing code sent until the device is claimed |
| `timestamp` | ISO datetime | No | Measurement time; server receive time is used if omitted |
| `scale_1_weight_kg` | number | No | Scale 1 weight in kilograms |
| `scale_2_weight_kg` | number | No | Scale 2 weight in kilograms |
| `hive_1_temp_c` | number | No | Hive 1 internal temperature |
| `hive_2_temp_c` | number | No | Hive 2 internal temperature |
| `ambient_temp_c` | number | No | Ambient temperature |
| `ambient_humidity_percent` | number | No | Ambient relative humidity |
| `battery_voltage` | number | No | Legacy battery/supply voltage field |
| `battery_voltage_v` | number | No | Battery voltage from MAX17048; preferred off-grid field |
| `battery_soc_percent` | number | No | LiPo state-of-charge percentage |
| `battery_alert` | boolean | No | MAX17048 alert state |
| `battery_monitor_ok` | boolean | No | Whether MAX17048 was detected/read successfully |
| `solar_monitor_ok` | boolean | No | Whether INA219 was detected/read successfully |
| `solar_bus_voltage_v` | number | No | INA219 bus voltage |
| `solar_shunt_voltage_mv` | number | No | INA219 shunt voltage |
| `solar_load_voltage_v` | number | No | Calculated load voltage |
| `solar_current_ma` | number | No | Solar/load current |
| `solar_power_mw` | number | No | Solar/load power |
| `network_transport` | string | No | `wifi`, `sim7080g`, or another future transport label |
| `cellular_ok` | boolean | No | SIM7080G data connection status |
| `cellular_csq` | integer | No | SIM7080G signal quality value |
| `calibration_mode` | boolean | No | Whether firmware was in calibration mode for this reading |
| `boot_count` | integer | No | ESP32 RTC boot counter |
| `time_source` | string | No | Time source such as `rtc`, `server`, `cellular`, or `compile` |
| `rssi_dbm` | integer | No | Wi-Fi RSSI or CSQ-derived approximate RSSI |
| `firmware_version` | string | No | Running firmware version |
| `config_version` | integer | No | Config version currently applied by the device |
| `sd_ok` | boolean | No | SD card status |
| `rtc_ok` | boolean | No | RTC status |
| `sht_ok` | boolean | No | SHT4x status |
| `scale_1_raw` | integer | No | Raw HX711 reading for scale 1 |
| `scale_2_raw` | integer | No | Raw HX711 reading for scale 2 |

The full payload is also stored as JSONB in `raw_json`.

#### Example Wi-Fi payload

```json
{
  "device_id": "hive_scale_dual_01",
  "claim_code": "ABCD-1234",
  "timestamp": "2026-05-01T12:00:00Z",
  "scale_1_weight_kg": 42.5,
  "scale_2_weight_kg": 38.2,
  "hive_1_temp_c": 34.1,
  "hive_2_temp_c": 33.7,
  "ambient_temp_c": 18.4,
  "ambient_humidity_percent": 61.2,
  "network_transport": "wifi",
  "rssi_dbm": -65,
  "firmware_version": "0.6.2-sim7080g-pwrkey-reset",
  "config_version": 3,
  "sd_ok": true,
  "rtc_ok": true,
  "sht_ok": true,
  "scale_1_raw": -298450,
  "scale_2_raw": -271900
}
```

#### Example off-grid payload

```json
{
  "device_id": "hive_scale_offgrid_01",
  "claim_code": "ABCD-1234",
  "timestamp": "2026-05-01T12:00:00Z",
  "scale_1_weight_kg": 42.5,
  "scale_2_weight_kg": 38.2,
  "network_transport": "sim7080g",
  "cellular_ok": true,
  "cellular_csq": 18,
  "rssi_dbm": -77,
  "battery_voltage_v": 3.94,
  "battery_soc_percent": 73.2,
  "battery_alert": false,
  "battery_monitor_ok": true,
  "solar_monitor_ok": true,
  "solar_bus_voltage_v": 5.22,
  "solar_shunt_voltage_mv": 12.4,
  "solar_load_voltage_v": 5.232,
  "solar_current_ma": 184.0,
  "solar_power_mw": 960.0,
  "calibration_mode": false,
  "boot_count": 128,
  "time_source": "cellular"
}
```

#### Response

```json
{
  "status": "ok",
  "id": 1042,
  "measured_at": "2026-05-01T12:00:00+00:00"
}
```

### `GET /api/v1/measurements/latest`

Returns recent measurements across all devices, newest-first.

**Auth:** `X-API-Key`

| Query parameter | Default | Max | Description |
|---|---:|---:|---|
| `limit` | 50 | 500 | Number of rows to return |

The response includes the core fields and the optional off-grid fields listed above.

---

## Device configuration

### `GET /api/v1/devices/{device_id}/config`

Returns the current config for a device. A default config is created if none exists.

**Auth:** `X-API-Key`

```json
{
  "device_id": "hive_scale_dual_01",
  "send_interval_seconds": 600,
  "scale1_offset": 0,
  "scale1_factor": -7050.0,
  "scale2_offset": 0,
  "scale2_factor": -7050.0,
  "config_version": 3
}
```

### `PATCH /api/v1/devices/{device_id}/config`

Updates one or more config fields and increments `config_version`.

**Auth:** `X-API-Key`

```json
{
  "send_interval_seconds": 300,
  "scale1_factor": -7200.0
}
```

---

## Firmware OTA

### `GET /api/v1/devices/{device_id}/firmware`

Checks whether a newer active firmware release is available.

**Auth:** `X-API-Key`

| Query parameter | Description |
|---|---|
| `version` | Current device firmware version |

No update:

```json
{ "update": false }
```

Update available:

```json
{
  "update": true,
  "version": "0.6.3",
  "url": "https://your-domain.example.com/firmware/hivescale-0.6.3.bin"
}
```

### `POST /api/v1/firmware/releases`

Registers or updates a firmware release. The binary must already exist in `FIRMWARE_DIR`.

**Auth:** `X-API-Key`

```json
{
  "version": "0.6.3",
  "filename": "hivescale-0.6.3.bin",
  "active": true
}
```

### `GET /firmware/{filename}`

Downloads a firmware binary. This endpoint has no API-key requirement; the URL is normally obtained from the firmware check endpoint.

---

## Remote commands

Commands are queued server-side and claimed by the device on a later cycle.

### `POST /api/v1/devices/{device_id}/commands`

**Auth:** `X-API-Key`

```json
{
  "command_type": "tare_scale_1",
  "payload": {}
}
```

| Command type | Payload | Description |
|---|---|---|
| `tare_scale_1` | `{}` | Zero scale 1 |
| `tare_scale_2` | `{}` | Zero scale 2 |
| `calibrate_scale_1` | `{"known_weight_kg": 10.0}` | Set scale 1 calibration factor using a known weight |
| `calibrate_scale_2` | `{"known_weight_kg": 10.0}` | Set scale 2 calibration factor using a known weight |
| `start_calibration_mode` | `{"interval_seconds": 5, "timeout_seconds": 600}` | Temporarily use fast cycles for calibration |
| `stop_calibration_mode` | `{}` | Return to the normal configured interval |
| `reboot` | `{}` | Restart ESP32 |
| `reset_preferences` | `{}` | Clear stored preferences and reboot |
| `factory_reset` | `{}` | Factory reset stored preferences and reboot |
| `reset_wifi` | `{}` | Clear saved Wi-Fi credentials and reboot |
| `check_ota` / `ota_update` | `{}` | Trigger immediate OTA check/update |
| `start_provisioning` | `{}` | Start provisioning AP |

Response:

```json
{ "status": "queued", "id": 55 }
```

### `GET /api/v1/devices/{device_id}/commands/next`

Claims the next pending command and marks it as claimed.

**Auth:** `X-API-Key`

No command:

```json
{ "command": false }
```

Command returned:

```json
{
  "command": true,
  "id": 55,
  "command_type": "tare_scale_1",
  "payload": {}
}
```

### `POST /api/v1/devices/{device_id}/commands/{command_id}/result`

Reports command success or failure. Calibration command results can include updated offset/factor values; the server applies them to `device_configs`.

**Auth:** `X-API-Key`

```json
{
  "success": true,
  "message": "Tare applied",
  "result": {
    "scale1_offset": -124800
  }
}
```

---

## App endpoints for HivePal

All app endpoints require both `X-HivePal-Service-Key` and `X-User-Id`.

| Role | Permissions |
|---|---|
| `owner` | Full access, including member management and removal |
| `admin` | Read plus config/channel writes |
| `viewer` | Read-only access |

### `POST /api/v1/app/devices/claim`

Claims an unclaimed device by claim code. The device must have sent at least one measurement containing that claim code.

```json
{
  "claim_code": "ABCD-1234",
  "display_name": "Back garden scale",
  "scale_1_display_name": "Hive A",
  "scale_2_display_name": "Hive B"
}
```

### `GET /api/v1/app/devices`

Lists all devices the current user can access. Device objects include `device_id`, `display_name`, `claimed_at`, `last_seen_at`, `last_firmware_version`, `role`, and `channels`.

### `DELETE /api/v1/app/devices/{device_id}`

Removes the current user's membership. If no members remain, the device becomes claimable again.

### `GET /api/v1/app/devices/{device_id}/channels`

Returns channel display names for scale 1 and scale 2.

### `PATCH /api/v1/app/devices/{device_id}/channels`

Updates channel display names. Requires `owner` or `admin`.

```json
{
  "scale_1_display_name": "Buckfast colony",
  "scale_2_display_name": "Carnica colony"
}
```

### `GET /api/v1/app/devices/{device_id}/config`

Returns the same device config schema as the device-facing config endpoint. Any role may read.

### `PATCH /api/v1/app/devices/{device_id}/config`

Updates config fields. Requires `owner` or `admin`.

### `GET /api/v1/app/devices/{device_id}/measurements`

Returns measurements for one device.

| Query parameter | Default | Max | Description |
|---|---:|---:|---|
| `limit` | 200 | 10000 | Number of rows |
| `start_at` | - | - | ISO datetime lower bound |
| `end_at` | - | - | ISO datetime upper bound |

The response includes off-grid fields when the firmware sends them.

### `GET /api/v1/app/devices/{device_id}/measurements/latest`

Returns the newest measurements for one device.

| Query parameter | Default | Max | Description |
|---|---:|---:|---|
| `limit` | 50 | 500 | Number of rows |

### `GET /api/v1/app/devices/{device_id}/members`

Lists members and roles.

### `POST /api/v1/app/devices/{device_id}/members`

Shares a device with another HivePal user ID. Requires `owner`.

```json
{
  "user_id": "user-002",
  "role": "viewer"
}
```

### `DELETE /api/v1/app/devices/{device_id}/members/{user_id}`

Revokes another user's access. Requires `owner`.

---

## Database schema

The backend auto-creates and updates the schema on startup.

| Table | Description |
|---|---|
| `devices` | Device identity, claim status, display name, last seen, firmware version |
| `device_members` | Users with `owner`, `admin`, or `viewer` role per device |
| `device_channels` | Display names for scale channel 1 and 2 |
| `device_configs` | Send interval, offsets, calibration factors, config version |
| `measurements` | Measurement records, including off-grid columns and `raw_json` |
| `firmware_releases` | Firmware versions available for OTA |
| `device_commands` | Pending, claimed, done, and failed commands |

The off-grid migration adds columns for battery telemetry, solar telemetry, cellular status, calibration mode, boot count, and time source. The same fields remain available in `raw_json` for forward compatibility.

---

## Error responses

FastAPI errors are returned as JSON:

```json
{ "detail": "No unclaimed device found for this claim code" }
```

| Status | Meaning |
|---:|---|
| 400 | Bad request or invalid command payload |
| 401 | Missing or invalid API key / service key / user ID |
| 403 | Insufficient device role |
| 404 | Resource not found |
| 500 | Server misconfiguration or unexpected backend error |
