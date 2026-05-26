"""
HiveScale sensor-based insights / alerts.

This module computes rule-based alerts from the measurement time-series that
HiveScale already stores (weight, internal hive temperature, ambient
temperature/humidity). It is intentionally pure: it takes a list of
measurement dicts (as returned by ``measurement_row_to_dict`` in main.py) and
returns a list of ``Alert`` objects. No DB access, no FastAPI imports - so
it is unit-testable in isolation.

------------------------------------------------------------------------------
Hardware assumptions
------------------------------------------------------------------------------
The current HiveScale ESP32 firmware delivers, per measurement:
    * ``scale_1_weight_kg`` / ``scale_2_weight_kg``   (HX711 + load cells)
    * ``hive_1_temp_c``     / ``hive_2_temp_c``       (DS18B20 internal probes)
    * ``ambient_temp_c``    / ``ambient_humidity_percent`` (SHT4x)

The original spec (see project docs) also describes algorithms based on
**hive sound** (piping/tooting in 300-550 Hz, queenless acoustic signature)
and a **bee counter** (in/out flight counts). HiveScale does **not** currently
ship a microphone or an entrance counter. Wherever an algorithm depends on
those sensors, the corresponding branch is marked

    # NOT IMPLEMENTED: requires <sensor>

so the structure is ready to be wired up once such a sensor is added.

------------------------------------------------------------------------------
Sources for the thresholds used below
------------------------------------------------------------------------------
* Project spec ("Phase 1/2/3 swarm warning", queenlessness, robbing,
  foraging, brood cycle, absconding, winter survival, harvest timing) -
  this is the local design doc reproduced in the conversation history.
* Seeley, T. D. (2010). *Honeybee Democracy* - swarm preparation behaviour.
* Kulkarni & Murphy time-series benchmark - weight + in-hive temp + entrance
  traffic, see PMC 11479372 (Frontiers / open access). Used here only for
  algorithmic shape; their dataset is the recommended validation set
  because it matches HiveScale's sensor stack the closest.
* MSPB multi-modal dataset, arXiv 2311.10876 - audio + temp + humidity over
  53 hives x 1 year; cited as validation for the temperature-based
  queenlessness fallback.
* Stalidzans, E. & Berzonis, A. (2013), "Temperature changes above the
  upper hive entrance ... swarming preparation indicator" - the 34-35 degC
  brood-nest baseline and the +1.5-3.4 degC pre-swarm rise.
* Meikle, W. G. et al. (2008), "Within-day variation in continuous hive
  weight data as a measure of honey bee colony activity" - rationale for
  the day-night weight-delta foraging algorithm.

All citations are pointers, not appeals to authority - the numeric
thresholds are still tunable knobs and should be re-calibrated against
your own historical data and the public datasets listed in the project's
testing plan (MSPB, BeeTogether, UrBAN, NU-Hive, OSBH, BUZZ1-4,
Kulkarni/Murphy).
"""

from __future__ import annotations

import statistics
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from typing import Any, Iterable, Literal, Optional

from pydantic import BaseModel, Field


# ---------------------------------------------------------------------------
# Public types
# ---------------------------------------------------------------------------

AlertSeverity = Literal["info", "watch", "warning", "critical"]
AlertCategory = Literal[
    "swarm",
    "queenless",
    "robbing",
    "foraging",
    "brood",
    "decline",
    "winter",
    "harvest",
]
ChannelRef = Literal[1, 2]  # which scale/hive channel the alert is about


class Alert(BaseModel):
    """A single insight/alert derived from sensor data."""

    id: str = Field(..., description="Stable id, unique within one compute pass")
    category: AlertCategory
    severity: AlertSeverity
    channel: ChannelRef
    title: str
    description: str
    window_start: Optional[datetime] = None
    window_end: Optional[datetime] = None
    confidence: float = Field(..., ge=0.0, le=1.0)
    evidence: dict[str, Any] = Field(default_factory=dict)
    source: str = Field(
        default="",
        description="Short reference to the algorithm/literature source",
    )


# ---------------------------------------------------------------------------
# Tunable thresholds
# ---------------------------------------------------------------------------
#
# Centralised so they can later be exposed via /api/v1/devices/.../config or
# moved into the DB without touching the algorithms.

# Phase 2 swarm warning - imminent (Stalidzans & Berzonis 2013, project spec)
BROOD_NEST_TARGET_C = 35.0
BROOD_NEST_TOLERANCE_C = 1.5            # 34.0 - 36.0 is "normal"
IMMINENT_SWARM_DELTA_C = 1.5            # alert when temp exceeds baseline by this much
IMMINENT_SWARM_SLOPE_C_PER_HOUR = 0.5   # and is still rising

