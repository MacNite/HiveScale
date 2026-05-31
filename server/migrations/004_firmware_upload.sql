-- 004_firmware_upload.sql
--
-- Supports uploading firmware binaries directly from HivePal via
--   POST /api/v1/app/devices/{device_id}/firmware  (multipart, owner/admin)
--
-- The endpoint writes the binary into FIRMWARE_DIR and upserts a
-- firmware_releases row. No new columns are strictly required beyond what the
-- existing schema already provides, but this migration makes the required
-- shape explicit and idempotent for older deployments that predate the
-- `target` / `crc32` columns (these are also created by init_db() at startup).

-- Firmware type the release applies to: 'hivescale' (the ESP32 itself) or
-- 'beecounter' (relayed to the entrance counter over I2C).
ALTER TABLE firmware_releases
    ADD COLUMN IF NOT EXISTS target TEXT NOT NULL DEFAULT 'hivescale';

-- CRC-32 (IEEE 802.3) of the image, stored as an unsigned value in a BIGINT so
-- the HiveScale can verify a download before flashing/relaying it.
ALTER TABLE firmware_releases
    ADD COLUMN IF NOT EXISTS crc32 BIGINT;

-- The firmware-check endpoint selects the most recent active release per
-- target, so an index on (target, active) keeps that lookup cheap.
CREATE INDEX IF NOT EXISTS idx_firmware_releases_target_active
    ON firmware_releases (target, active);
