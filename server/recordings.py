"""On-request audio from a hive's HiveInside node.

A HiveInside 0.6.0 node can be asked for live audio: an authenticated request
makes it capture 16 kHz PCM16 and push it out over BLE, and the HiveHub relays
that stream here. This module owns the recording's whole life:

    request  -> a queued device command + a `requested` row
    stream   -> the hub POSTs a chunked body that is written to disk as it lands
    finalize -> the hub reports byte count, CRC and quality counters
    play     -> the browser reads it live (raw PCM slices) or later (a WAV file)

Two decisions shape everything here.

**Raw PCM on disk, WAV on the way out.** The file is exactly what the node sent:
16 kHz, 16-bit little-endian, mono. A `.wav` download is that file behind a
44-byte header generated per request. Storing WAV instead would mean either
rewriting the header every time the file grows (it grows while a live session is
in flight) or storing the audio twice.

**A live session is readable before it is finished.** The dashboard polls
`/pcm?offset=` for whatever has landed so far and plays it through the Web Audio
API. That is why the upload writes and flushes incrementally rather than
buffering: a recording still being written is a normal, useful thing to read.

Recordings are kept until somebody deletes them — no retention sweep. See
docs/audio-recording.md.
"""

import struct
from pathlib import Path
from typing import Optional

from fastapi import APIRouter, Depends, HTTPException, Query, Request
from fastapi.responses import Response, StreamingResponse

from auth import require_api_key, require_device_key
from commands import create_command
from config import MAX_RECORDING_BYTES, RECORDINGS_DIR, logger
from db import get_conn
from devices import ensure_device_config
from schemas import MAX_HIVES, DeviceCommandIn, RecordingFinalizeIn

router = APIRouter()

# The node only ever produces this format; it is recorded per row anyway so a
# future rate change cannot silently mis-play everything already stored.
SAMPLE_RATE = 16000
BYTES_PER_SAMPLE = 2
CHANNELS = 1

# The node caps a session at 60 s and the hub caps its relay at the same. A
# request for more is a mistake somewhere, so refuse it rather than queue a
# command that cannot be honoured.
MAX_DURATION_S = 60

# How much of a live recording the player may fetch in one poll. Large enough
# that a 250 ms poll never falls behind 32 kB/s, small enough to stay a normal
# JSON-sized response.
MAX_PCM_SLICE = 256 * 1024


def _path_for(device_id: str, recording_id: int) -> Path:
    """Where a recording's audio lives.

    The filename is derived from ids the database issued, never from anything a
    client sent, so there is no path to traverse out of. The device id is still
    sanitised because it reaches us from a device-authenticated header and ends
    up as a directory name.
    """
    safe_device = "".join(c for c in device_id if c.isalnum() or c in "-_")
    if not safe_device:
        raise HTTPException(status_code=400, detail="invalid device id")
    return RECORDINGS_DIR / safe_device / f"{int(recording_id)}.pcm"


def _row_to_dict(r) -> dict:
    """One recording row as the API presents it.

    `complete` is computed rather than stored: it is the single question a
    listener actually has — is what I am about to hear the whole thing? — and it
    depends on three separate columns agreeing.
    """
    (rec_id, device_id, hive_index, status, requested_at, started_at,
     completed_at, duration_ds, gain_db, sample_rate, num_bytes, crc32,
     device_bytes, device_crc32, dropped_bytes, gaps, clipped_pct, error,
     requested_by) = r
    seconds = (num_bytes or 0) / float(sample_rate * BYTES_PER_SAMPLE) if sample_rate else 0.0
    complete = (
        status == "ready"
        and not dropped_bytes
        and not gaps
        and (device_crc32 is None or crc32 is None or device_crc32 == crc32)
    )
    return {
        "id": rec_id,
        "device_id": device_id,
        "hive_index": hive_index,
        "status": status,
        "requested_at": requested_at,
        "started_at": started_at,
        "completed_at": completed_at,
        "requested_duration_s": (duration_ds or 0) / 10.0,
        "gain_db": gain_db,
        "sample_rate": sample_rate,
        "bytes": num_bytes,
        "seconds": round(seconds, 2),
        "dropped_bytes": dropped_bytes,
        "gaps": gaps,
        "clipped_pct": clipped_pct,
        "complete": complete,
        "crc_ok": (crc32 is not None and device_crc32 is not None
                   and crc32 == device_crc32),
        "error": error,
        "requested_by": requested_by,
    }


