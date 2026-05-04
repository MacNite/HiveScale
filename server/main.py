import os
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional, Literal, Any

import psycopg
from fastapi import Depends, FastAPI, Header, HTTPException, Query
from fastapi.responses import FileResponse
from pydantic import BaseModel, Field

DATABASE_URL = os.environ["DATABASE_URL"]
API_KEY = os.environ["API_KEY"]
PUBLIC_BASE_URL = os.environ.get("PUBLIC_BASE_URL", "").rstrip("/")
FIRMWARE_DIR = Path(os.environ.get("FIRMWARE_DIR", "/app/firmware"))

app = FastAPI(
    title="HiveScale API",
    description="HTTP endpoint for ESP32-based dual hive scales.",
    version="0.3.0",
)


class MeasurementIn(BaseModel):
    device_id: str = Field(..., examples=["hive_scale_dual_01"])
    timestamp: Optional[datetime] = None
    scale_1_weight_kg: Optional[float] = None
    scale_2_weight_kg: Optional[float] = None
    hive_1_temp_c: Optional[float] = None
    hive_2_temp_c: Optional[float] = None
    ambient_temp_c: Optional[float] = None
    ambient_humidity_percent: Optional[float] = None
    battery_voltage: Optional[float] = None
    rssi_dbm: Optional[int] = None
    firmware_version: Optional[str] = None
    config_version: Optional[int] = None
    sd_ok: Optional[bool] = None
    rtc_ok: Optional[bool] = None
    sht_ok: Optional[bool] = None
    scale_1_raw: Optional[int] = None
    scale_2_raw: Optional[int] = None


class DeviceConfig(BaseModel):
    device_id: str
    send_interval_seconds: int = 600
    scale1_offset: int = 0
    scale1_factor: float = -7050.0
    scale2_offset: int = 0
    scale2_factor: float = -7050.0
    config_version: int = 1


class DeviceConfigUpdate(BaseModel):
    send_interval_seconds: Optional[int] = None
    scale1_offset: Optional[int] = None
    scale1_factor: Optional[float] = None
    scale2_offset: Optional[int] = None
    scale2_factor: Optional[float] = None


class FirmwareReleaseIn(BaseModel):
    version: str
    filename: str
    active: bool = True


class DeviceCommandIn(BaseModel):
    command_type: Literal[
        "tare_scale_1",
        "tare_scale_2",
        "calibrate_scale_1",
        "calibrate_scale_2",
        "reboot",
    ]
    payload: dict[str, Any] = Field(default_factory=dict)


class DeviceCommandResult(BaseModel):
    success: bool
    message: Optional[str] = None
    result: dict[str, Any] = Field(default_factory=dict)


def require_api_key(x_api_key: str = Header(default="")):
    if x_api_key != API_KEY:
        raise HTTPException(status_code=401, detail="Invalid API key")


def get_conn():
    return psycopg.connect(DATABASE_URL)