# Phase 1 swarm watch - brood-nest temperature instability (24h std-dev)
PRE_SWARM_STD_MULTIPLIER = 1.5
PRE_SWARM_BASELINE_DAYS = 7

# Phase 3 swarm event - weight signature (rapid drop)
SWARM_WEIGHT_DROP_KG = 1.5              # sudden drop of >= 1.5 kg within window
SWARM_WEIGHT_WINDOW_MIN = 30            # ... over <= 30 minutes
SWARM_DAYTIME_HOURS = (9, 17)           # swarms almost always leave during the day

# Robbing - rapid weight loss without takeoff signature, often in late afternoon
ROBBING_WEIGHT_LOSS_KG_PER_HOUR = 0.4
ROBBING_LATE_AFTERNOON_HOURS = (15, 19)
ROBBING_MIN_DURATION_MIN = 30

# Queenlessness fallback (no audio): widening hive temp std-dev + stagnant weight
QUEENLESS_TEMP_STDDEV_C = 1.0           # rolling 24h std-dev threshold
QUEENLESS_DAYS_WINDOW = 7
QUEENLESS_WEIGHT_STAGNANT_KG = 0.2      # <0.2 kg net change over 7d during active season

# Foraging intensity (Meikle et al. 2008, project spec)
FORAGING_STRONG_KG_PER_DAY = 1.0
FORAGING_MODERATE_KG_PER_DAY = 0.2

# Brood cycle state (temp std-dev classifier)
BROOD_ACTIVE_STDDEV_C = 0.5             # < 0.5 degC => active brood
BROOD_BROODLESS_STDDEV_C = 2.0          # > 2.0 degC => broodless / weak

# Absconding / collapse - compound declining trend
ABSCONDING_LOOKBACK_DAYS = 14
ABSCONDING_WEIGHT_LOSS_G_PER_DAY = 100  # >100 g/day sustained loss
ABSCONDING_TEMP_STDDEV_C = 1.5          # widening thermoregulation variance

# Winter survival risk (Oct-Feb in northern hemisphere)
WINTER_CLUSTER_DELTA_C = 2.0            # min hive temp - ambient temp must exceed this
WINTER_WEIGHT_LOSS_G_PER_WEEK = 300

# Honey-ready / harvest timing - plateau after sustained gain
HARVEST_FLOW_KG_PER_WEEK = 2.0
HARVEST_PLATEAU_KG_PER_WEEK = 0.3
HARVEST_PLATEAU_DAYS = 4


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

# A "series" is a list of (datetime, float) tuples sorted ascending by time.
SeriesPoint = tuple[datetime, float]
Series = list[SeriesPoint]


def _as_datetime(value: Any) -> Optional[datetime]:
    """Coerce ``measured_at`` (datetime or ISO string) to an aware UTC datetime."""
    if value is None:
        return None
    if isinstance(value, datetime):
        return value if value.tzinfo else value.replace(tzinfo=timezone.utc)
    if isinstance(value, str):
        try:
            # FastAPI / psycopg sometimes serialise with trailing Z
            v = value.replace("Z", "+00:00")
            dt = datetime.fromisoformat(v)
            return dt if dt.tzinfo else dt.replace(tzinfo=timezone.utc)
        except ValueError:
            return None
    return None


def _extract_series(
    measurements: Iterable[dict[str, Any]], field: str
) -> Series:
    """Build a clean ascending (datetime, float) series for ``field``."""
    out: Series = []
    for m in measurements:
        ts = _as_datetime(m.get("measured_at"))
        val = m.get(field)
        if ts is None or val is None:
            continue
        try:
            out.append((ts, float(val)))
        except (TypeError, ValueError):
            continue
    out.sort(key=lambda p: p[0])
    return out


def _window(series: Series, end: datetime, hours: float) -> Series:
    """Return points in (end - hours, end]."""
    start = end - timedelta(hours=hours)
    return [(t, v) for t, v in series if start < t <= end]


def _values(series: Series) -> list[float]:
    return [v for _, v in series]


def _safe_mean(values: list[float]) -> Optional[float]:
    return statistics.fmean(values) if values else None


def _safe_stddev(values: list[float]) -> Optional[float]:
    return statistics.pstdev(values) if len(values) >= 2 else None


def _linear_slope_per_day(series: Series) -> Optional[float]:
    """
    Least-squares slope in <unit>/day. Returns None if there are fewer than
    2 distinct timestamps.
    """
    if len(series) < 2:
        return None
    t0 = series[0][0]
    xs = [(t - t0).total_seconds() / 86400.0 for t, _ in series]
    ys = [v for _, v in series]
    if max(xs) - min(xs) <= 0:
        return None
    n = len(xs)
    mean_x = sum(xs) / n
    mean_y = sum(ys) / n
    num = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys))
    den = sum((x - mean_x) ** 2 for x in xs)
    if den == 0:
        return None
    return num / den