_SELECT = """
    SELECT id, device_id, hive_index, status, requested_at, started_at,
           completed_at, duration_ds, gain_db, sample_rate, bytes, crc32,
           device_bytes, device_crc32, dropped_bytes, gaps, clipped_pct, error,
           requested_by
    FROM hive_recordings
"""


def get_recording(recording_id: int) -> dict:
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(_SELECT + " WHERE id = %s;", (recording_id,))
            r = cur.fetchone()
    if not r:
        raise HTTPException(status_code=404, detail="recording not found")
    return _row_to_dict(r)


def list_recordings(device_id: str, hive_index: Optional[int] = None,
                    limit: int = 50) -> list[dict]:
    sql = _SELECT + " WHERE device_id = %s"
    params: list = [device_id]
    if hive_index is not None:
        sql += " AND hive_index = %s"
        params.append(hive_index)
    sql += " ORDER BY requested_at DESC LIMIT %s;"
    params.append(max(1, min(limit, 500)))
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(sql, tuple(params))
            rows = cur.fetchall()
    return [_row_to_dict(r) for r in rows]


def request_recording(device_id: str, hive_index: int, duration_s: float,
                      gain_db: int = 0, requested_by: Optional[str] = None) -> dict:
    """Create the row and queue the command that fills it.

    Returns immediately — the hub only picks the command up on its next wake, so
    the caller gets a `requested` recording and polls it. Live listening is a
    sequence of these: each one is capped at 60 s by the node, and "continue
    listening" queues the next.
    """
    if not 1 <= hive_index <= MAX_HIVES:
        raise HTTPException(status_code=400,
                            detail=f"hive_index must be between 1 and {MAX_HIVES}")
    if duration_s < 0 or duration_s > MAX_DURATION_S:
        raise HTTPException(
            status_code=400,
            detail=f"duration must be between 0 (live) and {MAX_DURATION_S} seconds",
        )
    gain_db = max(-20, min(20, int(gain_db)))
    ensure_device_config(device_id)

    duration_ds = int(round(duration_s * 10))
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                INSERT INTO hive_recordings
                    (device_id, hive_index, duration_ds, gain_db, sample_rate,
                     requested_by)
                VALUES (%s, %s, %s, %s, %s, %s)
                RETURNING id;
                """,
                (device_id, hive_index, duration_ds, gain_db, SAMPLE_RATE,
                 requested_by),
            )
            recording_id = cur.fetchone()[0]
            conn.commit()

    # The command carries no URL: the hub derives the upload target from its own
    # device id. A hub that posted microphone audio to whatever address a queued
    # command named would be one malformed command away from streaming a garden
    # to a stranger.
    #
    # The row exists before the command does, because the command has to carry
    # its id. If queueing then fails, that row would sit at "requested" forever
    # waiting for a hub that was never told anything — indistinguishable from a
    # hub that is merely asleep. So mark it failed and say why, rather than
    # leaving a request nothing will ever answer.
    try:
        cmd = create_command(device_id, DeviceCommandIn(
            command_type="record_audio",
            payload={
                "slot": hive_index,
                "recording_id": recording_id,
                "duration_ds": duration_ds,
                "gain_db": gain_db,
            },
        ))
    except Exception as exc:
        logger.exception("could not queue record_audio for recording %s", recording_id)
        with get_conn() as conn:
            with conn.cursor() as cur:
                cur.execute(
                    "UPDATE hive_recordings SET status = 'failed', completed_at = now(), "
                    "error = %s WHERE id = %s;",
                    (f"could not queue the command for the hub: {exc}", recording_id),
                )
                conn.commit()
        raise HTTPException(
            status_code=500,
            detail="the recording could not be queued for the hub",
        ) from exc
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute("UPDATE hive_recordings SET command_id = %s WHERE id = %s;",
                        (cmd["id"], recording_id))
            conn.commit()
    return {"id": recording_id, "status": "requested", "command_id": cmd["id"],
            "hive_index": hive_index, "duration_s": duration_ds / 10.0}


def delete_recording(recording_id: int) -> dict:
    """Remove the row and its audio. Idempotent enough to be safe to retry."""
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute("SELECT device_id FROM hive_recordings WHERE id = %s;",
                        (recording_id,))
            r = cur.fetchone()
            if not r:
                raise HTTPException(status_code=404, detail="recording not found")
            device_id = r[0]
            cur.execute("DELETE FROM hive_recordings WHERE id = %s;", (recording_id,))
            conn.commit()
    path = _path_for(device_id, recording_id)
    try:
        path.unlink(missing_ok=True)
    except OSError as exc:  # pragma: no cover - disk-level failure
        logger.warning("could not delete recording file %s: %s", path, exc)
    return {"status": "deleted", "id": recording_id}


def wav_header(data_bytes: int, sample_rate: int = SAMPLE_RATE) -> bytes:
    """A 44-byte canonical PCM WAV header for `data_bytes` of audio.

    Generated per request rather than stored, so the same file on disk serves
    both the live PCM reader and a .wav download, and a growing recording never
    needs its header rewritten.
    """
    byte_rate = sample_rate * CHANNELS * BYTES_PER_SAMPLE
    block_align = CHANNELS * BYTES_PER_SAMPLE
    return b"RIFF" + struct.pack("<I", 36 + data_bytes) + b"WAVEfmt " + \
        struct.pack("<IHHIIHH", 16, 1, CHANNELS, sample_rate, byte_rate,
                    block_align, BYTES_PER_SAMPLE * 8) + \
        b"data" + struct.pack("<I", data_bytes)


# ── Device-authenticated ingest ─────────────────────────────────────────────

@router.post("/api/v1/devices/{device_id}/recordings/{recording_id}/stream",
             dependencies=[Depends(require_device_key)])
async def stream_recording(device_id: str, recording_id: int, request: Request):
    """Receive a live audio stream from the hub and write it to disk as it lands.

    The body arrives chunked with no declared length — a live session's size is
    not known until it ends — so it is consumed incrementally and flushed after
    every chunk. Flushing matters: the dashboard reads this file *while it is
    being written*, and buffered writes would make live playback lag by however
    much the OS felt like holding.
    """
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                "SELECT status FROM hive_recordings WHERE id = %s AND device_id = %s;",
                (recording_id, device_id),
            )
            r = cur.fetchone()
            if not r:
                raise HTTPException(status_code=404, detail="recording not found")
            cur.execute(
                "UPDATE hive_recordings SET status = 'streaming', started_at = now() "
                "WHERE id = %s;",
                (recording_id,),
            )
            conn.commit()

    path = _path_for(device_id, recording_id)
    path.parent.mkdir(parents=True, exist_ok=True)

    written = 0
    truncated = False
    with open(path, "wb") as fh:
        async for chunk in request.stream():
            if not chunk:
                continue
            if written + len(chunk) > MAX_RECORDING_BYTES:
                # Keep what fits and stop reading. The node bounds itself at 60 s,
                # so this only fires for a hub that has gone wrong — and half a
                # recording plus an explicit note beats an unbounded write.
                chunk = chunk[: max(0, MAX_RECORDING_BYTES - written)]
                truncated = True
            if chunk:
                fh.write(chunk)
                fh.flush()
                written += len(chunk)
            if truncated:
                break

    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                "UPDATE hive_recordings SET bytes = %s, filename = %s WHERE id = %s;",
                (written, path.name, recording_id),
            )
            if truncated:
                cur.execute(
                    "UPDATE hive_recordings SET error = %s WHERE id = %s;",
                    (f"truncated at the server cap of {MAX_RECORDING_BYTES} bytes",
                     recording_id),
                )
            conn.commit()
    logger.info("recording %s: received %s bytes%s", recording_id, written,
                " (truncated)" if truncated else "")
    return {"status": "ok", "bytes": written}


@router.post("/api/v1/devices/{device_id}/recordings/{recording_id}/finalize",
             dependencies=[Depends(require_device_key)])
def finalize_recording(device_id: str, recording_id: int,
                       payload: RecordingFinalizeIn):
    """Record how the session actually went.

    Called even when it went badly, and that is the point: a row that never
    hears back is indistinguishable from a hub that died mid-session, and the
    dashboard would show "recording…" forever.
    """
    status = "ready" if payload.ok else "failed"
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                UPDATE hive_recordings
                SET status = %s,
                    completed_at = now(),
                    crc32 = %s,
                    device_bytes = %s,
                    device_crc32 = %s,
                    dropped_bytes = %s,
                    gaps = %s,
                    clipped_pct = %s,
                    device_error = %s,
                    sample_rate = COALESCE(%s, sample_rate),
                    error = COALESCE(%s, error)
                WHERE id = %s AND device_id = %s;
                """,
                (status, payload.crc32, payload.device_bytes, payload.device_crc32,
                 payload.dropped_bytes, payload.gaps, payload.clipped_pct,
                 payload.device_error, payload.sample_rate, payload.error,
                 recording_id, device_id),
            )
            if cur.rowcount == 0:
                raise HTTPException(status_code=404, detail="recording not found")
            conn.commit()
    return {"status": status}


