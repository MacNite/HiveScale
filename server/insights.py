"""
HiveScale sensor-based insights / alerts.

This module computes rule-based alerts from the measurement time-series that
HiveScale already stores (weight, internal hive temperature, ambient
temperature/humidity, and — when available — microphone RMS and FFT band
energy).

It is intentionally pure: it takes a list of measurement dicts (as returned
by ``measurement_row_to_dict`` in main.py) and returns a list of ``Alert``
objects. No DB access, no FastAPI imports — unit-testable in isolation.

------------------------------------------------------------------------------
Hardware assumptions
------------------------------------------------------------------------------
The current HiveScale ESP32 firmware delivers, per measurement:
    * ``scale_1_weight_kg`` / ``scale_2_weight_kg``   (HX711 + load cells)
    * ``hive_1_temp_c``     / ``hive_2_temp_c``       (DS18B20 internal probes)
    * ``ambient_temp_c``    / ``ambient_humidity_percent`` (SHT4x)
    * ``mic_left_rms_dbfs`` / ``mic_right_rms_dbfs``  (INMP441 broadband RMS)
    * ``mic_left_peak_dbfs``/ ``mic_right_peak_dbfs``
    * ``mic_left_band_*_dbfs`` / ``mic_right_band_*_dbfs``  (arduinoFFT bands)

Mic and FFT fields are optional: every detector degrades gracefully to its
weight/temperature-only rule when they are absent. The acoustic evidence
raises confidence and, in some cases, severity when both signals agree.

FFT bands (dBFS, 500 ms capture at 16 kHz, Hann window, 4096-point FFT):
    * sub_bass   50 –  150 Hz  structural vibration / low rumble
    * hum       150 –  300 Hz  normal colony hum (fundamental ~200 Hz)
    * piping    300 –  550 Hz  queen piping / tooting (pre-swarm signal)
    * stress    550 – 1500 Hz  agitated / robbing colony
    * high     1500 – 3000 Hz  harmonic overtones

------------------------------------------------------------------------------
Acoustic thresholds & literature
------------------------------------------------------------------------------
* Piping detection threshold of −45 dBFS for the 300–550 Hz band is
  conservatively derived from Seeley (2010) and Ramsey et al. (2020),
  "Acoustic detection of queen presence ...", PLOS ONE. The published
  piping fundamental is 320–480 Hz; 300–550 Hz gives margin.

* Queenless hum shift: a queenless colony shows elevated low-frequency
  noise (< 300 Hz) and a broadened, lower-pitched hum. We approximate this
  as hum_dbfs > −40 dBFS when the piping band is quiet (< −52 dBFS),
  combined with the existing temp/weight rules (MSPB arXiv 2311.10876).

* Robbing stress band: agitated bee flight and wing-beat noise concentrate
  in the 550–1500 Hz range. Threshold −38 dBFS based on the BUZZ dataset
  characterisation (Nolasco et al. 2019).

All thresholds are starting points and should be re-calibrated against your
own historical data and the public datasets (MSPB, BeeTogether, UrBAN,
NU-Hive, OSBH, BUZZ1–4, Kulkarni/Murphy).

------------------------------------------------------------------------------
Sources for the thresholds used below
------------------------------------------------------------------------------
* Project spec ("Phase 1/2/3 swarm warning", queenlessness, robbing,
  foraging, brood cycle, absconding, winter survival, harvest timing).
* Seeley, T. D. (2010). *Honeybee Democracy* - swarm preparation behaviour.
* Kulkarni & Murphy time-series benchmark - weight + in-hive temp + entrance
  traffic, PMC 11479372 (Frontiers / open access).
* MSPB multi-modal dataset, arXiv 2311.10876 - audio + temp + humidity over
  53 hives x 1 year.
* Stalidzans, E. & Berzonis, A. (2013), "Temperature changes above the
  upper hive entrance ... swarming preparation indicator".
* Meikle, W. G. et al. (2008), "Within-day variation in continuous hive
  weight data as a measure of honey bee colony activity".
* Ramsey, M. et al. (2020), "Acoustic detection of the honey bee ...
  piping signal", PLOS ONE.
* Nolasco, I. et al. (2019), "Honey bee detection ... BUZZ dataset",
  DCASE Workshop.
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
ChannelRef = Literal[1, 2]


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

BROOD_NEST_TARGET_C = 35.0
BROOD_NEST_TOLERANCE_C = 1.5
IMMINENT_SWARM_DELTA_C = 1.5
IMMINENT_SWARM_SLOPE_C_PER_HOUR = 0.5

PRE_SWARM_STD_MULTIPLIER = 1.5
PRE_SWARM_BASELINE_DAYS = 7

SWARM_WEIGHT_DROP_KG = 1.5
SWARM_WEIGHT_WINDOW_MIN = 30
SWARM_DAYTIME_HOURS = (9, 17)

ROBBING_WEIGHT_LOSS_KG_PER_HOUR = 0.4
ROBBING_LATE_AFTERNOON_HOURS = (15, 19)
ROBBING_MIN_DURATION_MIN = 30

QUEENLESS_TEMP_STDDEV_C = 1.0
QUEENLESS_DAYS_WINDOW = 7
QUEENLESS_WEIGHT_STAGNANT_KG = 0.2

FORAGING_STRONG_KG_PER_DAY = 1.0
FORAGING_MODERATE_KG_PER_DAY = 0.2

BROOD_ACTIVE_STDDEV_C = 0.5
BROOD_BROODLESS_STDDEV_C = 2.0

ABSCONDING_LOOKBACK_DAYS = 14
ABSCONDING_WEIGHT_LOSS_G_PER_DAY = 100
ABSCONDING_TEMP_STDDEV_C = 1.5

WINTER_CLUSTER_DELTA_C = 2.0
WINTER_WEIGHT_LOSS_G_PER_WEEK = 300

HARVEST_FLOW_KG_PER_WEEK = 2.0
HARVEST_PLATEAU_KG_PER_WEEK = 0.3
HARVEST_PLATEAU_DAYS = 4

# ── Acoustic thresholds (dBFS, see module docstring for literature refs) ────
# Pre-swarm: piping band energy at or above this level is a strong positive signal.
PIPING_ACTIVE_DBFS = -45.0
# Queenless hum shift: hum band is louder than this and piping is quiet.
QUEENLESS_HUM_DBFS = -40.0
QUEENLESS_PIPING_QUIET_DBFS = -52.0
# Robbing: stress band energy above this is consistent with agitated flight.
ROBBING_STRESS_DBFS = -38.0


# ---------------------------------------------------------------------------
# Internal helpers
# ---------------------------------------------------------------------------

SeriesPoint = tuple[datetime, float]
Series = list[SeriesPoint]


def _as_datetime(value: Any) -> Optional[datetime]:
    if value is None:
        return None
    if isinstance(value, datetime):
        return value if value.tzinfo else value.replace(tzinfo=timezone.utc)
    if isinstance(value, str):
        try:
            v = value.replace("Z", "+00:00")
            dt = datetime.fromisoformat(v)
            return dt if dt.tzinfo else dt.replace(tzinfo=timezone.utc)
        except ValueError:
            return None
    return None


def _extract_series(measurements: Iterable[dict[str, Any]], field: str) -> Series:
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
    start = end - timedelta(hours=hours)
    return [(t, v) for t, v in series if start < t <= end]


def _values(series: Series) -> list[float]:
    return [v for _, v in series]


def _safe_mean(values: list[float]) -> Optional[float]:
    return statistics.fmean(values) if values else None


def _safe_stddev(values: list[float]) -> Optional[float]:
    return statistics.pstdev(values) if len(values) >= 2 else None


def _linear_slope_per_day(series: Series) -> Optional[float]:
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


def _max_drop(
    series: Series, window_minutes: int
) -> tuple[Optional[float], Optional[datetime], Optional[datetime]]:
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
        j = i + 1
    return (best, best_pair[0], best_pair[1]) if best > 0 else (None, None, None)


def _is_active_season(when: datetime) -> bool:
    return 3 <= when.month <= 9


def _is_winter(when: datetime) -> bool:
    return when.month >= 10 or when.month <= 2


# ---------------------------------------------------------------------------
# Acoustic helpers
# ---------------------------------------------------------------------------

def _latest_band(measurements: list[dict[str, Any]], field: str) -> Optional[float]:
    """
    Return the most recent non-null value of a mic band field across all
    measurements.  Returns None when no measurement carries the field.
    """
    for m in reversed(measurements):
        v = m.get(field)
        if v is not None:
            try:
                return float(v)
            except (TypeError, ValueError):
                continue
    return None


def _mic_band_snapshot(
    measurements: list[dict[str, Any]], channel: ChannelRef
) -> dict[str, Optional[float]]:
    """
    Collect the latest value of every FFT band for the given channel.
    channel=1 -> left mic, channel=2 -> right mic.
    Returns a dict with keys: sub_bass, hum, piping, stress, high (all dBFS or None).
    """
    side = "left" if channel == 1 else "right"
    return {
        "sub_bass": _latest_band(measurements, f"mic_{side}_band_sub_bass_dbfs"),
        "hum":      _latest_band(measurements, f"mic_{side}_band_hum_dbfs"),
        "piping":   _latest_band(measurements, f"mic_{side}_band_piping_dbfs"),
        "stress":   _latest_band(measurements, f"mic_{side}_band_stress_dbfs"),
        "high":     _latest_band(measurements, f"mic_{side}_band_high_dbfs"),
    }


# ---------------------------------------------------------------------------
# Detectors
# ---------------------------------------------------------------------------

def detect_imminent_swarm(
    hive_temp_series: Series,
    channel: ChannelRef,
    now: datetime,
) -> Optional[Alert]:
    """
    Phase 2 - imminent swarm (~10-30 min ahead) from brood-nest temperature.

    Source: project spec "Phase 2"; Stalidzans & Berzonis 2013.
    """
    last_1h = _window(hive_temp_series, now, hours=1)
    last_4h = _window(hive_temp_series, now, hours=4)
    if len(last_1h) < 3 or len(last_4h) < 5:
        return None

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

    Source: project spec "Phase 3" (weight half only).
    """
    last_2h = _window(weight_series, now, hours=2)
    if len(last_2h) < 4:
        return None
    drop, t_start, t_end = _max_drop(last_2h, SWARM_WEIGHT_WINDOW_MIN)
    if drop is None or drop < SWARM_WEIGHT_DROP_KG:
        return None
    hour = now.hour
    in_daytime = SWARM_DAYTIME_HOURS[0] <= hour < SWARM_DAYTIME_HOURS[1]
    severity: AlertSeverity = "critical" if in_daytime else "warning"
    return Alert(
        id=f"swarm-event-ch{channel}",
        category="swarm",
        severity=severity,
        channel=channel,
        title=f"Swarm event detected (hive {channel})",
        description=(
            f"Weight dropped {drop:.2f} kg within {SWARM_WEIGHT_WINDOW_MIN} min. "
            f"{'Daytime timing strongly suggests a swarm departure.' if in_daytime else 'Unusual time; could be a measurement artefact — investigate.'}"
        ),
        window_start=t_start,
        window_end=t_end,
        confidence=min(1.0, 0.6 + (drop - SWARM_WEIGHT_DROP_KG) * 0.1),
        evidence={"drop_kg": drop, "in_daytime": in_daytime},
        source="project spec Phase 3 (weight); Seeley 2010",
    )