def _max_drop(series: Series, window_minutes: int) -> tuple[Optional[float], Optional[datetime], Optional[datetime]]:
    """
    Maximum (start_value - end_value) where the end-point is within
    ``window_minutes`` after the start-point. Returns (drop_kg, t_start, t_end).
    Positive drop means weight decreased.
    """
    if len(series) < 2:
        return (None, None, None)
    window = timedelta(minutes=window_minutes)
    best = 0.0
    best_pair: tuple[Optional[datetime], Optional[datetime]] = (None, None)
    j = 0
    for i in range(len(series)):
        ti, vi = series[i]
        if j < i + 1:
            j = i + 1
        while j < len(series) and series[j][0] - ti <= window:
            drop = vi - series[j][1]
            if drop > best:
                best = drop
                best_pair = (ti, series[j][0])
            j += 1
        # Reset j cursor for the next i: keep monotone scan
        j = i + 1
    return (best, best_pair[0], best_pair[1]) if best > 0 else (None, None, None)


def _is_active_season(when: datetime) -> bool:
    """Rough northern-hemisphere active season filter (Mar-Sep)."""
    return 3 <= when.month <= 9


def _is_winter(when: datetime) -> bool:
    """Northern-hemisphere winter window for the winter-risk algorithm."""
    # Oct, Nov, Dec, Jan, Feb
    return when.month >= 10 or when.month <= 2


# ---------------------------------------------------------------------------
# Algorithms - one detector per alert category, per channel
# ---------------------------------------------------------------------------
#
# Each detector takes the time-series it needs and returns Optional[Alert].
# The orchestrator further down calls them for both scale channels.


def detect_imminent_swarm(
    hive_temp_series: Series,
    channel: ChannelRef,
    now: datetime,
) -> Optional[Alert]:
    """
    Phase 2 - imminent swarm (~10-30 min ahead) from brood-nest temperature.

    Source: project spec "Phase 2"; Stalidzans & Berzonis 2013.
    Rule: rolling 1h baseline temp + temp now exceeds (baseline + 1.5 degC)
    AND the slope over the last hour is still rising.
    The absolute value should also be above the upper brood-nest tolerance
    (~36.5 degC) so we don't flag a hive that was simply cold and warmed up.
    """
    last_1h = _window(hive_temp_series, now, hours=1)
    last_4h = _window(hive_temp_series, now, hours=4)
    if len(last_1h) < 3 or len(last_4h) < 5:
        return None

    # baseline = the three hours BEFORE the last hour
    baseline_cutoff = now - timedelta(hours=1)
    baseline_window = [(t, v) for t, v in last_4h if t <= baseline_cutoff]
    baseline = _safe_mean(_values(baseline_window))
    current = last_1h[-1][1]
    slope_per_hour = _linear_slope_per_day(last_1h)
    if baseline is None or slope_per_hour is None:
        return None
    slope_per_hour = slope_per_hour / 24.0

    delta = current - baseline
    above_brood_upper = current > (BROOD_NEST_TARGET_C + BROOD_NEST_TOLERANCE_C)
    if delta >= IMMINENT_SWARM_DELTA_C and slope_per_hour >= IMMINENT_SWARM_SLOPE_C_PER_HOUR and above_brood_upper:
        return Alert(
            id=f"swarm-imminent-ch{channel}",
            category="swarm",
            severity="warning",
            channel=channel,
            title=f"Imminent swarm warning (hive {channel})",
            description=(
                f"Brood-nest temperature is {current:.1f} degC, "
                f"{delta:.1f} degC above the 4h baseline and still rising "
                f"({slope_per_hour:.2f} degC/h). Swarm may issue within 30 min."
            ),
            window_start=last_4h[0][0],
            window_end=now,
            confidence=min(1.0, 0.5 + delta / 5.0),
            evidence={
                "current_c": current,
                "baseline_c": baseline,
                "delta_c": delta,
                "slope_c_per_hour": slope_per_hour,
            },
            source="project spec Phase 2; Stalidzans & Berzonis 2013",
        )
    return None