# ── Playback ────────────────────────────────────────────────────────────────

def recording_pcm(recording_id: int, offset: int, limit: int) -> Response:
    """Raw PCM from `offset`, for the live player.

    Returns 204 when there is nothing new yet rather than an empty 200: the
    player polls this several times a second while a session is in flight, and
    "no new audio yet" is the common case, not an error.

    `X-Recording-Status` and `X-Recording-Offset` let the player know when to
    stop polling without a second request.
    """
    rec = get_recording(recording_id)
    path = _path_for(rec["device_id"], recording_id)
    if not path.exists():
        raise HTTPException(status_code=404, detail="no audio for this recording")
    size = path.stat().st_size
    offset = max(0, offset)
    limit = max(1, min(limit, MAX_PCM_SLICE))
    headers = {
        "X-Recording-Status": rec["status"],
        "X-Recording-Size": str(size),
        "X-Recording-Offset": str(offset),
        "Cache-Control": "no-store",
    }
    if offset >= size:
        return Response(status_code=204, headers=headers)
    with open(path, "rb") as fh:
        fh.seek(offset)
        data = fh.read(limit)
    headers["X-Recording-Next-Offset"] = str(offset + len(data))
    return Response(content=data, media_type="application/octet-stream",
                    headers=headers)


def recording_wav(recording_id: int) -> StreamingResponse:
    """The finished recording as a downloadable/playable WAV.

    The header is generated here and the payload streamed from disk, so nothing
    the size of a recording is ever held in memory.
    """
    rec = get_recording(recording_id)
    path = _path_for(rec["device_id"], recording_id)
    if not path.exists():
        raise HTTPException(status_code=404, detail="no audio for this recording")
    size = path.stat().st_size
    header = wav_header(size, rec["sample_rate"] or SAMPLE_RATE)

    def body():
        yield header
        with open(path, "rb") as fh:
            while True:
                block = fh.read(64 * 1024)
                if not block:
                    break
                yield block

    return StreamingResponse(
        body(),
        media_type="audio/wav",
        headers={
            "Content-Length": str(len(header) + size),
            "Content-Disposition":
                f'inline; filename="hive{rec["hive_index"]}-recording-{recording_id}.wav"',
            "Cache-Control": "no-store",
        },
    )