def init_db():
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                CREATE TABLE IF NOT EXISTS measurements (
                    id BIGSERIAL PRIMARY KEY,
                    device_id TEXT NOT NULL,
                    measured_at TIMESTAMPTZ NOT NULL,
                    received_at TIMESTAMPTZ NOT NULL DEFAULT now(),
                    scale_1_weight_kg DOUBLE PRECISION,
                    scale_2_weight_kg DOUBLE PRECISION,
                    hive_1_temp_c DOUBLE PRECISION,
                    hive_2_temp_c DOUBLE PRECISION,
                    ambient_temp_c DOUBLE PRECISION,
                    ambient_humidity_percent DOUBLE PRECISION,
                    battery_voltage DOUBLE PRECISION,
                    rssi_dbm INTEGER,
                    firmware_version TEXT,
                    config_version INTEGER,
                    sd_ok BOOLEAN,
                    rtc_ok BOOLEAN,
                    sht_ok BOOLEAN,
                    scale_1_raw BIGINT,
                    scale_2_raw BIGINT,
                    raw_json JSONB NOT NULL
                );

                ALTER TABLE measurements ADD COLUMN IF NOT EXISTS firmware_version TEXT;
                ALTER TABLE measurements ADD COLUMN IF NOT EXISTS config_version INTEGER;
                ALTER TABLE measurements ADD COLUMN IF NOT EXISTS sd_ok BOOLEAN;
                ALTER TABLE measurements ADD COLUMN IF NOT EXISTS rtc_ok BOOLEAN;
                ALTER TABLE measurements ADD COLUMN IF NOT EXISTS sht_ok BOOLEAN;
                ALTER TABLE measurements ADD COLUMN IF NOT EXISTS scale_1_raw BIGINT;
                ALTER TABLE measurements ADD COLUMN IF NOT EXISTS scale_2_raw BIGINT;

                CREATE INDEX IF NOT EXISTS idx_measurements_device_time
                    ON measurements (device_id, measured_at DESC);

                CREATE TABLE IF NOT EXISTS device_configs (
                    device_id TEXT PRIMARY KEY,
                    send_interval_seconds INTEGER NOT NULL DEFAULT 600,
                    scale1_offset BIGINT NOT NULL DEFAULT 0,
                    scale1_factor DOUBLE PRECISION NOT NULL DEFAULT -7050.0,
                    scale2_offset BIGINT NOT NULL DEFAULT 0,
                    scale2_factor DOUBLE PRECISION NOT NULL DEFAULT -7050.0,
                    config_version INTEGER NOT NULL DEFAULT 1,
                    updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
                );

                CREATE TABLE IF NOT EXISTS firmware_releases (
                    id BIGSERIAL PRIMARY KEY,
                    version TEXT NOT NULL UNIQUE,
                    filename TEXT NOT NULL,
                    active BOOLEAN NOT NULL DEFAULT true,
                    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
                );

                CREATE TABLE IF NOT EXISTS device_commands (
                    id BIGSERIAL PRIMARY KEY,
                    device_id TEXT NOT NULL,
                    command_type TEXT NOT NULL,
                    payload JSONB NOT NULL DEFAULT '{}'::jsonb,
                    status TEXT NOT NULL DEFAULT 'pending',
                    result JSONB,
                    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
                    claimed_at TIMESTAMPTZ,
                    completed_at TIMESTAMPTZ
                );

                CREATE INDEX IF NOT EXISTS idx_device_commands_pending
                    ON device_commands (device_id, status, created_at);
                """
            )
            conn.commit()


def ensure_device_config(device_id: str):
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                INSERT INTO device_configs (device_id)
                VALUES (%s)
                ON CONFLICT (device_id) DO NOTHING;
                """,
                (device_id,),
            )
            conn.commit()


def version_tuple(v: str) -> tuple[int, ...]:
    clean = v.strip().lstrip("v")
    parts = []
    for p in clean.split("."):
        try:
            parts.append(int("".join(ch for ch in p if ch.isdigit()) or "0"))
        except ValueError:
            parts.append(0)
    return tuple(parts)


@app.on_event("startup")
def startup():
    FIRMWARE_DIR.mkdir(parents=True, exist_ok=True)
    init_db()


@app.get("/health")
def health():
    return {"status": "ok"}