def detect_swarm_event(
    weight_series: Series,
    channel: ChannelRef,
    now: datetime,
) -> Optional[Alert]:
    """
    Phase 3 - swarm in progress / just happened from weight signature.

    Source: project spec "Phase 3" (weight half only - the counter half is
    not implementable on current hardware).

    Rule: a sudden weight drop >= SWARM_WEIGHT_DROP_KG within a window of
    <= SWARM_WEIGHT_WINDOW_MIN minutes, during daytime swarm hours.

    NOTE: A bee-counter would let us combine this with massive asymmetric
    outflow (out_count > 3 * baseline AND out/(in+1) > 5). Once a counter
    sensor is added, AND the two signals together to raise confidence /
    severity here.
    """
    # NOT IMPLEMENTED: counter-based corroboration. Falls back to weight-only.
    recent = _window(weight_series, now, hours=2)
    if len(recent) < 4:
        return None
    drop, t_start, t_end = _max_drop(recent, SWARM_WEIGHT_WINDOW_MIN)
    if drop is None or t_start is None or t_end is None:
        return None
    if drop < SWARM_WEIGHT_DROP_KG:
        return None
    hour_local = t_end.astimezone(timezone.utc).hour  # caller may localise via TZ later
    in_daytime = SWARM_DAYTIME_HOURS[0] <= hour_local < SWARM_DAYTIME_HOURS[1]
    severity: AlertSeverity = "critical" if in_daytime else "warning"
    return Alert(
        id=f"swarm-event-ch{channel}",
        category="swarm",
        severity=severity,
        channel=channel,
        title=f"Possible swarm event (hive {channel})",
        description=(
            f"Weight dropped {drop:.2f} kg between {t_start.isoformat()} "
            f"and {t_end.isoformat()}. Consistent with a swarm departure."
        ),
        window_start=t_start,
        window_end=t_end,
        confidence=min(1.0, 0.6 + (drop - SWARM_WEIGHT_DROP_KG) * 0.1),
        evidence={
            "weight_drop_kg": drop,
            "in_daytime_window": in_daytime,
        },
        source="project spec Phase 3 (weight component)",
    )


def detect_pre_swarm_temp_instability(
    hive_temp_series: Series,
    channel: ChannelRef,
    now: datetime,
) -> Optional[Alert]:
    """
    Phase 1 - pre-swarm watch (hours to days ahead) using temperature only.

    Source: project spec "Phase 1" (temperature half only).
    Rule: rolling 24h std-dev of hive temp exceeds the 7-day baseline std-dev
    by >= 50%.

    NOTE: piping/tooting acoustic detection (300-550 Hz narrowband tones)
    is the strongest signal in the published literature. It is NOT
    IMPLEMENTED: requires audio sensor.
    """
    last_24h = _window(hive_temp_series, now, hours=24)
    last_baseline = _window(
        hive_temp_series,
        now - timedelta(hours=24),
        hours=24 * PRE_SWARM_BASELINE_DAYS,
    )
    if len(last_24h) < 6 or len(last_baseline) < 12:
        return None

    s_now = _safe_stddev(_values(last_24h))
    s_base = _safe_stddev(_values(last_baseline))
    if s_now is None or s_base is None or s_base <= 0:
        return None
    ratio = s_now / s_base
    # "exceeds baseline by >50%" -> ratio >= 1.5 when PRE_SWARM_STD_MULTIPLIER=1.5
    if ratio >= PRE_SWARM_STD_MULTIPLIER:
        return Alert(
            id=f"swarm-watch-ch{channel}",
            category="swarm",
            severity="watch",
            channel=channel,
            title=f"Pre-swarm watch (hive {channel})",
            description=(
                f"24h brood-nest temperature variability ({s_now:.2f} degC) "
                f"is {(ratio - 1) * 100:.0f}% above the 7d baseline. "
                f"Inspect for queen cells in the next 24-48h."
            ),
            window_start=last_baseline[0][0],
            window_end=now,
            confidence=min(1.0, 0.4 + (ratio - 1.5) * 0.5),
            evidence={
                "stddev_24h_c": s_now,
                "stddev_baseline_c": s_base,
                "ratio": ratio,
            },
            source="project spec Phase 1 (temp); MSPB arXiv 2311.10876",
        )
    return None