def detect_pre_swarm_temp_instability(
    hive_temp_series: Series,
    channel: ChannelRef,
    now: datetime,
    measurements: Optional[list[dict[str, Any]]] = None,
) -> Optional[Alert]:
    """
    Phase 1 - pre-swarm watch from brood-nest temperature instability.

    Source: project spec "Phase 1"; MSPB arXiv 2311.10876.
    Rule: rolling 24h std-dev of hive temp exceeds the 7-day baseline std-dev
    by >= 50%.

    Acoustic enhancement (when FFT data is available):
      If the piping band (300–550 Hz) energy >= PIPING_ACTIVE_DBFS, the
      detector interprets this as a queen piping / tooting signal and:
        * raises base confidence by up to +0.35 (capped at 1.0)
        * adds acoustic_piping_dbfs to the evidence dict
      Source: Ramsey et al. (2020) PLOS ONE; Seeley (2010).
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
    if ratio < PRE_SWARM_STD_MULTIPLIER:
        return None

    # Base confidence from temperature signal alone
    base_confidence = min(1.0, 0.4 + (ratio - 1.5) * 0.5)
    evidence: dict[str, Any] = {
        "stddev_24h_c": s_now,
        "stddev_baseline_c": s_base,
        "ratio": ratio,
    }
    desc_parts = [
        f"24h brood-nest temperature variability ({s_now:.2f} degC) "
        f"is {(ratio - 1) * 100:.0f}% above the 7d baseline. "
        f"Inspect for queen cells in the next 24-48h."
    ]

    # ── Acoustic boost ───────────────────────────────────────────────────────
    piping_dbfs: Optional[float] = None
    if measurements:
        bands = _mic_band_snapshot(measurements, channel)
        piping_dbfs = bands.get("piping")

    if piping_dbfs is not None and piping_dbfs >= PIPING_ACTIVE_DBFS:
        # Piping is active — strong corroboration
        acoustic_boost = min(0.35, 0.15 + (piping_dbfs - PIPING_ACTIVE_DBFS) * 0.01)
        base_confidence = min(1.0, base_confidence + acoustic_boost)
        evidence["acoustic_piping_dbfs"] = piping_dbfs
        evidence["acoustic_piping_active"] = True
        desc_parts.append(
            f"Queen piping signal detected in the 300–550 Hz band "
            f"({piping_dbfs:.1f} dBFS) — acoustic corroboration of swarm preparation."
        )
    elif piping_dbfs is not None:
        evidence["acoustic_piping_dbfs"] = piping_dbfs
        evidence["acoustic_piping_active"] = False

    return Alert(
        id=f"swarm-watch-ch{channel}",
        category="swarm",
        severity="watch",
        channel=channel,
        title=f"Pre-swarm watch (hive {channel})",
        description=" ".join(desc_parts),
        window_start=last_baseline[0][0],
        window_end=now,
        confidence=base_confidence,
        evidence=evidence,
        source="project spec Phase 1 (temp); MSPB arXiv 2311.10876; Ramsey et al. 2020",
    )


def detect_queenlessness(
    hive_temp_series: Series,
    weight_series: Series,
    channel: ChannelRef,
    now: datetime,
    measurements: Optional[list[dict[str, Any]]] = None,
) -> Optional[Alert]:
    """
    Queenlessness - rule-based with optional acoustic corroboration.

    Source: project spec "Queenlessness detection"; MSPB / BeeTogether.

    Weight/temperature rules (always applied):
      1. 7-day hive-temp std-dev > QUEENLESS_TEMP_STDDEV_C (1.0 degC)
      2. 7-day net weight change <= QUEENLESS_WEIGHT_STAGNANT_KG (0.2 kg)

    Acoustic enhancement (when FFT data is available):
      A queenless colony's hum shifts lower and broader: the hum band
      (150–300 Hz) becomes louder while the piping band (300–550 Hz) is
      quiet (no queen to pipe).  When both acoustic conditions hold:
        * confidence rises from 0.55 → up to 0.80
        * description is updated
      Source: MSPB arXiv 2311.10876; BeeTogether queenless classifiers.

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

    stddev = _safe_stddev(_values(recent_temp))
    if stddev is None or stddev < QUEENLESS_TEMP_STDDEV_C:
        return None

    delta = recent_weight[-1][1] - recent_weight[0][1]
    if abs(delta) > QUEENLESS_WEIGHT_STAGNANT_KG:
        return None

    confidence = 0.55
    evidence: dict[str, Any] = {
        "temp_stddev_c": stddev,
        "weight_delta_kg": delta,
        "window_days": days,
    }
    desc_parts = [
        f"Over the last {days}d, hive temperature variability "
        f"({stddev:.2f} degC) suggests broodless thermoregulation, and "
        f"net weight change is only {delta:+.2f} kg during the active season. "
        f"Inspect for eggs / brood pattern."
    ]

    # ── Acoustic boost ───────────────────────────────────────────────────────
    if measurements:
        bands = _mic_band_snapshot(measurements, channel)
        hum_dbfs    = bands.get("hum")
        piping_dbfs = bands.get("piping")

        hum_elevated   = hum_dbfs    is not None and hum_dbfs    >= QUEENLESS_HUM_DBFS
        piping_quiet   = piping_dbfs is not None and piping_dbfs <  QUEENLESS_PIPING_QUIET_DBFS

        if hum_dbfs is not None:
            evidence["acoustic_hum_dbfs"] = hum_dbfs
        if piping_dbfs is not None:
            evidence["acoustic_piping_dbfs"] = piping_dbfs

        if hum_elevated and piping_quiet:
            # Both acoustic conditions match queenless signature
            confidence = min(0.80, confidence + 0.25)
            evidence["acoustic_queenless_signature"] = True
            desc_parts.append(
                f"Acoustic signature consistent with queenlessness: elevated hum band "
                f"({hum_dbfs:.1f} dBFS) with quiet piping band "
                f"({piping_dbfs:.1f} dBFS)."
            )
        elif hum_elevated or piping_quiet:
            confidence = min(0.70, confidence + 0.10)
            evidence["acoustic_queenless_signature"] = "partial"
        else:
            evidence["acoustic_queenless_signature"] = False

    return Alert(
        id=f"queenless-ch{channel}",
        category="queenless",
        severity="warning",
        channel=channel,
        title=f"Possible queenlessness (hive {channel})",
        description=" ".join(desc_parts),
        window_start=recent_temp[0][0],
        window_end=now,
        confidence=confidence,
        evidence=evidence,
        source="project spec queenless; MSPB arXiv 2311.10876; BeeTogether",
    )