# ── Master-key API (server-to-server / tooling) ─────────────────────────────

@router.post("/api/v1/devices/{device_id}/commands/record-audio",
             dependencies=[Depends(require_api_key)])
def queue_recording(device_id: str, slot: int = Query(1),
                    duration: float = Query(0.0),
                    gain_db: int = Query(0)):
    """Ask a hive for audio. `duration=0` is a live session (node-capped at 60 s)."""
    return request_recording(device_id, slot, duration, gain_db)


@router.get("/api/v1/devices/{device_id}/recordings",
            dependencies=[Depends(require_api_key)])
def list_device_recordings(device_id: str, hive: Optional[int] = Query(None),
                           limit: int = Query(50)):
    return {"recordings": list_recordings(device_id, hive, limit)}


@router.get("/api/v1/recordings/{recording_id}",
            dependencies=[Depends(require_api_key)])
def read_recording(recording_id: int):
    return get_recording(recording_id)


@router.get("/api/v1/recordings/{recording_id}/pcm",
            dependencies=[Depends(require_api_key)])
def read_recording_pcm(recording_id: int, offset: int = Query(0),
                       limit: int = Query(MAX_PCM_SLICE)):
    return recording_pcm(recording_id, offset, limit)


@router.get("/api/v1/recordings/{recording_id}/audio.wav",
            dependencies=[Depends(require_api_key)])
def read_recording_wav(recording_id: int):
    return recording_wav(recording_id)


@router.delete("/api/v1/recordings/{recording_id}",
               dependencies=[Depends(require_api_key)])
def remove_recording(recording_id: int):
    return delete_recording(recording_id)