def detect_queenlessness(
    hive_temp_series: Series,
    weight_series: Series,
    channel: ChannelRef,
    now: datetime,
) -> Optional[Alert]:
    """
    Queenlessness - rule-based fallback (no audio).

    Source: project spec "Queenlessness detection"; MSPB / BeeTogether
    queenless acoustic classifiers are the gold standard, but with weight
    and temperature alone we can still implement two of three sub-rules:

      1. Hive temperature 24h std-dev > 1.0 degC sustained (no brood ->
         no thermoregulation).
      2. Weight stagnant during the active season (|delta| < 0.2 kg over 7d).

    Fires when both fire. Confidence stays moderate without acoustic input.

    NOTE: forager-count decline (~5% per day for 7+ days) is NOT IMPLEMENTED:
    requires entrance counter.
    """
    if not _is_active_season(now):
        return None

    days = QUEENLESS_DAYS_WINDOW
    recent_temp = _window(hive_temp_series, now, hours=24 * days)
    recent_weight = _window(weight_series, now, hours=24 * days)
    if len(recent_temp) < 12 or len(recent_weight) < 12:
        return None

    # Rule 1: temp std-dev too wide
    stddev = _safe_stddev(_values(recent_temp))
    if stddev is None or stddev < QUEENLESS_TEMP_STDDEV_C:
        return None

    # Rule 2: weight stagnant during active season
    delta = recent_weight[-1][1] - recent_weight[0][1]
    if abs(delta) > QUEENLESS_WEIGHT_STAGNANT_KG:
        return None

    return Alert(
        id=f"queenless-ch{channel}",
        category="queenless",
        severity="warning",
        channel=channel,
        title=f"Possible queenlessness (hive {channel})",
        description=(
            f"Over the last {days}d, hive temperature variability "
            f"({stddev:.2f} degC) suggests broodless thermoregulation, and "
            f"net weight change is only {delta:+.2f} kg during the active "
            f"season. Inspect for eggs / brood pattern."
        ),
        window_start=recent_temp[0][0],
        window_end=now,
        confidence=0.55,  # moderate without acoustic confirmation
        evidence={
            "temp_stddev_c": stddev,
            "weight_delta_kg": delta,
            "window_days": days,
        },
        source="project spec queenless (2 of 3 rule, no audio)",
    )


def detect_robbing(
    weight_series: Series,
    channel: ChannelRef,
    now: datetime,
) -> Optional[Alert]:
    """
    Robbing detection - rapid weight loss, often in late afternoon during
    dearth.

    Source: project spec "Robbing detection".
    Rule: weight loss rate >= ROBBING_WEIGHT_LOSS_KG_PER_HOUR sustained for
    >= ROBBING_MIN_DURATION_MIN minutes, NOT matching a swarm signature.

    NOTE: incoming-count spikes with low outgoing AND agitated acoustic
    spectrum are the canonical robbing signals. Those are NOT IMPLEMENTED:
    require counter and audio.
    """
    last_2h = _window(weight_series, now, hours=2)
    if len(last_2h) < 4:
        return None
    duration_h = (last_2h[-1][0] - last_2h[0][0]).total_seconds() / 3600.0
    if duration_h < ROBBING_MIN_DURATION_MIN / 60.0:
        return None
    delta = last_2h[0][1] - last_2h[-1][1]  # positive => weight lost
    rate_kg_per_h = delta / duration_h if duration_h > 0 else 0.0
    if rate_kg_per_h < ROBBING_WEIGHT_LOSS_KG_PER_HOUR:
        return None

    # Avoid double-firing with the swarm-event detector
    drop, _, _ = _max_drop(last_2h, SWARM_WEIGHT_WINDOW_MIN)
    if drop is not None and drop >= SWARM_WEIGHT_DROP_KG:
        return None

    hour = last_2h[-1][0].hour
    in_afternoon = ROBBING_LATE_AFTERNOON_HOURS[0] <= hour < ROBBING_LATE_AFTERNOON_HOURS[1]
    severity: AlertSeverity = "warning" if in_afternoon else "watch"
    return Alert(
        id=f"robbing-ch{channel}",
        category="robbing",
        severity=severity,
        channel=channel,
        title=f"Possible robbing (hive {channel})",
        description=(
            f"Sustained weight loss of {rate_kg_per_h:.2f} kg/h over "
            f"{duration_h * 60:.0f} min. {'Late afternoon timing is consistent with dearth-period robbing.' if in_afternoon else 'Unusual time of day - investigate.'}"
        ),
        window_start=last_2h[0][0],
        window_end=now,
        confidence=0.5 + (0.2 if in_afternoon else 0.0),
        evidence={
            "rate_kg_per_h": rate_kg_per_h,
            "duration_min": duration_h * 60.0,
            "in_afternoon": in_afternoon,
        },
        source="project spec robbing (weight component)",
    )