def detect_robbing(
    weight_series: Series,
    channel: ChannelRef,
    now: datetime,
    measurements: Optional[list[dict[str, Any]]] = None,
) -> Optional[Alert]:
    """
    Robbing detection - rapid weight loss with optional acoustic corroboration.

    Source: project spec "Robbing detection".
    Rule: weight loss rate >= ROBBING_WEIGHT_LOSS_KG_PER_HOUR sustained for
    >= ROBBING_MIN_DURATION_MIN minutes, NOT matching a swarm signature.

    Acoustic enhancement (when FFT data is available):
      Robbing produces agitated flight noise concentrated in the stress band
      (550–1500 Hz). When stress_band_dbfs >= ROBBING_STRESS_DBFS:
        * confidence rises by +0.20 (capped at 1.0)
        * severity upgrades from "watch" → "warning" even outside the
          late-afternoon window
      Source: Nolasco et al. (2019) BUZZ dataset; project spec.

    NOTE: incoming-count spikes with low outgoing are the other canonical
    robbing signal — NOT IMPLEMENTED: requires entrance counter.
    """
    last_2h = _window(weight_series, now, hours=2)
    if len(last_2h) < 4:
        return None
    duration_h = (last_2h[-1][0] - last_2h[0][0]).total_seconds() / 3600.0
    if duration_h < ROBBING_MIN_DURATION_MIN / 60.0:
        return None
    delta = last_2h[0][1] - last_2h[-1][1]
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
    confidence = 0.5 + (0.2 if in_afternoon else 0.0)

    evidence: dict[str, Any] = {
        "rate_kg_per_h": rate_kg_per_h,
        "duration_min": duration_h * 60.0,
        "in_afternoon": in_afternoon,
    }
    desc_parts = [
        f"Sustained weight loss of {rate_kg_per_h:.2f} kg/h over "
        f"{duration_h * 60:.0f} min. "
        f"{'Late afternoon timing is consistent with dearth-period robbing.' if in_afternoon else 'Unusual time of day — investigate.'}"
    ]

    # ── Acoustic boost ───────────────────────────────────────────────────────
    if measurements:
        bands = _mic_band_snapshot(measurements, channel)
        stress_dbfs = bands.get("stress")
        if stress_dbfs is not None:
            evidence["acoustic_stress_dbfs"] = stress_dbfs
            if stress_dbfs >= ROBBING_STRESS_DBFS:
                confidence = min(1.0, confidence + 0.20)
                # Upgrade severity if acoustic confirms even outside afternoon
                if severity == "watch":
                    severity = "warning"
                evidence["acoustic_stress_active"] = True
                desc_parts.append(
                    f"Elevated agitation-band energy detected "
                    f"({stress_dbfs:.1f} dBFS in 550–1500 Hz band) — "
                    f"acoustic signature consistent with robbing activity."
                )
            else:
                evidence["acoustic_stress_active"] = False

    return Alert(
        id=f"robbing-ch{channel}",
        category="robbing",
        severity=severity,
        channel=channel,
        title=f"Possible robbing (hive {channel})",
        description=" ".join(desc_parts),
        window_start=last_2h[0][0],
        window_end=now,
        confidence=confidence,
        evidence=evidence,
        source="project spec robbing; Nolasco et al. 2019 BUZZ",
    )


