-- HiveScale off-grid telemetry migration
-- Adds typed columns for optional MAX17048, INA219 and SIM7080G telemetry.
-- Safe to run multiple times.

BEGIN;

ALTER TABLE measurements ADD COLUMN IF NOT EXISTS battery_soc_percent DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS battery_alert BOOLEAN;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS battery_monitor_ok BOOLEAN;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS solar_monitor_ok BOOLEAN;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS solar_bus_voltage_v DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS solar_shunt_voltage_mv DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS solar_load_voltage_v DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS solar_current_ma DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS solar_power_mw DOUBLE PRECISION;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS network_transport TEXT;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS cellular_ok BOOLEAN;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS cellular_csq INTEGER;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS calibration_mode BOOLEAN;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS boot_count BIGINT;
ALTER TABLE measurements ADD COLUMN IF NOT EXISTS time_source TEXT;

UPDATE measurements
SET
  battery_soc_percent = COALESCE(battery_soc_percent, NULLIF(raw_json->>'battery_soc_percent', '')::double precision),
  battery_alert = COALESCE(battery_alert, NULLIF(raw_json->>'battery_alert', '')::boolean),
  battery_monitor_ok = COALESCE(battery_monitor_ok, NULLIF(raw_json->>'battery_monitor_ok', '')::boolean),
  solar_monitor_ok = COALESCE(solar_monitor_ok, NULLIF(raw_json->>'solar_monitor_ok', '')::boolean),
  solar_bus_voltage_v = COALESCE(solar_bus_voltage_v, NULLIF(raw_json->>'solar_bus_voltage_v', '')::double precision),
  solar_shunt_voltage_mv = COALESCE(solar_shunt_voltage_mv, NULLIF(raw_json->>'solar_shunt_voltage_mv', '')::double precision),
  solar_load_voltage_v = COALESCE(solar_load_voltage_v, NULLIF(raw_json->>'solar_load_voltage_v', '')::double precision),
  solar_current_ma = COALESCE(solar_current_ma, NULLIF(raw_json->>'solar_current_ma', '')::double precision),
  solar_power_mw = COALESCE(solar_power_mw, NULLIF(raw_json->>'solar_power_mw', '')::double precision),
  network_transport = COALESCE(network_transport, raw_json->>'network_transport'),
  cellular_ok = COALESCE(cellular_ok, NULLIF(raw_json->>'cellular_ok', '')::boolean),
  cellular_csq = COALESCE(cellular_csq, NULLIF(raw_json->>'cellular_csq', '')::integer),
  calibration_mode = COALESCE(calibration_mode, NULLIF(raw_json->>'calibration_mode', '')::boolean),
  boot_count = COALESCE(boot_count, NULLIF(raw_json->>'boot_count', '')::bigint),
  time_source = COALESCE(time_source, raw_json->>'time_source')
WHERE raw_json IS NOT NULL
  AND (
    raw_json ? 'battery_soc_percent'
    OR raw_json ? 'battery_alert'
    OR raw_json ? 'battery_monitor_ok'
    OR raw_json ? 'solar_monitor_ok'
    OR raw_json ? 'solar_bus_voltage_v'
    OR raw_json ? 'solar_shunt_voltage_mv'
    OR raw_json ? 'solar_load_voltage_v'
    OR raw_json ? 'solar_current_ma'
    OR raw_json ? 'solar_power_mw'
    OR raw_json ? 'network_transport'
    OR raw_json ? 'cellular_ok'
    OR raw_json ? 'cellular_csq'
    OR raw_json ? 'calibration_mode'
    OR raw_json ? 'boot_count'
    OR raw_json ? 'time_source'
  );

COMMIT;
