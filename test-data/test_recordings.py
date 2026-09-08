"""Tests for hive audio recordings (issue #71).

Run: PYTHONPATH=server python3 test-data/test_recordings.py
     (no database or FastAPI server required)

These cover the parts that are pure functions of their inputs — the WAV header
the playback endpoints synthesize, the storage path, and the row-to-API shaping
that decides whether a listener is told a recording is complete. The HTTP
plumbing needs a database and is exercised by hand against a real deployment.

The `complete` flag gets the most attention here on purpose. A recording with a
hole in it still plays as continuous audio, so a bug that reported an incomplete
recording as complete would produce a confident, wrong acoustic judgement and
leave no trace of why.
"""

import os
import struct
import sys
from datetime import datetime, timezone

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "server"))

# recordings.py pulls in auth -> config, which reads these at import time. No
# database, network or disk is touched: everything checked below is a pure
# function of its arguments. RECORDINGS_DIR is pinned too, so the path
# assertions describe the code rather than whatever the deployment default
# happens to be.
os.environ.setdefault("DATABASE_URL", "postgresql://localhost/test")
os.environ.setdefault("API_KEY", "test-api-key")
os.environ.setdefault("JWT_SECRET", "test-jwt-secret")
os.environ.setdefault("RECORDINGS_DIR", "/tmp/hivehub-test-recordings")

from recordings import (  # noqa: E402
    SAMPLE_RATE,
    _path_for,
    _row_to_dict,
    wav_header,
)

NOW = datetime(2026, 9, 8, 14, 3, tzinfo=timezone.utc)

_failures = 0


def check(name, condition):
    global _failures
    status = "ok" if condition else "FAIL"
    if not condition:
        _failures += 1
    print(f"[{status}] {name}")


def row(**over):
    """A recording row in the column order recordings._SELECT returns."""
    base = dict(
        rec_id=7, device_id="hive-a", hive_index=2, status="ready",
        requested_at=NOW, started_at=NOW, completed_at=NOW,
        duration_ds=0, gain_db=0, sample_rate=SAMPLE_RATE,
        num_bytes=32000, crc32=123456, device_bytes=32000, device_crc32=123456,
        dropped_bytes=0, gaps=0, clipped_pct=0, error=None, requested_by="max",
    )
    base.update(over)
    return tuple(base.values())


# ── WAV header ───────────────────────────────────────────────────────────────
#
# Generated per request rather than stored, so a growing live recording never
# needs its header rewritten. That only works if it is exactly right.

h = wav_header(32000)
check("header is the canonical 44 bytes", len(h) == 44)
check("header starts with RIFF", h[0:4] == b"RIFF")
check("RIFF size counts everything after the first 8 bytes",
      struct.unpack("<I", h[4:8])[0] == 36 + 32000)
check("format is WAVE/fmt ", h[8:16] == b"WAVEfmt ")
check("fmt chunk is 16 bytes of PCM", struct.unpack("<I", h[16:20])[0] == 16)
check("audio format is uncompressed PCM", struct.unpack("<H", h[20:22])[0] == 1)
check("one channel", struct.unpack("<H", h[22:24])[0] == 1)
check("16 kHz sample rate", struct.unpack("<I", h[24:28])[0] == 16000)
check("byte rate is rate * channels * 2", struct.unpack("<I", h[28:32])[0] == 32000)
check("block align is 2", struct.unpack("<H", h[32:34])[0] == 2)
check("16 bits per sample", struct.unpack("<H", h[34:36])[0] == 16)
check("data chunk declares the payload size",
      h[36:40] == b"data" and struct.unpack("<I", h[40:44])[0] == 32000)

# A zero-length recording still has to produce a valid (silent) file rather than
# a truncated one: a failed session is listed alongside good ones.
h0 = wav_header(0)
check("an empty recording still yields a valid header",
      len(h0) == 44 and struct.unpack("<I", h0[40:44])[0] == 0)

check("a non-default sample rate is carried into the header",
      struct.unpack("<I", wav_header(100, 8000)[24:28])[0] == 8000)


# ── storage path ─────────────────────────────────────────────────────────────
#
# The filename comes from ids the database issued, but the device id arrives in
# a request header, so it is the part that has to be defanged.

p = _path_for("hive-a", 7)
check("path is <device>/<id>.pcm", p.parent.name == "hive-a" and p.name == "7.pcm")
check("a device id cannot traverse out of the recordings directory",
      ".." not in str(_path_for("../../etc/passwd", 1)))
check("separators are stripped from the device id",
      "/" not in _path_for("../../etc/passwd", 1).parent.name)

try:
    _path_for("///", 1)
    check("an entirely unusable device id is rejected", False)
except Exception:
    check("an entirely unusable device id is rejected", True)


# ── what a listener is told ──────────────────────────────────────────────────

d = _row_to_dict(row())
check("a clean ready recording is complete", d["complete"] is True)
check("seconds are derived from bytes and sample rate", d["seconds"] == 1.0)
check("matching CRCs report crc_ok", d["crc_ok"] is True)

check("audio the node dropped makes it incomplete",
      _row_to_dict(row(dropped_bytes=480))["complete"] is False)
check("a gap in transit makes it incomplete",
      _row_to_dict(row(gaps=1))["complete"] is False)
check("a CRC mismatch makes it incomplete",
      _row_to_dict(row(device_crc32=999))["complete"] is False)
check("a CRC mismatch is reported as such",
      _row_to_dict(row(device_crc32=999))["crc_ok"] is False)
check("a recording still streaming is not yet complete",
      _row_to_dict(row(status="streaming"))["complete"] is False)
check("a failed recording is not complete",
      _row_to_dict(row(status="failed", error="node busy"))["complete"] is False)
check("a failure reason is passed through",
      _row_to_dict(row(status="failed", error="node busy"))["error"] == "node busy")

# An older row can predate the CRC columns. Unknown must not read as "verified".
unknown = _row_to_dict(row(crc32=None, device_crc32=None))
check("an unverifiable recording is not claimed to be checksum-verified",
      unknown["crc_ok"] is False)
check("an unverifiable but otherwise clean recording is still offered as complete",
      unknown["complete"] is True)

# A live session is requested with duration 0; the UI shows what was asked for.
check("an open-ended request reports zero requested duration",
      _row_to_dict(row(duration_ds=0))["requested_duration_s"] == 0.0)
check("a fixed request reports its duration in seconds",
      _row_to_dict(row(duration_ds=105))["requested_duration_s"] == 10.5)

# Bytes are what landed, and a partial recording must still report honestly.
partial = _row_to_dict(row(num_bytes=16000, device_bytes=32000, gaps=2))
check("seconds reflect what arrived, not what was expected", partial["seconds"] == 0.5)
check("a partial recording is not complete", partial["complete"] is False)


if _failures:
    raise SystemExit(f"{_failures} check(s) failed")
print("\nAll recording checks passed.")
