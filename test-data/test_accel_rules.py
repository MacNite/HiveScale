"""
Behavioral tests for the accelerometer (per-hive vibration) rules in insights.py.

Run: python3 test_accel_rules.py
Builds synthetic measurement dicts (same shape as measurement_row_to_dict) and
asserts the vibration swarm-prediction detector fires on a rising night-time
8–30 Hz band, boosts the temperature pre-swarm watch, and degrades cleanly when
the accelerometer fields are absent or the sensor is not ok.
"""

import os
import sys
from datetime import datetime, timedelta, timezone

# Import the canonical insights engine whether this file is run from the repo
# root, from test-data/, or from test-data/mock-server/ (a verbatim copy). Prefer
# the authoritative server/ copy.
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


def _vib_rows(days, recent_mg, baseline_mg, *, ok=True, channel=1, hours=(1, 2, 3, 4, 22, 23)):
    """
    Build `days` of night-and-evening rows ending just before NOW. Samples in the
    VIBRATION_NIGHT_HOURS window (00–05) drive the detector; a couple of evening
    hours are included to prove the night filter ignores them.

    Rows within the last VIBRATION_RECENT_DAYS get `recent_mg`, older rows get
    `baseline_mg`.
    """
    ok_field = f"accel_{channel}_ok"
    band_field = f"accel_{channel}_band_swarm_mg"
    rows = []
    for d in range(days, 0, -1):
        day = NOW - timedelta(days=d)
        for h in hours:
            ts = day.replace(hour=h, minute=0, second=0, microsecond=0)
            mg = recent_mg if d <= insights.VIBRATION_RECENT_DAYS else baseline_mg
            rows.append({"measured_at": ts.isoformat(), ok_field: ok, band_field: mg})
    return rows


# ---------------------------------------------------------------------------
print("\n=== 1. Vibration swarm prediction: rising night-time 8–30 Hz band ===")
rows = _vib_rows(12, recent_mg=2.5, baseline_mg=0.5)
accel_swarm = insights._accel_band_series(rows, 1, "swarm")
alert = insights.detect_vibration_swarm_prediction(accel_swarm, 1, NOW)
check("fires on a clear rise", alert is not None)
check("category is swarm", alert.category == "swarm")
check("severity is watch", alert.severity == "watch")
check("ratio recorded and >= standalone mult",
      alert.evidence.get("vibration_swarm_ratio", 0) >= insights.VIBRATION_SWARM_STANDALONE_MULT)
check("confidence in (0,1]", 0.0 < alert.confidence <= 1.0)


# ---------------------------------------------------------------------------
print("\n=== 2. Flat vibration: no alert ===")
flat = _vib_rows(12, recent_mg=0.5, baseline_mg=0.5)
flat_series = insights._accel_band_series(flat, 1, "swarm")
check("no alert when band is flat",
      insights.detect_vibration_swarm_prediction(flat_series, 1, NOW) is None)


# ---------------------------------------------------------------------------
print("\n=== 3. Noise floor: tiny absolute levels don't fire on ratio alone ===")
# 10x rise but both levels are sub-floor (0.05 -> 0.5 mg): the recent mean is
# below VIBRATION_MIN_RECENT_MG, so the ratio must be suppressed.
tiny = _vib_rows(12, recent_mg=0.5, baseline_mg=0.05)
tiny_series = insights._accel_band_series(tiny, 1, "swarm")
recent_mg, baseline_mg, ratio = insights._vibration_swarm_rise(tiny_series, NOW)
check("ratio suppressed below the noise floor", ratio is None)
check("no alert below the noise floor",
      insights.detect_vibration_swarm_prediction(tiny_series, 1, NOW) is None)


