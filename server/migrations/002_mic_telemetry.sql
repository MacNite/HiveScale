-- HiveScale mic telemetry migration
-- Adds typed columns for INMP441 stereo microphone data.
-- Safe to run multiple times (uses ADD COLUMN IF NOT EXISTS).

BEGIN;

ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_ok                   BOOLEAN;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_sample_rate_hz       INTEGER;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_sample_frames        INTEGER;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_left_ok              BOOLEAN;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_left_rms_dbfs        DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_left_peak_dbfs       DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_left_rms_normalized  DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_right_ok             BOOLEAN;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_right_rms_dbfs       DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_right_peak_dbfs      DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS mic_right_rms_normalized DOUBLE PRECISION;

-- Backfill existing rows that already have mic data stored in raw_json
UPDATE measurements
SET
    mic_ok                   = COALESCE(mic_ok,                   NULLIF(raw_json->>'mic_ok',                   '')::boolean),
    mic_sample_rate_hz       = COALESCE(mic_sample_rate_hz,       NULLIF(raw_json->>'mic_sample_rate_hz',       '')::integer),
    mic_sample_frames        = COALESCE(mic_sample_frames,        NULLIF(raw_json->>'mic_sample_frames',        '')::integer),
    mic_left_ok              = COALESCE(mic_left_ok,              NULLIF(raw_json->>'mic_left_ok',              '')::boolean),
    mic_left_rms_dbfs        = COALESCE(mic_left_rms_dbfs,        NULLIF(raw_json->>'mic_left_rms_dbfs',        '')::double precision),
    mic_left_peak_dbfs       = COALESCE(mic_left_peak_dbfs,       NULLIF(raw_json->>'mic_left_peak_dbfs',       '')::double precision),
    mic_left_rms_normalized  = COALESCE(mic_left_rms_normalized,  NULLIF(raw_json->>'mic_left_rms_normalized',  '')::double precision),
    mic_right_ok             = COALESCE(mic_right_ok,             NULLIF(raw_json->>'mic_right_ok',             '')::boolean),
    mic_right_rms_dbfs       = COALESCE(mic_right_rms_dbfs,       NULLIF(raw_json->>'mic_right_rms_dbfs',       '')::double precision),
    mic_right_peak_dbfs      = COALESCE(mic_right_peak_dbfs,      NULLIF(raw_json->>'mic_right_peak_dbfs',      '')::double precision),
    mic_right_rms_normalized = COALESCE(mic_right_rms_normalized, NULLIF(raw_json->>'mic_right_rms_normalized', '')::double precision)
WHERE raw_json IS NOT NULL
  AND (
        raw_json ? 'mic_ok'
     OR raw_json ? 'mic_left_rms_dbfs'
     OR raw_json ? 'mic_right_rms_dbfs'
  );

COMMIT;
