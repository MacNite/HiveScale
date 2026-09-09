-- 030_recording_ring_overruns.sql — separate the two kinds of seam.
--
-- `gaps` used to be a sum. Hub firmware up to 0.30.1 sent
-- `stats.gaps + stats.ringOverruns` in one field, so a recording with holes in
-- it could not say whether the audio was lost on the air (or inside the node)
-- or whether the hub's own staging ring refused it because the TLS upload was
-- behind. Those have opposite fixes — one is a radio problem, the other is
-- ours — and telling them apart needed a serial cable.
--
-- From firmware 0.30.2 the hub reports them separately:
--   gaps                sequence discontinuities on the air, plus the node's
--                       own FLAG_GAP packets
--   ring_overruns       notifications the hub's staging ring could not take
--   ring_dropped_bytes  how much PCM those refusals cost
--
-- Older hubs keep sending only `gaps`, and land in the same column they always
-- did — the sum, attributed to the air. The new columns default to 0, so an
-- existing row keeps exactly the meaning it was written with.
ALTER TABLE hive_recordings
    ADD COLUMN IF NOT EXISTS ring_overruns INTEGER NOT NULL DEFAULT 0;

ALTER TABLE hive_recordings
    ADD COLUMN IF NOT EXISTS ring_dropped_bytes BIGINT NOT NULL DEFAULT 0;