def detect_foraging_intensity(
    weight_series: Series,
    channel: ChannelRef,
    now: datetime,
) -> Optional[Alert]:
    """
    Foraging intensity / nectar flow from daily weight delta.

    Source: project spec "Foraging intensity"; Meikle et al. 2008.
    Returns an *informational* alert classifying the current flow strength.
    """
    last_24h = _window(weight_series, now, hours=24)
    if len(last_24h) < 4:
        return None
    delta = last_24h[-1][1] - last_24h[0][1]

    if delta >= FORAGING_STRONG_KG_PER_DAY:
        level, severity = "strong", "info"
    elif delta >= FORAGING_MODERATE_KG_PER_DAY:
        level, severity = "moderate", "info"
    elif delta <= -FORAGING_MODERATE_KG_PER_DAY:
        # Net loss during what should be foraging time - flag as watch
        level, severity = "negative", "watch"
    else:
        # Quiet day - no alert worth raising
        return None

    return Alert(
        id=f"foraging-ch{channel}",
        category="foraging",
        severity=severity,  # type: ignore[arg-type]
        channel=channel,
        title=f"Foraging: {level} flow (hive {channel})",
        description=(
            f"Net weight change over the last 24h: {delta:+.2f} kg. "
            f"Classified as {level} nectar flow."
        ),
        window_start=last_24h[0][0],
        window_end=now,
        confidence=0.8,
        evidence={"delta_24h_kg": delta, "level": level},
        source="project spec foraging; Meikle et al. 2008",
    )


def detect_brood_cycle_state(
    hive_temp_series: Series,
    channel: ChannelRef,
    now: datetime,
) -> Optional[Alert]:
    """
    Brood-cycle / colony-state classifier from rolling 24h std-dev of
    hive temperature.

    Source: project spec "Brood cycle / colony state".
        std-dev < 0.5 degC around 34-35 degC -> active brood rearing
        std-dev > 2.0 degC                    -> broodless / weak colony
    """
    last_24h = _window(hive_temp_series, now, hours=24)
    if len(last_24h) < 8:
        return None
    stddev = _safe_stddev(_values(last_24h))
    mean = _safe_mean(_values(last_24h))
    if stddev is None or mean is None:
        return None

    if stddev < BROOD_ACTIVE_STDDEV_C and abs(mean - BROOD_NEST_TARGET_C) <= BROOD_NEST_TOLERANCE_C:
        title = f"Active brood rearing (hive {channel})"
        desc = (
            f"24h hive temp held at {mean:.1f}+/-{stddev:.2f} degC - "
            f"consistent with active brood thermoregulation."
        )
        severity: AlertSeverity = "info"
    elif stddev > BROOD_BROODLESS_STDDEV_C:
        title = f"Broodless / weak colony indicator (hive {channel})"
        desc = (
            f"24h hive temp variability is wide (+/-{stddev:.2f} degC, "
            f"mean {mean:.1f} degC). Suggests little or no brood being "
            f"thermoregulated."
        )
        severity = "watch"
    else:
        return None

    return Alert(
        id=f"brood-state-ch{channel}",
        category="brood",
        severity=severity,
        channel=channel,
        title=title,
        description=desc,
        window_start=last_24h[0][0],
        window_end=now,
        confidence=0.7,
        evidence={"mean_c": mean, "stddev_c": stddev},
        source="project spec brood cycle",
    )


def detect_absconding_trend(
    hive_temp_series: Series,
    weight_series: Series,
    channel: ChannelRef,
    now: datetime,
) -> Optional[Alert]:
    """
    Absconding / collapse early warning - compound declining trend.

    Source: project spec "Absconding / collapse early warning".
    Requires (in the original spec) THREE agreeing trends. With current
    sensors we can implement TWO of three:

      * Weight losing > 100 g/day sustained over 14d.
      * Temperature 24h std-dev widening (regression slope positive on
        the daily std-dev series).

    NOTE: counter daily-peak declining linear trend is NOT IMPLEMENTED:
    requires entrance counter. So this alert stays at "watch" until a
    counter is added; bump to "warning" automatically when the third
    signal becomes available.
    """
    weight_window = _window(weight_series, now, hours=24 * ABSCONDING_LOOKBACK_DAYS)
    temp_window = _window(hive_temp_series, now, hours=24 * ABSCONDING_LOOKBACK_DAYS)
    if len(weight_window) < 14 or len(temp_window) < 14:
        return None

    weight_slope_kg_per_day = _linear_slope_per_day(weight_window)
    if weight_slope_kg_per_day is None:
        return None
    weight_loss_g_per_day = -weight_slope_kg_per_day * 1000.0
    if weight_loss_g_per_day < ABSCONDING_WEIGHT_LOSS_G_PER_DAY:
        return None

    # Daily std-dev series of hive temp
    by_day: dict[str, list[float]] = {}
    for t, v in temp_window:
        by_day.setdefault(t.date().isoformat(), []).append(v)
    daily_stddev: Series = []
    for day, values in sorted(by_day.items()):
        s = _safe_stddev(values)
        if s is None:
            continue
        # use noon UTC of that day as a representative timestamp
        daily_stddev.append((datetime.fromisoformat(day + "T12:00:00+00:00"), s))
    if len(daily_stddev) < 4:
        return None
    stddev_slope = _linear_slope_per_day(daily_stddev)
    if stddev_slope is None or stddev_slope <= 0:
        return None

    # Most recent daily std-dev should already be wide enough
    if daily_stddev[-1][1] < ABSCONDING_TEMP_STDDEV_C:
        return None

    return Alert(
        id=f"decline-ch{channel}",
        category="decline",
        severity="watch",  # bumped to warning once counter trend is wired in
        channel=channel,
        title=f"Compound colony decline (hive {channel})",
        description=(
            f"Over the last {ABSCONDING_LOOKBACK_DAYS}d the hive is losing "
            f"~{weight_loss_g_per_day:.0f} g/day and brood-nest thermo "
            f"variability is widening (slope {stddev_slope:+.3f} degC/day). "
            f"Inspect for queen problems, disease, or pre-absconding stress."
        ),
        window_start=weight_window[0][0],
        window_end=now,
        confidence=0.5,
        evidence={
            "weight_loss_g_per_day": weight_loss_g_per_day,
            "stddev_slope_c_per_day": stddev_slope,
            "current_daily_stddev_c": daily_stddev[-1][1],
        },
        source="project spec absconding/collapse (2 of 3 rule, no counter)",
    )


