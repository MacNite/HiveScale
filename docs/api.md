# HiveScale API Reference

The HiveScale backend exposes a REST API built with [FastAPI](https://fastapi.tiangolo.com/) and served by Uvicorn. Interactive Swagger documentation is automatically available at `http://<host>:31115/docs`.

---

## Base URL

```
http://<host>:31115
```

Replace `<host>` with the IP address or domain name of your server. The default port is `31115`.

---

## Authentication

The API uses two separate API key schemes depending on the caller.

### Device key (`X-API-Key`)

Used by the ESP32 firmware. Set via the `API_KEY` environment variable on the server and the `API_KEY` define in `secrets.h` on the device.

```
X-API-Key: your-api-key
```

### HivePal service key (`X-HivePal-Service-Key` + `X-User-Id`)

Used by the HivePal backend (or any other third-party app) to act on behalf of an end user. Both headers are required on every app endpoint.

```
X-HivePal-Service-Key: your-hivepal-service-key
X-User-Id: user-123
```

`X-User-Id` is the ID of the end user performing the action. HivePal forwards the authenticated user ID here.

---

## General endpoints

### `GET /health`

Health check. No authentication required.

**Response `200 OK`:**
```json
{ "status": "ok" }
```

---

### `GET /api/v1/time`

Returns the current UTC server time. Useful for the firmware to sync the RTC without a separate NTP call.

**Auth:** `X-API-Key`

**Response `200 OK`:**
```json
{
  "timestamp": "2026-05-01T12:00:00+00:00",
  "unix": 1746100800,
  "timezone": "UTC"
}
```

---

## Measurements

### `POST /api/v1/measurements`

Submit a measurement from the device. On the first measurement from a new `device_id`, the server automatically creates a device record and a default configuration entry. If a `claim_code` is provided, it is hashed and stored so the device can later be claimed by a user.

**Auth:** `X-API-Key`

**Request body:**

| Field | Type | Required | Description |
|---|---|---|---|
| `device_id` | string | ✅ | Unique device identifier |
| `claim_code` | string | — | Pairing code (sent until the device is claimed) |
| `timestamp` | ISO 8601 datetime | — | Measurement time; defaults to server receive time |
| `scale_1_weight_kg` | float | — | Weight on scale 1 in kg |
| `scale_2_weight_kg` | float | — | Weight on scale 2 in kg |
| `hive_1_temp_c` | float | — | Internal temperature of hive 1 in °C |
| `hive_2_temp_c` | float | — | Internal temperature of hive 2 in °C |
| `ambient_temp_c` | float | — | Ambient temperature in °C |
| `ambient_humidity_percent` | float | — | Ambient relative humidity in % |
| `battery_voltage` | float | — | Battery or supply voltage in V |
| `rssi_dbm` | int | — | Wi-Fi signal strength in dBm |
| `firmware_version` | string | — | Currently running firmware version |
| `config_version` | int | — | Config version the device is running |
| `sd_ok` | bool | — | SD card status |
| `rtc_ok` | bool | — | RTC status |
| `sht_ok` | bool | — | SHT4x sensor status |
| `scale_1_raw` | int | — | Raw HX711 reading for scale 1 |
| `scale_2_raw` | int | — | Raw HX711 reading for scale 2 |

All fields except `device_id` are optional. The full payload is also stored as JSONB for future use.

**Example:**
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
  "battery_voltage": 3.85,
  "rssi_dbm": -65,
  "firmware_version": "0.4.1",
  "sd_ok": true,
  "rtc_ok": true,
  "sht_ok": true,
  "scale_1_raw": -298450,
  "scale_2_raw": -271900,
  "config_version": 3
}
```

**Response `200 OK`:**
```json
{
  "status": "ok",
  "id": 1042,
  "measured_at": "2026-05-01T12:00:00+00:00"
}
```

---

### `GET /api/v1/measurements/latest`

Returns the most recent measurements across all devices, sorted newest-first. No authentication required — suitable for a simple public dashboard.

**Query parameters:**

| Parameter | Default | Max | Description |
|---|---|---|---|
| `limit` | 50 | 500 | Number of measurements to return |

**Response `200 OK`:** Array of measurement objects (same fields as stored, without `raw_json`).

---

## Device configuration

### `GET /api/v1/devices/{device_id}/config`

Returns the current configuration for a device. A default configuration is created automatically if none exists yet.

**Auth:** `X-API-Key`

**Response `200 OK`:**
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

| Field | Description |
|---|---|
| `send_interval_seconds` | How often the device should measure and send (seconds) |
| `scale1_offset` | Raw tare offset for scale 1 |
| `scale1_factor` | Raw-to-kg factor for scale 1 |
| `scale2_offset` | Raw tare offset for scale 2 |
| `scale2_factor` | Raw-to-kg factor for scale 2 |
| `config_version` | Incremented on every update; the device tracks this to detect config changes |

---

### `PATCH /api/v1/devices/{device_id}/config`

Updates one or more configuration fields. Only the fields included in the request body are modified; `config_version` is automatically incremented.

**Auth:** `X-API-Key`

**Request body** (all fields optional):
```json
{
  "send_interval_seconds": 300,
  "scale1_factor": -7200.0
}
```

**Response `200 OK`:** Updated `DeviceConfig` object (same schema as above).

---

## Firmware OTA

### `GET /api/v1/devices/{device_id}/firmware`

Checks whether a newer firmware release is available for the device.

**Auth:** `X-API-Key`

**Query parameters:**

| Parameter | Description |
|---|---|
| `version` | Current firmware version running on the device (e.g. `0.4.1`) |

**Response `200 OK` — no update available:**
```json
{ "update": false }
```

**Response `200 OK` — update available:**
```json
{
  "update": true,
  "version": "0.5.0",
  "url": "https://your-domain.example.com/firmware/hivescale-0.5.0.bin"
}
```

---

### `POST /api/v1/firmware/releases`

Registers a firmware release. The binary must already be present in `FIRMWARE_DIR` on the server before calling this endpoint.

**Auth:** `X-API-Key`

**Request body:**
```json
{
  "version": "0.5.0",
  "filename": "hivescale-0.5.0.bin",
  "active": true
}
```

Setting `active: false` registers the release without making it available to devices yet. If a release with the same version already exists, it is updated (upsert).

**Response `200 OK`:**
```json
{ "status": "ok", "id": 7 }
```

---

### `GET /firmware/{filename}`

Serves a firmware binary file for download. Used by the device during OTA.

**Auth:** None (URL is only known from the firmware check response).

---

## Remote commands

Commands are queued on the server and picked up by the device on its next measurement cycle. A device can have multiple commands queued; they are processed one at a time in chronological order.

### `POST /api/v1/devices/{device_id}/commands`

Queue a command for the device.

**Auth:** `X-API-Key`

**Request body:**
```json
{
  "command_type": "tare_scale_1",
  "payload": {}
}
```

Available command types:

| `command_type` | `payload` | Description |
|---|---|---|
| `tare_scale_1` | `{}` | Zero scale 1 at current weight |
| `tare_scale_2` | `{}` | Zero scale 2 at current weight |
| `calibrate_scale_1` | `{"known_weight_kg": 10.0}` | Set calibration factor for scale 1 using a known weight |
| `calibrate_scale_2` | `{"known_weight_kg": 10.0}` | Set calibration factor for scale 2 using a known weight |
| `reboot` | `{}` | Restart the ESP32 |
| `check_ota` / `ota_update` | `{}` | Trigger an immediate OTA firmware check |
| `start_provisioning` | `{}` | Start the Wi-Fi provisioning access point |
| `reset_wifi` | `{}` | Clear all saved Wi-Fi credentials and reboot |
| `factory_reset` | `{}` | Clear all Preferences and reboot |

**Response `200 OK`:**
```json
{ "status": "queued", "id": 55 }
```

**Example — calibrate scale 2 with 20 kg:**
```bash
curl -X POST https://your-domain.example.com/api/v1/devices/hive_scale_dual_01/commands \
  -H "X-API-Key: YOUR_KEY" \
  -H "Content-Type: application/json" \
  -d '{"command_type": "calibrate_scale_2", "payload": {"known_weight_kg": 20.0}}'
