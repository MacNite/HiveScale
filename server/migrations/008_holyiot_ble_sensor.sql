-- HiveScale HolyIot 25015 in-hive BLE sensor migration
--
-- The HolyIot 25015 is a passive BLE beacon (SHT40 temp/humidity, LPS22HB
-- pressure, LIS2DH12 acceleration) bridged by the ESP32 during each upload
-- cycle. It replaces the previous wired LIS3DH/LIS2DH12 accelerometer.
--
-- Data-model decision (reuse over new accel columns):
--   * acceleration            -> existing accel_{1,2}_* columns (ok / rms_mg /
--                                peak_mg / sample_count / range_g). No FFT bands:
--                                a beacon emits periodic single-shot samples, so
--                                accel_{1,2}_band_*_mg stay NULL for BLE-sourced
--                                rows and the server runs a low-rate pre-swarm
--                                detector on accel_{1,2}_rms_mg instead.
--   * temperature             -> existing hive_{1,2}_temp_c columns (the wired
--                                DS18B20 is now optional; the BLE SHT40 is the
--                                fallback / replacement source).
--   * humidity & pressure     -> the NEW columns added below (genuinely new).
--   * raw per-axis accel,
--     battery %, link RSSI     -> raw_json only (ble_{1,2}_accel_*_mg,
--                                ble_{1,2}_battery_percent, ble_{1,2}_rssi_dbm).
--
-- Safe to run multiple times (ADD COLUMN IF NOT EXISTS). init_db() in
-- server/main.py creates the same columns, so this migration is optional for
-- fresh deployments and idempotent for existing ones.

BEGIN;

ALTER TABLE measurements ADD COLUMN IF NOT EXISTS ble_1_humidity_percent  DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS ble_1_pressure_hpa      DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS ble_2_humidity_percent  DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS ble_2_pressure_hpa      DOUBLE PRECISION;

-- Backfill any rows that already carry these fields in raw_json
UPDATE measurements
SET
    ble_1_humidity_percent = COALESCE(ble_1_humidity_percent, NULLIF(raw_json->>'ble_1_humidity_percent', '')::double precision),
    ble_1_pressure_hpa     = COALESCE(ble_1_pressure_hpa,     NULLIF(raw_json->>'ble_1_pressure_hpa',     '')::double precision),
    ble_2_humidity_percent = COALESCE(ble_2_humidity_percent, NULLIF(raw_json->>'ble_2_humidity_percent', '')::double precision),
    ble_2_pressure_hpa     = COALESCE(ble_2_pressure_hpa,     NULLIF(raw_json->>'ble_2_pressure_hpa',     '')::double precision)
WHERE raw_json IS NOT NULL
  AND (
        raw_json ? 'ble_1_humidity_percent'
     OR raw_json ? 'ble_1_pressure_hpa'
     OR raw_json ? 'ble_2_humidity_percent'
     OR raw_json ? 'ble_2_pressure_hpa'
  );

COMMIT;