@app.post("/api/v1/measurements", dependencies=[Depends(require_api_key)])
def create_measurement(payload: MeasurementIn):
    measured_at = payload.timestamp or datetime.now(timezone.utc)
    ensure_device_config(payload.device_id)
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                INSERT INTO measurements (
                    device_id, measured_at, scale_1_weight_kg, scale_2_weight_kg,
                    hive_1_temp_c, hive_2_temp_c, ambient_temp_c,
                    ambient_humidity_percent, battery_voltage, rssi_dbm,
                    firmware_version, config_version, sd_ok, rtc_ok, sht_ok,
                    scale_1_raw, scale_2_raw, raw_json
                )
                VALUES (
                    %(device_id)s, %(measured_at)s, %(scale_1_weight_kg)s,
                    %(scale_2_weight_kg)s, %(hive_1_temp_c)s, %(hive_2_temp_c)s,
                    %(ambient_temp_c)s, %(ambient_humidity_percent)s,
                    %(battery_voltage)s, %(rssi_dbm)s, %(firmware_version)s,
                    %(config_version)s, %(sd_ok)s, %(rtc_ok)s, %(sht_ok)s,
                    %(scale_1_raw)s, %(scale_2_raw)s, %(raw_json)s
                )
                RETURNING id;
                """,
                {
                    "device_id": payload.device_id,
                    "measured_at": measured_at,
                    "scale_1_weight_kg": payload.scale_1_weight_kg,
                    "scale_2_weight_kg": payload.scale_2_weight_kg,
                    "hive_1_temp_c": payload.hive_1_temp_c,
                    "hive_2_temp_c": payload.hive_2_temp_c,
                    "ambient_temp_c": payload.ambient_temp_c,
                    "ambient_humidity_percent": payload.ambient_humidity_percent,
                    "battery_voltage": payload.battery_voltage,
                    "rssi_dbm": payload.rssi_dbm,
                    "firmware_version": payload.firmware_version,
                    "config_version": payload.config_version,
                    "sd_ok": payload.sd_ok,
                    "rtc_ok": payload.rtc_ok,
                    "sht_ok": payload.sht_ok,
                    "scale_1_raw": payload.scale_1_raw,
                    "scale_2_raw": payload.scale_2_raw,
                    "raw_json": payload.model_dump_json(),
                },
            )
            new_id = cur.fetchone()[0]
            conn.commit()
    return {"status": "ok", "id": new_id, "measured_at": measured_at.isoformat()}


@app.get("/api/v1/measurements/latest")
def latest_measurements(limit: int = 50):
    limit = min(max(limit, 1), 500)
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT id, device_id, measured_at, received_at, scale_1_weight_kg,
                       scale_2_weight_kg, hive_1_temp_c, hive_2_temp_c,
                       ambient_temp_c, ambient_humidity_percent, battery_voltage,
                       rssi_dbm, firmware_version, config_version, sd_ok, rtc_ok, sht_ok,
                       scale_1_raw, scale_2_raw
                FROM measurements
                ORDER BY measured_at DESC
                LIMIT %s;
                """,
                (limit,),
            )
            rows = cur.fetchall()
    return [
        {
            "id": r[0], "device_id": r[1], "measured_at": r[2], "received_at": r[3],
            "scale_1_weight_kg": r[4], "scale_2_weight_kg": r[5],
            "hive_1_temp_c": r[6], "hive_2_temp_c": r[7],
            "ambient_temp_c": r[8], "ambient_humidity_percent": r[9],
            "battery_voltage": r[10], "rssi_dbm": r[11], "firmware_version": r[12],
            "config_version": r[13], "sd_ok": r[14], "rtc_ok": r[15], "sht_ok": r[16],
            "scale_1_raw": r[17], "scale_2_raw": r[18],
        }
        for r in rows
    ]


@app.get("/api/v1/devices/{device_id}/config", dependencies=[Depends(require_api_key)])
def get_device_config(device_id: str):
    ensure_device_config(device_id)
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT device_id, send_interval_seconds, scale1_offset, scale1_factor,
                       scale2_offset, scale2_factor, config_version
                FROM device_configs WHERE device_id = %s;
                """,
                (device_id,),
            )
            r = cur.fetchone()
    return DeviceConfig(
        device_id=r[0], send_interval_seconds=r[1], scale1_offset=r[2],
        scale1_factor=r[3], scale2_offset=r[4], scale2_factor=r[5], config_version=r[6]
    )


@app.patch("/api/v1/devices/{device_id}/config", dependencies=[Depends(require_api_key)])
def update_device_config(device_id: str, patch: DeviceConfigUpdate):
    ensure_device_config(device_id)
    fields = patch.model_dump(exclude_unset=True)
    if not fields:
        return get_device_config(device_id)
    assignments = [f"{k} = %({k})s" for k in fields]
    assignments.append("config_version = config_version + 1")
    assignments.append("updated_at = now()")
    fields["device_id"] = device_id
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                f"UPDATE device_configs SET {', '.join(assignments)} WHERE device_id = %(device_id)s;",
                fields,
            )
            conn.commit()
    return get_device_config(device_id)


@app.get("/api/v1/devices/{device_id}/firmware", dependencies=[Depends(require_api_key)])
def check_firmware(device_id: str, version: str = Query("0.0.0")):
    ensure_device_config(device_id)
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT version, filename FROM firmware_releases
                WHERE active = true
                ORDER BY created_at DESC, id DESC
                LIMIT 1;
                """
            )
            r = cur.fetchone()
    if not r or version_tuple(r[0]) <= version_tuple(version):
        return {"update": False}
    url = f"{PUBLIC_BASE_URL}/firmware/{r[1]}" if PUBLIC_BASE_URL else f"/firmware/{r[1]}"
    return {"update": True, "version": r[0], "url": url}