def detect_winter_risk(
    hive_temp_series: Series,
    ambient_temp_series: Series,
    weight_series: Series,
    channel: ChannelRef,
    now: datetime,
) -> Optional[Alert]:
    """
    Winter survival risk (Oct-Feb) from cluster temperature, weight loss
    rate and (when available) cleansing-flight activity.

    Source: project spec "Winter survival risk".
    Rule:
      * Cluster losing heat: min(hive_temp_24h) < ambient_24h_mean +
        WINTER_CLUSTER_DELTA_C  -> indicates the cluster is small / weak.
      * Abnormal consumption: weight loss > 300 g/week sustained.

    NOTE: cleansing-flight activity during warm spells (out_count > 50 on
    warm days) is NOT IMPLEMENTED: requires entrance counter.
    """
    if not _is_winter(now):
        return None

    last_7d_temp = _window(hive_temp_series, now, hours=24 * 7)
    last_7d_amb = _window(ambient_temp_series, now, hours=24 * 7)
    last_7d_weight = _window(weight_series, now, hours=24 * 7)
    if len(last_7d_temp) < 12 or len(last_7d_weight) < 4 or len(last_7d_amb) < 12:
        return None

    min_hive = min(_values(last_7d_temp))
    ambient_mean = _safe_mean(_values(last_7d_amb))
    if ambient_mean is None:
        return None
    cluster_weak = min_hive < (ambient_mean + WINTER_CLUSTER_DELTA_C)

    weight_delta_kg = last_7d_weight[-1][1] - last_7d_weight[0][1]
    weight_loss_g_per_week = -weight_delta_kg * 1000.0
    consumption_high = weight_loss_g_per_week > WINTER_WEIGHT_LOSS_G_PER_WEEK

    if not (cluster_weak or consumption_high):
        return None
    severity: AlertSeverity = "warning" if cluster_weak and consumption_high else "watch"
    return Alert(
        id=f"winter-ch{channel}",
        category="winter",
        severity=severity,
        channel=channel,
        title=f"Winter survival risk (hive {channel})",
        description=(
            f"Min hive temp last 7d: {min_hive:.1f} degC vs. mean ambient "
            f"{ambient_mean:.1f} degC. Weight change: {weight_delta_kg:+.2f} kg/week."
            + (" Cluster appears weak/small." if cluster_weak else "")
            + (" Consumption is abnormally high." if consumption_high else "")
        ),
        window_start=last_7d_temp[0][0],
        window_end=now,
        confidence=0.6,
        evidence={
            "min_hive_temp_c": min_hive,
            "mean_ambient_c": ambient_mean,
            "weight_loss_g_per_week": weight_loss_g_per_week,
            "cluster_weak": cluster_weak,
            "consumption_high": consumption_high,
        },
        source="project spec winter survival",
    )