# ---------------------------------------------------------------------------
print("\n=== 4. Not-ok accelerometer is ignored (no implicit zeros) ===")
not_ok = _vib_rows(12, recent_mg=2.5, baseline_mg=0.5, ok=False)
not_ok_series = insights._accel_band_series(not_ok, 1, "swarm")
check("ok=false rows excluded from the series", len(not_ok_series) == 0)
check("no alert when sensor never ok",
      insights.detect_vibration_swarm_prediction(not_ok_series, 1, NOW) is None)


# ---------------------------------------------------------------------------
print("\n=== 5. Off-season: prediction is suppressed ===")
winter = datetime(2024, 1, 15, 12, 0, tzinfo=timezone.utc)
winter_rows = []
for d in range(12, 0, -1):
    day = winter - timedelta(days=d)
    for h in (1, 2, 3, 4):
        mg = 2.5 if d <= insights.VIBRATION_RECENT_DAYS else 0.5
        winter_rows.append(
            {"measured_at": day.replace(hour=h).isoformat(),
             "accel_1_ok": True, "accel_1_band_swarm_mg": mg}
        )
winter_series = insights._accel_band_series(winter_rows, 1, "swarm")
check("no swarm prediction outside the active season",
      insights.detect_vibration_swarm_prediction(winter_series, 1, winter) is None)


# ---------------------------------------------------------------------------
print("\n=== 6. Vibration boosts the temperature pre-swarm watch ===")
# Build hive-temp rows that trip detect_pre_swarm_temp_instability with a
# MODEST variance ratio (~1.6×), so the base confidence stays well below the 1.0
# cap and the vibration boost is observable. Baseline alternates ±0.5 °C
# (std-dev ~0.5); the last 24h alternates ±0.8 °C (std-dev ~0.8).
def _temp_rows():
    rows = []
    for d in range(8, 1, -1):
        day = NOW - timedelta(days=d)
        for h in range(0, 24, 3):
            rows.append({"measured_at": day.replace(hour=h).isoformat(),
                         "hive_1_temp_c": 34.5 if (h // 3) % 2 == 0 else 35.5})
    for k in range(12):
        ts = NOW - timedelta(hours=24) + timedelta(hours=2 * k)
        rows.append({"measured_at": ts.isoformat(),
                     "hive_1_temp_c": 34.2 if k % 2 == 0 else 35.8})
    return rows

temp_rows = _temp_rows()
hive_temp = insights._extract_series(temp_rows, "hive_1_temp_c")
base = insights.detect_pre_swarm_temp_instability(hive_temp, 1, NOW)
check("temperature-only pre-swarm watch fires", base is not None)

rising = insights._accel_band_series(_vib_rows(12, 2.5, 0.5), 1, "swarm")
boosted = insights.detect_pre_swarm_temp_instability(hive_temp, 1, NOW, None, rising)
check("boosted watch still fires", boosted is not None)
check("vibration recorded as active", boosted.evidence.get("vibration_swarm_active") is True)
check("confidence boosted above temperature-only", boosted.confidence > base.confidence)

# Flat but ABOVE the noise floor (1.0 mg): the ratio is computed (~1.0) and,
# being below the rise multiplier, is recorded as inactive without boosting.
flatv = insights._accel_band_series(_vib_rows(12, 1.0, 1.0), 1, "swarm")
unboosted = insights.detect_pre_swarm_temp_instability(hive_temp, 1, NOW, None, flatv)
check("flat vibration does not boost", unboosted.confidence == base.confidence)
check("flat vibration marked inactive", unboosted.evidence.get("vibration_swarm_active") is False)


# ---------------------------------------------------------------------------
print("\n=== 7. End-to-end via compute_insights (channel 2) ===")
e2e = _vib_rows(12, recent_mg=3.0, baseline_mg=0.5, channel=2)
alerts = insights.compute_insights(e2e, now=NOW)
ids = [a.id for a in alerts]
check("swarm-vibration-ch2 present end-to-end", "swarm-vibration-ch2" in ids)


print("\nAll accelerometer-rule tests passed.\n")
