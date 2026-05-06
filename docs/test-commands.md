# HiveScale — Test Commands

A collection of `curl` commands for verifying and interacting with the HiveScale API. Replace the placeholders below before use:

| Placeholder | Replace with |
|---|---|
| `HOST` | IP address or domain name of your server (e.g. `192.168.1.100` or `hivescale.example.com`) |
| `YOUR_API_KEY` | Value of `API_KEY` in your `.env` file |
| `YOUR_HIVEPAL_KEY` | Value of `HIVEPAL_SERVICE_API_KEY` in your `.env` file |
| `DEVICE_ID` | Your device ID (e.g. `hive_scale_dual_01`) |

---

## Health & connectivity

**Check the API is running:**
```bash
curl http://HOST:31115/health
```

Expected response:
```json
{ "status": "ok" }
```

**Get current server time:**
```bash
curl http://HOST:31115/api/v1/time \
  -H "X-API-Key: YOUR_API_KEY"
```

---

## Measurements

**Submit a test measurement:**
```bash
curl -X POST http://HOST:31115/api/v1/measurements \
  -H "Content-Type: application/json" \
  -H "X-API-Key: YOUR_API_KEY" \
  -d '{
    "device_id": "DEVICE_ID",
    "scale_1_weight_kg": 42.5,
    "scale_2_weight_kg": 38.2,
    "hive_1_temp_c": 34.1,
    "hive_2_temp_c": 33.7,
    "ambient_temp_c": 18.4,
    "ambient_humidity_percent": 61.2,
    "battery_voltage": 3.85,
    "rssi_dbm": -65,
    "firmware_version": "0.4.1"
  }'
```

**Submit a measurement with a claim code** (simulates first boot of an unregistered device):
```bash
curl -X POST http://HOST:31115/api/v1/measurements \
  -H "Content-Type: application/json" \
  -H "X-API-Key: YOUR_API_KEY" \
  -d '{
    "device_id": "DEVICE_ID",
    "claim_code": "ABCD-1234",
    "scale_1_weight_kg": 42.5,
    "scale_2_weight_kg": 38.2
  }'
```

**Get the latest measurements (no auth required):**
```bash
# Default: last 50
curl "http://HOST:31115/api/v1/measurements/latest"

# Custom limit
curl "http://HOST:31115/api/v1/measurements/latest?limit=10"
```

---

## Device configuration

**Get device configuration:**
```bash
curl http://HOST:31115/api/v1/devices/DEVICE_ID/config \
  -H "X-API-Key: YOUR_API_KEY"
```

**Update measurement interval to 5 minutes:**
```bash
curl -X PATCH http://HOST:31115/api/v1/devices/DEVICE_ID/config \
  -H "Content-Type: application/json" \
  -H "X-API-Key: YOUR_API_KEY" \
  -d '{"send_interval_seconds": 300}'
```

**Update scale calibration factors:**
```bash
curl -X PATCH http://HOST:31115/api/v1/devices/DEVICE_ID/config \
  -H "Content-Type: application/json" \
  -H "X-API-Key: YOUR_API_KEY" \
  -d '{
    "scale1_factor": -7200.0,
    "scale2_factor": -7100.0
  }'
```

---

## Remote commands

**Tare scale 1 (zero at current weight):**
```bash
curl -X POST http://HOST:31115/api/v1/devices/DEVICE_ID/commands \
  -H "Content-Type: application/json" \
  -H "X-API-Key: YOUR_API_KEY" \
  -d '{"command_type": "tare_scale_1", "payload": {}}'
```

**Tare scale 2:**
```bash
curl -X POST http://HOST:31115/api/v1/devices/DEVICE_ID/commands \
  -H "Content-Type: application/json" \
  -H "X-API-Key: YOUR_API_KEY" \
  -d '{"command_type": "tare_scale_2", "payload": {}}'
```

**Calibrate scale 1 with a 10 kg reference weight:**
```bash
curl -X POST http://HOST:31115/api/v1/devices/DEVICE_ID/commands \
  -H "Content-Type: application/json" \
  -H "X-API-Key: YOUR_API_KEY" \
  -d '{"command_type": "calibrate_scale_1", "payload": {"known_weight_kg": 10.0}}'
```

**Calibrate scale 2 with a 20 kg reference weight:**
```bash
curl -X POST http://HOST:31115/api/v1/devices/DEVICE_ID/commands \
  -H "Content-Type: application/json" \
  -H "X-API-Key: YOUR_API_KEY" \
  -d '{"command_type": "calibrate_scale_2", "payload": {"known_weight_kg": 20.0}}'
```

**Reboot the device:**
```bash
curl -X POST http://HOST:31115/api/v1/devices/DEVICE_ID/commands \
  -H "Content-Type: application/json" \
  -H "X-API-Key: YOUR_API_KEY" \
  -d '{"command_type": "reboot", "payload": {}}'
```

