-- 029_hive_recordings.sql — on-request audio from a hive's HiveInside node.
--
-- A recording is a row plus a file. The row is created the moment somebody asks
-- for audio, long before any bytes exist: a HiveHub deep-sleeps and only picks
-- commands up on its next wake, so "requested" is a real state a listener has
-- to be shown rather than a transient the API can hide. The lifecycle is
--
--   requested -> streaming -> ready
--                          \-> failed
--
-- and every terminal state carries a reason, so a recording that produced
-- nothing can say why instead of just being absent.
--
-- The audio itself lives on disk under RECORDINGS_DIR, not in the database:
-- one minute of 16 kHz PCM16 is about 1.9 MB, and a bytea column that size
-- turns every listing query into a table scan over audio. The row carries the
-- filename; `bytes` is what actually landed.
--
-- Quality fields are kept because a HiveInside session can legitimately produce
-- an INCOMPLETE recording and still be worth listening to:
--
--   dropped_bytes  audio the node's own ring lost before it was ever sent
--   gaps           discontinuities the hub saw (sequence jumps, ring overruns)
--   crc32/device_crc32  end-to-end check; a mismatch means transit corruption
--   clipped_pct    samples that hit the rail — the gain was too high
--
-- A player that ignored these would splice across a hole and present the result
-- as continuous audio, which is exactly the kind of quiet lie that makes an
-- acoustic diagnosis wrong.
--
-- There is deliberately NO retention sweep. Recordings are kept until somebody
-- deletes them; see docs/audio-recording.md for the disk-space consequences.
--
-- init_db() in server/db.py creates the same objects idempotently; this is the
-- standalone migration for an already-running database.

CREATE TABLE IF NOT EXISTS hive_recordings (
    id BIGSERIAL PRIMARY KEY,
    device_id TEXT NOT NULL REFERENCES devices(device_id) ON DELETE CASCADE,
    hive_index INTEGER NOT NULL,
    command_id BIGINT,
    status TEXT NOT NULL DEFAULT 'requested',
    requested_by TEXT,
    requested_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    started_at TIMESTAMPTZ,
    completed_at TIMESTAMPTZ,
    duration_ds INTEGER NOT NULL DEFAULT 0,
    gain_db INTEGER NOT NULL DEFAULT 0,
    sample_rate INTEGER NOT NULL DEFAULT 16000,
    bytes BIGINT NOT NULL DEFAULT 0,
    crc32 BIGINT,
    device_bytes BIGINT,
    device_crc32 BIGINT,
    dropped_bytes BIGINT NOT NULL DEFAULT 0,
    gaps INTEGER NOT NULL DEFAULT 0,
    clipped_pct INTEGER NOT NULL DEFAULT 0,
    device_error INTEGER,
    filename TEXT,
    error TEXT
);

-- The dashboard lists a device's recordings newest first, optionally filtered
-- to one hive; the live player polls the newest row for a device.
CREATE INDEX IF NOT EXISTS hive_recordings_device_idx
    ON hive_recordings (device_id, requested_at DESC);
CREATE INDEX IF NOT EXISTS hive_recordings_hive_idx
    ON hive_recordings (device_id, hive_index, requested_at DESC);