@app.post("/api/v1/firmware/releases", dependencies=[Depends(require_api_key)])
def create_firmware_release(payload: FirmwareReleaseIn):
    path = FIRMWARE_DIR / payload.filename
    if not path.exists():
        raise HTTPException(status_code=400, detail=f"Firmware file not found: {path}")
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                INSERT INTO firmware_releases (version, filename, active)
                VALUES (%s, %s, %s)
                ON CONFLICT (version) DO UPDATE
                SET filename = EXCLUDED.filename, active = EXCLUDED.active
                RETURNING id;
                """,
                (payload.version, payload.filename, payload.active),
            )
            release_id = cur.fetchone()[0]
            conn.commit()
    return {"status": "ok", "id": release_id}


@app.get("/firmware/{filename}")
def download_firmware(filename: str):
    if "/" in filename or ".." in filename:
        raise HTTPException(status_code=400, detail="Invalid filename")
    path = FIRMWARE_DIR / filename
    if not path.exists():
        raise HTTPException(status_code=404, detail="Firmware not found")
    return FileResponse(path, media_type="application/octet-stream", filename=filename)


@app.post("/api/v1/devices/{device_id}/commands", dependencies=[Depends(require_api_key)])
def create_command(device_id: str, payload: DeviceCommandIn):
    ensure_device_config(device_id)
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                INSERT INTO device_commands (device_id, command_type, payload)
                VALUES (%s, %s, %s)
                RETURNING id;
                """,
                (device_id, payload.command_type, psycopg.types.json.Jsonb(payload.payload)),
            )
            command_id = cur.fetchone()[0]
            conn.commit()
    return {"status": "queued", "id": command_id}


@app.get("/api/v1/devices/{device_id}/commands/next", dependencies=[Depends(require_api_key)])
def get_next_command(device_id: str):
    ensure_device_config(device_id)
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT id, command_type, payload
                FROM device_commands
                WHERE device_id = %s AND status = 'pending'
                ORDER BY created_at ASC
                LIMIT 1
                FOR UPDATE SKIP LOCKED;
                """,
                (device_id,),
            )
            r = cur.fetchone()
            if not r:
                conn.commit()
                return {"command": False}
            cur.execute(
                "UPDATE device_commands SET status = 'claimed', claimed_at = now() WHERE id = %s;",
                (r[0],),
            )
            conn.commit()
    return {"command": True, "id": r[0], "command_type": r[1], "payload": r[2]}


def apply_command_result_to_config(device_id: str, result: dict[str, Any]):
    allowed = {
        "scale1_offset",
        "scale1_factor",
        "scale2_offset",
        "scale2_factor",
    }
    fields = {k: v for k, v in result.items() if k in allowed and v is not None}
    if not fields:
        return
    assignments = [f"{k} = %({k})s" for k in fields]
    assignments.append("config_version = config_version + 1")
    assignments.append("updated_at = now()")
    fields["device_id"] = device_id
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                f"UPDATE device_configs SET {', '.join(assignments)} WHERE device_id = %(device_id)s;",
                fields,
            )
            conn.commit()


@app.post("/api/v1/devices/{device_id}/commands/{command_id}/result", dependencies=[Depends(require_api_key)])
def command_result(device_id: str, command_id: int, payload: DeviceCommandResult):
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                UPDATE device_commands
                SET status = %s, result = %s, completed_at = now()
                WHERE id = %s AND device_id = %s;
                """,
                (
                    "done" if payload.success else "failed",
                    psycopg.types.json.Jsonb(payload.model_dump()),
                    command_id,
                    device_id,
                ),
            )
            conn.commit()
    return {"status": "ok"}

@app.get("/api/v1/time", dependencies=[Depends(require_api_key)])
def get_server_time():
    now = datetime.now(timezone.utc)
    return {
        "timestamp": now.isoformat(),
        "unix": int(now.timestamp()),
        "timezone": "UTC",
    }