**Trigger an immediate OTA check:**
```bash
curl -X POST http://HOST:31115/api/v1/devices/DEVICE_ID/commands \
  -H "Content-Type: application/json" \
  -H "X-API-Key: YOUR_API_KEY" \
  -d '{"command_type": "ota_update", "payload": {}}'
```

**Start the Wi-Fi provisioning portal:**
```bash
curl -X POST http://HOST:31115/api/v1/devices/DEVICE_ID/commands \
  -H "Content-Type: application/json" \
  -H "X-API-Key: YOUR_API_KEY" \
  -d '{"command_type": "start_provisioning", "payload": {}}'
```

**Factory reset the device:**
```bash
curl -X POST http://HOST:31115/api/v1/devices/DEVICE_ID/commands \
  -H "Content-Type: application/json" \
  -H "X-API-Key: YOUR_API_KEY" \
  -d '{"command_type": "factory_reset", "payload": {}}'
```

**Check for pending commands** (simulates what the device does each cycle):
```bash
curl http://HOST:31115/api/v1/devices/DEVICE_ID/commands/next \
  -H "X-API-Key: YOUR_API_KEY"
```

---

## OTA firmware

**Register a new firmware release** (binary must already be in `FIRMWARE_DIR`):
```bash
curl -X POST http://HOST:31115/api/v1/firmware/releases \
  -H "Content-Type: application/json" \
  -H "X-API-Key: YOUR_API_KEY" \
  -d '{"version": "0.5.0", "filename": "hivescale-0.5.0.bin", "active": true}'
```

**Check if a firmware update is available for a device:**
```bash
curl "http://HOST:31115/api/v1/devices/DEVICE_ID/firmware?version=0.4.1" \
  -H "X-API-Key: YOUR_API_KEY"
```

---

## App / HivePal endpoints

**Claim a device** (after the device has sent at least one measurement with the claim code):
```bash
curl -X POST http://HOST:31115/api/v1/app/devices/claim \
  -H "Content-Type: application/json" \
  -H "X-HivePal-Service-Key: YOUR_HIVEPAL_KEY" \
  -H "X-User-Id: user-001" \
  -d '{
    "claim_code": "ABCD-1234",
    "display_name": "Back garden hive",
    "scale_1_display_name": "Hive A",
    "scale_2_display_name": "Hive B"
  }'
```

**List devices for a user:**
```bash
curl http://HOST:31115/api/v1/app/devices \
  -H "X-HivePal-Service-Key: YOUR_HIVEPAL_KEY" \
  -H "X-User-Id: user-001"
```

**Get measurements for a specific device (last 100):**
```bash
curl "http://HOST:31115/api/v1/app/devices/DEVICE_ID/measurements?limit=100" \
  -H "X-HivePal-Service-Key: YOUR_HIVEPAL_KEY" \
  -H "X-User-Id: user-001"
```

**Get measurements in a date range:**
```bash
curl "http://HOST:31115/api/v1/app/devices/DEVICE_ID/measurements?start_at=2026-05-01T00:00:00Z&end_at=2026-05-07T00:00:00Z" \
  -H "X-HivePal-Service-Key: YOUR_HIVEPAL_KEY" \
  -H "X-User-Id: user-001"
```

**Share a device with another user:**
```bash
curl -X POST http://HOST:31115/api/v1/app/devices/DEVICE_ID/members \
  -H "Content-Type: application/json" \
  -H "X-HivePal-Service-Key: YOUR_HIVEPAL_KEY" \
  -H "X-User-Id: user-001" \
  -d '{"user_id": "user-002", "role": "viewer"}'
```

**Revoke a user's access:**
```bash
curl -X DELETE http://HOST:31115/api/v1/app/devices/DEVICE_ID/members/user-002 \
  -H "X-HivePal-Service-Key: YOUR_HIVEPAL_KEY" \
  -H "X-User-Id: user-001"
```

**Update channel display names:**
```bash
curl -X PATCH http://HOST:31115/api/v1/app/devices/DEVICE_ID/channels \
  -H "Content-Type: application/json" \
  -H "X-HivePal-Service-Key: YOUR_HIVEPAL_KEY" \
  -H "X-User-Id: user-001" \
  -d '{"scale_1_display_name": "Buckfast colony", "scale_2_display_name": "Carnica colony"}'
```

---

## Tips

- The interactive Swagger UI at `http://HOST:31115/docs` lets you execute all endpoints in the browser and inspect request/response schemas.
- If you need pretty-printed JSON output from curl, pipe through `python3 -m json.tool` or `jq .`:
  ```bash
  curl http://HOST:31115/api/v1/measurements/latest | jq .
  ```
- Commands are picked up by the device only on its next measurement cycle. If `send_interval_seconds` is 600, you may need to wait up to 10 minutes after queueing a command.