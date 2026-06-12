"""
Behavioral tests for the low-rate (HolyIot 25015 BLE) pre-swarm rule in
insights.py.

Run: python3 test_ble_sensor_rules.py
Builds synthetic measurement dicts (same shape as measurement_row_to_dict) and
asserts the BLE accelerometer detector fires on a rising night-time per-cycle
vibration magnitude (accel_N_rms_mg), defers to the FFT-band detector when real
band data exists, and degrades cleanly when the sensor is absent or flat.
"""

import os
import sys
from datetime import datetime, timedelta, timezone

# Prefer the authoritative server/ copy of insights.py (same loader as the other
# test modules in this directory).
_HERE = os.path.dirname(os.path.abspath(__file__))
for _candidate in (
    os.path.join(_HERE, "..", "server"),
    os.path.join(_HERE, "mock-server"),
    _HERE,
):
    if os.path.exists(os.path.join(_candidate, "insights.py")):
        sys.path.insert(0, _candidate)
        break

import insights


# Active season, midday so "recent" (last 2 days) and "baseline" night windows
# both have room behind them.
NOW = datetime(2024, 6, 15, 12, 0, tzinfo=timezone.utc)


def check(name, cond):
    status = "PASS" if cond else "FAIL"
    print(f"  [{status}] {name}")
    if not cond:
        raise AssertionError(name)


def night_row(days_ago, rms_mg, channel=1, ok=True):
    ts = (NOW - timedelta(days=days_ago)).replace(hour=2, minute=0)
    return {
        "measured_at": ts,
        f"accel_{channel}_ok": ok,
        f"accel_{channel}_rms_mg": rms_mg,
    }


def rising_rows(channel=1, recent_mg=12.0, baseline_mg=5.0):
    rows = []
    for d in (1.8, 1.4, 1.0, 0.4):           # recent (< 2 days)
        rows.append(night_row(d, recent_mg, channel))
    for d in (2.4, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0):  # baseline (2..10 days)
        rows.append(night_row(d, baseline_mg, channel))
    return rows


def main():
    print("\n=== 1. Rising night-time BLE vibration fires a pre-swarm watch ===")
    rows = rising_rows()
    series = insights._accel_rms_series(rows, 1)
    recent, baseline, ratio = insights._lowrate_vibration_rise(series, NOW)
    check("ratio computed above the trigger", ratio is not None and ratio >= insights.LOWRATE_SWARM_RISE_MULT)
    alert = insights.detect_lowrate_accel_swarm(series, 1, NOW, accel_swarm_series=[])
    check("alert fires", alert is not None)
    check("alert id is the BLE variant", alert.id == "swarm-ble-vibration-ch1")
    check("evidence flags the BLE sensor", alert.evidence.get("source_sensor") == "ble_holyiot_25015")

    print("\n=== 2. Defers to the FFT-band detector when band data exists ===")
    deferred = insights.detect_lowrate_accel_swarm(
        series, 1, NOW, accel_swarm_series=[(NOW, 1.0)]
    )
    check("no duplicate alert when bands present", deferred is None)

    print("\n=== 3. Flat vibration does not fire ===")
    flat = insights._accel_rms_series(rising_rows(recent_mg=5.0, baseline_mg=5.0), 1)
    check("no alert on flat series", insights.detect_lowrate_accel_swarm(flat, 1, NOW, []) is None)

    print("\n=== 4. Noise floor: tiny absolute levels don't fire on ratio alone ===")
    tiny = insights._accel_rms_series(rising_rows(recent_mg=1.0, baseline_mg=0.3), 1)
    check("no alert below the absolute floor", insights.detect_lowrate_accel_swarm(tiny, 1, NOW, []) is None)

    print("\n=== 5. Not-ok sensor is ignored (no implicit zeros) ===")
    notok = [night_row(d, 12.0, ok=False) for d in (1.8, 1.4, 1.0, 0.4)]
    notok += [night_row(d, 5.0, ok=False) for d in (2.4, 3.0, 4.0, 5.0, 6.0, 7.0)]
    check("ok=false rows excluded", len(insights._accel_rms_series(notok, 1)) == 0)

    print("\n=== 6. Off-season: suppressed ===")
    off = datetime(2024, 1, 15, 12, 0, tzinfo=timezone.utc)
    off_rows = [
        {**r, "measured_at": r["measured_at"].replace(year=2024, month=1)} for r in rows
    ]
    check(
        "no BLE swarm watch outside the active season",
        insights.detect_lowrate_accel_swarm(insights._accel_rms_series(off_rows, 1), 1, off, []) is None,
    )

    print("\n=== 7. End-to-end via compute_insights (channel 2) ===")
    rows2 = rising_rows(channel=2)
    alerts = insights.compute_insights(rows2, now=NOW)
    check("swarm-ble-vibration-ch2 present end-to-end", any(a.id == "swarm-ble-vibration-ch2" for a in alerts))

    print("\nAll HolyIot BLE-sensor rule tests passed.")


if __name__ == "__main__":
    main()