```

---

### `GET /api/v1/devices/{device_id}/commands/next`

Claims the next pending command for the device. The command status is atomically moved from `pending` to `claimed` so it is not returned again. The device calls this after every successful measurement upload.

**Auth:** `X-API-Key`

**Response `200 OK` — no pending command:**
```json
{ "command": false }
```

**Response `200 OK` — command returned:**
```json
{
  "command": true,
  "id": 55,
  "command_type": "tare_scale_1",
  "payload": {}
}
```

---

### `POST /api/v1/devices/{device_id}/commands/{command_id}/result`

Reports the outcome of a command. For calibration commands, if the result includes updated scale offset/factor values the server automatically applies them to `device_configs`.

**Auth:** `X-API-Key`

**Request body:**
```json
{
  "success": true,
  "message": "Tare applied",
  "result": {
    "scale1_offset": -124800
  }
}
```

**Response `200 OK`:**
```json
{ "status": "ok" }
```

---

## App endpoints (HivePal integration)

All app endpoints require both `X-HivePal-Service-Key` and `X-User-Id` headers. Role-based access control is enforced per device:

| Role | Permissions |
|---|---|
| `owner` | Full access including member management and device removal |
| `admin` | Read + write config and channels; cannot manage members or delete |
| `viewer` | Read-only access to measurements, config, channels, and members |

---

### `POST /api/v1/app/devices/claim`

Claims an unclaimed device on behalf of a user. The device must have already sent at least one measurement containing the claim code. The claiming user becomes the `owner`.

**Auth:** `X-HivePal-Service-Key` + `X-User-Id`

**Request body:**
```json
{
  "claim_code": "ABCD-1234",
  "display_name": "Back garden hive",
  "scale_1_display_name": "Hive A",
  "scale_2_display_name": "Hive B"
}
```

`display_name`, `scale_1_display_name`, and `scale_2_display_name` are optional.

**Response `200 OK`:**
```json
{
  "status": "claimed",
  "device_id": "hive_scale_dual_01",
  "role": "owner",
  "channels": [
    { "channel_number": 1, "name": "Hive A" },
    { "channel_number": 2, "name": "Hive B" }
  ]
}
```

**Error `404`:** No unclaimed device found for this claim code.

---

### `GET /api/v1/app/devices`

Lists all devices the current user has access to (any role).

**Auth:** `X-HivePal-Service-Key` + `X-User-Id`

**Response `200 OK`:** Array of device objects including `device_id`, `display_name`, `claimed_at`, `last_seen_at`, `last_firmware_version`, `role`, and `channels`.

---

### `DELETE /api/v1/app/devices/{device_id}`

Removes the current user's membership from a device. If this was the last member, the device is reset to unclaimed so it can be re-paired.

**Auth:** `X-HivePal-Service-Key` + `X-User-Id` (any role)

**Response `200 OK`:**
```json
{
  "status": "removed",
  "device_id": "hive_scale_dual_01",
  "claimable": false
}
```

`claimable: true` means the device has no remaining members and can be claimed again.

---

### `GET /api/v1/app/devices/{device_id}/channels`

Returns the display names for both scale channels.

**Auth:** `X-HivePal-Service-Key` + `X-User-Id` (any role)

**Response `200 OK`:**
```json
{
  "device_id": "hive_scale_dual_01",
  "channels": [
    { "channel_number": 1, "name": "Hive A" },
    { "channel_number": 2, "name": "Hive B" }
  ]
}
```

---

### `PATCH /api/v1/app/devices/{device_id}/channels`

Updates one or both channel display names.

**Auth:** `X-HivePal-Service-Key` + `X-User-Id` (owner or admin)

**Request body:**
```json
{
  "scale_1_display_name": "Buckfast colony",
  "scale_2_display_name": "Carnica colony"
}
```

Both fields are optional. Omit a field to leave that channel name unchanged.

---

### `GET /api/v1/app/devices/{device_id}/config`

Returns the device configuration. Same schema as the device-facing config endpoint.

**Auth:** `X-HivePal-Service-Key` + `X-User-Id` (any role)

---

### `PATCH /api/v1/app/devices/{device_id}/config`

Updates device configuration fields. Same behaviour as the device-facing PATCH, but requires owner or admin role.

**Auth:** `X-HivePal-Service-Key` + `X-User-Id` (owner or admin)

---

### `GET /api/v1/app/devices/{device_id}/measurements`

Returns measurements for a specific device, optionally filtered by date range.

**Auth:** `X-HivePal-Service-Key` + `X-User-Id` (any role)

**Query parameters:**

| Parameter | Default | Max | Description |
|---|---|---|---|
| `limit` | 200 | 10 000 | Number of results |
| `start_at` | — | — | ISO 8601 datetime — only return measurements at or after this time |
| `end_at` | — | — | ISO 8601 datetime — only return measurements at or before this time |

Results are sorted newest-first.

---

### `GET /api/v1/app/devices/{device_id}/measurements/latest`

Returns the most recent measurements for a specific device.

**Auth:** `X-HivePal-Service-Key` + `X-User-Id` (any role)

**Query parameters:**

| Parameter | Default | Max | Description |
|---|---|---|---|
| `limit` | 50 | 500 | Number of results |

---

### `GET /api/v1/app/devices/{device_id}/members`

Lists all members with access to the device, including their role and who invited them.

**Auth:** `X-HivePal-Service-Key` + `X-User-Id` (any role)

**Response `200 OK`:**
```json
[
  {
    "user_id": "user-001",
    "role": "owner",
    "invited_by": null,
    "created_at": "2026-04-01T10:00:00+00:00"
  },
  {
    "user_id": "user-002",
    "role": "viewer",
    "invited_by": "user-001",
    "created_at": "2026-04-15T08:30:00+00:00"
  }
]
```

---

### `POST /api/v1/app/devices/{device_id}/members`

Shares the device with another user. If the user already has access, their role is updated.

**Auth:** `X-HivePal-Service-Key` + `X-User-Id` (owner only)

**Request body:**
```json
{
  "user_id": "user-002",
  "role": "viewer"
}
```

`role` must be `"admin"` or `"viewer"`. Owners cannot be added via this endpoint.

**Response `200 OK`:**
```json
{
  "status": "shared",
  "device_id": "hive_scale_dual_01",
  "user_id": "user-002",
  "role": "viewer"
}
```

---

### `DELETE /api/v1/app/devices/{device_id}/members/{user_id}`

Revokes another user's access to the device. Cannot be used to remove an owner or yourself.

**Auth:** `X-HivePal-Service-Key` + `X-User-Id` (owner only)

**Response `200 OK`:**
```json
{
  "status": "revoked",
  "device_id": "hive_scale_dual_01",
  "user_id": "user-002"
}
```

---

## Database schema

The schema is auto-created on startup. The following tables are used:

| Table | Description |
|---|---|
| `devices` | One row per device: ID, claim code hash, claimed status, display name, last seen |
| `device_members` | Many-to-many: users ↔ devices with roles (`owner`, `admin`, `viewer`) |
| `device_channels` | Display names for channel 1 and channel 2 per device |
| `device_configs` | Calibration and sampling configuration per device |
| `measurements` | All measurement records; raw payload also stored as JSONB |
| `firmware_releases` | Registered firmware versions available for OTA |
| `device_commands` | Command queue with status (`pending`, `claimed`, `done`, `failed`) |

---

## Error responses

FastAPI returns standard HTTP error responses as JSON:

```json
{ "detail": "No unclaimed device found for this claim code" }
```

| Status | Meaning |
|---|---|
| `400` | Bad request (e.g. invalid payload, file not found for firmware release) |
| `401` | Missing or invalid API key / service key / user ID |
| `403` | Insufficient role for the requested operation |
| `404` | Resource not found |
| `500` | Server misconfiguration (e.g. `HIVEPAL_SERVICE_API_KEY` not set) |