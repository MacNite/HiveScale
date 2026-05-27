-- HiveScale mic FFT band energy migration
-- Adds per-channel frequency band energy columns (dBFS) for the 5 hive-relevant bands.
-- Safe to run multiple times (ADD COLUMN IF NOT EXISTS).
-- Requires 002_mic_telemetry.sql to have been applied first.

BEGIN;

-- Left channel bands
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_left_band_sub_bass_dbfs  DOUBLE PRECISION; --  50-150 Hz
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_left_band_hum_dbfs       DOUBLE PRECISION; -- 150-300 Hz colony hum
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_left_band_piping_dbfs    DOUBLE PRECISION; -- 300-550 Hz piping/tooting
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_left_band_stress_dbfs    DOUBLE PRECISION; -- 550-1500 Hz agitation
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_left_band_high_dbfs      DOUBLE PRECISION; -- 1500-3000 Hz

-- Right channel bands
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_right_band_sub_bass_dbfs DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_right_band_hum_dbfs      DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_right_band_piping_dbfs   DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_right_band_stress_dbfs   DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_right_band_high_dbfs     DOUBLE PRECISION;

-- Backfill any rows already carrying these fields in raw_json
UPDATE measurements
SET
    mic_left_band_sub_bass_dbfs  = COALESCE(mic_left_band_sub_bass_dbfs,  NULLIF(raw_json->>'mic_left_band_sub_bass_dbfs',  '')::double precision),
    mic_left_band_hum_dbfs       = COALESCE(mic_left_band_hum_dbfs,       NULLIF(raw_json->>'mic_left_band_hum_dbfs',       '')::double precision),
    mic_left_band_piping_dbfs    = COALESCE(mic_left_band_piping_dbfs,    NULLIF(raw_json->>'mic_left_band_piping_dbfs',    '')::double precision),
    mic_left_band_stress_dbfs    = COALESCE(mic_left_band_stress_dbfs,    NULLIF(raw_json->>'mic_left_band_stress_dbfs',    '')::double precision),
    mic_left_band_high_dbfs      = COALESCE(mic_left_band_high_dbfs,      NULLIF(raw_json->>'mic_left_band_high_dbfs',      '')::double precision),
    mic_right_band_sub_bass_dbfs = COALESCE(mic_right_band_sub_bass_dbfs, NULLIF(raw_json->>'mic_right_band_sub_bass_dbfs', '')::double precision),
    mic_right_band_hum_dbfs      = COALESCE(mic_right_band_hum_dbfs,      NULLIF(raw_json->>'mic_right_band_hum_dbfs',      '')::double precision),
    mic_right_band_piping_dbfs   = COALESCE(mic_right_band_piping_dbfs,   NULLIF(raw_json->>'mic_right_band_piping_dbfs',   '')::double precision),
    mic_right_band_stress_dbfs   = COALESCE(mic_right_band_stress_dbfs,   NULLIF(raw_json->>'mic_right_band_stress_dbfs',   '')::double precision),
    mic_right_band_high_dbfs     = COALESCE(mic_right_band_high_dbfs,     NULLIF(raw_json->>'mic_right_band_high_dbfs',     '')::double precision)
WHERE raw_json IS NOT NULL
  AND (
        raw_json ? 'mic_left_band_piping_dbfs'
     OR raw_json ? 'mic_right_band_piping_dbfs'
  );

COMMIT;