def detect_foraging_intensity(
    weight_series: Series,
    channel: ChannelRef,
    now: datetime,
) -> Optional[Alert]:
    """
    Foraging intensity / nectar flow from daily weight delta.

    Source: project spec "Foraging intensity"; Meikle et al. 2008.
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
        level, severity = "negative", "watch"
    else:
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
            f"24h hive temp held at {mean:.1f}+/-{stddev:.2f} degC — "
            f"consistent with active brood thermoregulation."
        )
        severity: AlertSeverity = "info"
    elif stddev > BROOD_BROODLESS_STDDEV_C:
        title = f"Broodless / weak colony indicator (hive {channel})"
        desc = (
            f"24h hive temp variability is wide (+/-{stddev:.2f} degC, "
            f"mean {mean:.1f} degC). Suggests little or no brood being thermoregulated."
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
    Absconding / collapse early warning — compound declining trend.

    Source: project spec "Absconding / collapse early warning".
    2 of 3 rules (entrance counter NOT IMPLEMENTED).
    """
    lookback_h = 24 * ABSCONDING_LOOKBACK_DAYS
    weight_window = _window(weight_series, now, hours=lookback_h)
    temp_window = _window(hive_temp_series, now, hours=lookback_h)
    if len(weight_window) < 14 or len(temp_window) < 14:
        return None

    weight_slope = _linear_slope_per_day(weight_window)
    if weight_slope is None:
        return None
    weight_loss_g_per_day = -weight_slope * 1000.0
    if weight_loss_g_per_day < ABSCONDING_WEIGHT_LOSS_G_PER_DAY:
        return None

    # Compute rolling 24h std-dev series and its slope
    daily_stddev: Series = []
    step = timedelta(hours=6)
    cursor = weight_window[0][0] + timedelta(hours=24)
    while cursor <= now:
        seg = _window(temp_window, cursor, hours=24)
        sd = _safe_stddev(_values(seg))
        if sd is not None:
            daily_stddev.append((cursor, sd))
        cursor += step

    if len(daily_stddev) < 4:
        return None
    stddev_slope = _linear_slope_per_day(daily_stddev)
    if stddev_slope is None or stddev_slope <= 0:
        return None

    return Alert(
        id=f"absconding-ch{channel}",
        category="decline",
        severity="watch",
        channel=channel,
        title=f"Absconding / collapse risk (hive {channel})",
        description=(
            f"Weight loss of {weight_loss_g_per_day:.0f} g/day sustained "
            f"over {ABSCONDING_LOOKBACK_DAYS}d, combined with widening "
            f"temperature variability (slope +{stddev_slope:.3f} degC/day). "
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
    Winter survival risk (Oct-Feb) from cluster temperature and weight loss.

    Source: project spec "Winter survival risk".
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
    Honey-ready / harvest timing — plateau after sustained gain.

    Source: project spec "Honey-ready / harvest timing".
    """
    last_11d = _window(weight_series, now, hours=24 * 11)
    if len(last_11d) < 24:
        return None

    win_now = _window(weight_series, now, hours=24 * 7)
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
                f"The flow appears finished — supers may be ready to harvest."
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
    Mic and FFT band fields are optional and consumed transparently.
    """
    now = now or datetime.now(timezone.utc)
    if now.tzinfo is None:
        now = now.replace(tzinfo=timezone.utc)

    ambient = _extract_series(measurements, "ambient_temp_c")
    alerts: list[Alert] = []

    for channel, weight_field, temp_field in (
        (1, "scale_1_weight_kg", "hive_1_temp_c"),
        (2, "scale_2_weight_kg", "hive_2_temp_c"),
    ):
        weight = _extract_series(measurements, weight_field)
        hive_temp = _extract_series(measurements, temp_field)

        if not weight and not hive_temp:
            continue

        # Detectors that accept acoustic data get ``measurements`` passed in.
        for detector in (
            lambda: detect_imminent_swarm(hive_temp, channel, now),
            lambda: detect_swarm_event(weight, channel, now),
            lambda: detect_pre_swarm_temp_instability(hive_temp, channel, now, measurements),
            lambda: detect_queenlessness(hive_temp, weight, channel, now, measurements),
            lambda: detect_robbing(weight, channel, now, measurements),
            lambda: detect_foraging_intensity(weight, channel, now),
            lambda: detect_brood_cycle_state(hive_temp, channel, now),
            lambda: detect_absconding_trend(hive_temp, weight, channel, now),
            lambda: detect_winter_risk(hive_temp, ambient, weight, channel, now),
            lambda: detect_harvest_window(weight, channel, now),
        ):
            try:
                alert = detector()
            except Exception:
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