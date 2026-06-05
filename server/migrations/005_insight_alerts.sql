-- HiveScale insight alert history migration
-- Persists the lifecycle of sensor-based insight alerts (server/insights.py)
-- so HivePal can show a *history* of alerts. Insights are otherwise recomputed
-- live on every request and never stored.
--
-- One row per distinct alert occurrence: while a detector keeps firing the same
-- row is updated (last_seen_at bumped); when it stops firing the row is resolved
-- (resolved_at set). A later recurrence of the same detector creates a new row.
-- The partial unique index guarantees at most one *active* row per detector.
--
-- Safe to run multiple times. init_db() in server/main.py creates the same
-- objects with IF NOT EXISTS, so applying this migration is optional for fresh
-- deployments and idempotent for existing ones.

BEGIN;

CREATE TABLE IF NOT EXISTS insight_alerts (
    id BIGSERIAL PRIMARY KEY,
    device_id TEXT NOT NULL REFERENCES devices(device_id) ON DELETE CASCADE,
    alert_key TEXT NOT NULL,
    category TEXT NOT NULL,
    channel INTEGER NOT NULL,
    severity TEXT NOT NULL,
    peak_severity TEXT NOT NULL,
    title TEXT NOT NULL,
    description TEXT NOT NULL,
    confidence DOUBLE PRECISION NOT NULL DEFAULT 0,
    evidence JSONB NOT NULL DEFAULT '{}'::jsonb,
    source TEXT NOT NULL DEFAULT '',
    window_start TIMESTAMPTZ,
    window_end TIMESTAMPTZ,
    first_seen_at TIMESTAMPTZ NOT NULL,
    last_seen_at TIMESTAMPTZ NOT NULL,
    resolved_at TIMESTAMPTZ,
    update_count INTEGER NOT NULL DEFAULT 1,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE UNIQUE INDEX IF NOT EXISTS insight_alerts_active_uniq
    ON insight_alerts (device_id, alert_key)
    WHERE resolved_at IS NULL;

CREATE INDEX IF NOT EXISTS insight_alerts_device_first_seen_idx
    ON insight_alerts (device_id, first_seen_at DESC);

COMMIT;
