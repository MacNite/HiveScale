-- HiveScale accelerometer (per-hive vibration) migration
-- Adds typed columns for the LIS3DH / LIS2DH12 low-frequency vibration data,
-- one accelerometer per hive (slot 1 -> hive 1 @ 0x18, slot 2 -> hive 2 @ 0x19).
--
-- All band/RMS values are AC (gravity removed), in milli-g (mg). The swarm band
-- (8–30 Hz) carries the ~20 Hz pre-swarm vibration the microphones cannot reach
-- (Ramsey et al. 2020; Uthoff et al. 2023). See firmware/include/accel.h and
-- docs/accelerometer.md.
--
-- Safe to run multiple times (ADD COLUMN IF NOT EXISTS). init_db() in
-- server/main.py creates the same columns, so this migration is optional for
-- fresh deployments and idempotent for existing ones.

BEGIN;

-- Hive 1 accelerometer
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS accel_1_ok                BOOLEAN;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS accel_1_sample_rate_hz    INTEGER;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS accel_1_sample_count      INTEGER;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS accel_1_range_g           INTEGER;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS accel_1_rms_mg            DOUBLE PRECISION; -- broadband AC RMS
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS accel_1_peak_mg           DOUBLE PRECISION; -- peak |a-mean|
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS accel_1_band_swarm_mg     DOUBLE PRECISION; --   8-30 Hz  pre-swarm
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS accel_1_band_fanning_mg   DOUBLE PRECISION; --  30-100 Hz fanning
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS accel_1_band_activity_mg  DOUBLE PRECISION; -- 100-200 Hz activity

-- Hive 2 accelerometer
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS accel_2_ok                BOOLEAN;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS accel_2_sample_rate_hz    INTEGER;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS accel_2_sample_count      INTEGER;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS accel_2_range_g           INTEGER;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS accel_2_rms_mg            DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS accel_2_peak_mg           DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS accel_2_band_swarm_mg     DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS accel_2_band_fanning_mg   DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS accel_2_band_activity_mg  DOUBLE PRECISION;

-- Backfill any rows that already carry these fields in raw_json
UPDATE measurements
SET
    accel_1_ok                = COALESCE(accel_1_ok,                NULLIF(raw_json->>'accel_1_ok',                '')::boolean),
    accel_1_sample_rate_hz    = COALESCE(accel_1_sample_rate_hz,    NULLIF(raw_json->>'accel_1_sample_rate_hz',    '')::integer),
    accel_1_sample_count      = COALESCE(accel_1_sample_count,      NULLIF(raw_json->>'accel_1_sample_count',      '')::integer),
    accel_1_range_g           = COALESCE(accel_1_range_g,           NULLIF(raw_json->>'accel_1_range_g',           '')::integer),
    accel_1_rms_mg            = COALESCE(accel_1_rms_mg,            NULLIF(raw_json->>'accel_1_rms_mg',            '')::double precision),
    accel_1_peak_mg           = COALESCE(accel_1_peak_mg,           NULLIF(raw_json->>'accel_1_peak_mg',           '')::double precision),
    accel_1_band_swarm_mg     = COALESCE(accel_1_band_swarm_mg,     NULLIF(raw_json->>'accel_1_band_swarm_mg',     '')::double precision),
    accel_1_band_fanning_mg   = COALESCE(accel_1_band_fanning_mg,   NULLIF(raw_json->>'accel_1_band_fanning_mg',   '')::double precision),
    accel_1_band_activity_mg  = COALESCE(accel_1_band_activity_mg,  NULLIF(raw_json->>'accel_1_band_activity_mg',  '')::double precision),
    accel_2_ok                = COALESCE(accel_2_ok,                NULLIF(raw_json->>'accel_2_ok',                '')::boolean),
    accel_2_sample_rate_hz    = COALESCE(accel_2_sample_rate_hz,    NULLIF(raw_json->>'accel_2_sample_rate_hz',    '')::integer),
    accel_2_sample_count      = COALESCE(accel_2_sample_count,      NULLIF(raw_json->>'accel_2_sample_count',      '')::integer),
    accel_2_range_g           = COALESCE(accel_2_range_g,           NULLIF(raw_json->>'accel_2_range_g',           '')::integer),
    accel_2_rms_mg            = COALESCE(accel_2_rms_mg,            NULLIF(raw_json->>'accel_2_rms_mg',            '')::double precision),
    accel_2_peak_mg           = COALESCE(accel_2_peak_mg,           NULLIF(raw_json->>'accel_2_peak_mg',           '')::double precision),
    accel_2_band_swarm_mg     = COALESCE(accel_2_band_swarm_mg,     NULLIF(raw_json->>'accel_2_band_swarm_mg',     '')::double precision),
    accel_2_band_fanning_mg   = COALESCE(accel_2_band_fanning_mg,   NULLIF(raw_json->>'accel_2_band_fanning_mg',   '')::double precision),
    accel_2_band_activity_mg  = COALESCE(accel_2_band_activity_mg,  NULLIF(raw_json->>'accel_2_band_activity_mg',  '')::double precision)
WHERE raw_json IS NOT NULL
  AND (
        raw_json ? 'accel_1_ok'
     OR raw_json ? 'accel_2_ok'
     OR raw_json ? 'accel_1_band_swarm_mg'
     OR raw_json ? 'accel_2_band_swarm_mg'
  );

COMMIT;