def detect_harvest_window(
    weight_series: Series,
    channel: ChannelRef,
    now: datetime,
) -> Optional[Alert]:
    """
    Honey-ready / harvest timing - plateau after sustained gain.

    Source: project spec "Honey-ready / harvest timing".
    Rule: rolling 7d weight delta transitions from > 2 kg/week to
    < 0.3 kg/week and stays there for >= 4 days.
    """
    last_11d = _window(weight_series, now, hours=24 * 11)
    if len(last_11d) < 24:
        return None

    # 7d ending at "now"
    win_now = _window(weight_series, now, hours=24 * 7)
    # 7d ending HARVEST_PLATEAU_DAYS+ ago
    win_prev = _window(
        weight_series,
        now - timedelta(days=HARVEST_PLATEAU_DAYS),
        hours=24 * 7,
    )
    if len(win_now) < 4 or len(win_prev) < 4:
        return None
    delta_now = win_now[-1][1] - win_now[0][1]
    delta_prev = win_prev[-1][1] - win_prev[0][1]

    if delta_prev > HARVEST_FLOW_KG_PER_WEEK and delta_now < HARVEST_PLATEAU_KG_PER_WEEK:
        return Alert(
            id=f"harvest-ch{channel}",
            category="harvest",
            severity="info",
            channel=channel,
            title=f"Harvest window likely open (hive {channel})",
            description=(
                f"Weight gain has plateaued: {delta_prev:+.2f} kg/week "
                f"earlier this period vs {delta_now:+.2f} kg/week now. "
                f"The flow appears finished - supers may be ready to harvest."
            ),
            window_start=win_prev[0][0],
            window_end=now,
            confidence=0.7,
            evidence={
                "delta_prev_kg_per_week": delta_prev,
                "delta_now_kg_per_week": delta_now,
            },
            source="project spec harvest timing",
        )
    return None


# ---------------------------------------------------------------------------
# Orchestrator
# ---------------------------------------------------------------------------

def compute_insights(
    measurements: list[dict[str, Any]],
    now: Optional[datetime] = None,
) -> list[Alert]:
    """
    Run all detectors against both scale channels and return a flat list of
    Alerts sorted by severity then time.

    ``measurements`` is the same shape as ``measurement_row_to_dict`` returns
    in main.py: a list of dicts with at least ``measured_at``,
    ``scale_1_weight_kg``, ``scale_2_weight_kg``, ``hive_1_temp_c``,
    ``hive_2_temp_c``, ``ambient_temp_c``.
    """
    now = now or datetime.now(timezone.utc)
    if now.tzinfo is None:
        now = now.replace(tzinfo=timezone.utc)

    ambient = _extract_series(measurements, "ambient_temp_c")
    alerts: list[Alert] = []

    # Per-channel detectors. Scale 1 -> hive 1, scale 2 -> hive 2.
    for channel, weight_field, temp_field in (
        (1, "scale_1_weight_kg", "hive_1_temp_c"),
        (2, "scale_2_weight_kg", "hive_2_temp_c"),
    ):
        weight = _extract_series(measurements, weight_field)
        hive_temp = _extract_series(measurements, temp_field)

        # Skip a channel that has never reported anything
        if not weight and not hive_temp:
            continue

        for detector in (
            lambda: detect_imminent_swarm(hive_temp, channel, now),
            lambda: detect_swarm_event(weight, channel, now),
            lambda: detect_pre_swarm_temp_instability(hive_temp, channel, now),
            lambda: detect_queenlessness(hive_temp, weight, channel, now),
            lambda: detect_robbing(weight, channel, now),
            lambda: detect_foraging_intensity(weight, channel, now),
            lambda: detect_brood_cycle_state(hive_temp, channel, now),
            lambda: detect_absconding_trend(hive_temp, weight, channel, now),
            lambda: detect_winter_risk(hive_temp, ambient, weight, channel, now),
            lambda: detect_harvest_window(weight, channel, now),
        ):
            try:
                alert = detector()
            except Exception:
                # Defensive: a buggy detector must not break the whole pass
                alert = None
            if alert is not None:
                alerts.append(alert)

    severity_rank = {"critical": 4, "warning": 3, "watch": 2, "info": 1}
    alerts.sort(
        key=lambda a: (
            -severity_rank.get(a.severity, 0),
            a.window_end or datetime.min.replace(tzinfo=timezone.utc),
        )
    )
    return alerts


# ---------------------------------------------------------------------------
# Convenience dataclass for the HTTP summary endpoint
# ---------------------------------------------------------------------------

@dataclass
class InsightsSummary:
    device_id: str
    computed_at: datetime
    alert_count: int
    highest_severity: Optional[AlertSeverity]
    highest_alert: Optional[Alert]
    categories: list[AlertCategory]


def summarize(device_id: str, alerts: list[Alert], computed_at: datetime) -> InsightsSummary:
    severity_rank = {"critical": 4, "warning": 3, "watch": 2, "info": 1}
    highest = max(alerts, key=lambda a: severity_rank.get(a.severity, 0), default=None)
    categories = sorted({a.category for a in alerts})
    return InsightsSummary(
        device_id=device_id,
        computed_at=computed_at,
        alert_count=len(alerts),
        highest_severity=highest.severity if highest else None,
        highest_alert=highest,
        categories=categories,
    )