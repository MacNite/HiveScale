import hashlib
import os
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional, Literal, Any

import psycopg
from fastapi import Depends, FastAPI, Header, HTTPException, Query, status
from fastapi.responses import FileResponse
from pydantic import BaseModel, Field

DATABASE_URL = os.environ["DATABASE_URL"]
API_KEY = os.environ["API_KEY"]
HIVEPAL_SERVICE_API_KEY = os.environ.get("HIVEPAL_SERVICE_API_KEY", "")
PUBLIC_BASE_URL = os.environ.get("PUBLIC_BASE_URL", "").rstrip("/")
FIRMWARE_DIR = Path(os.environ.get("FIRMWARE_DIR", "/app/firmware"))

app = FastAPI(
    title="HiveScale API",
    description="HTTP endpoint for ESP32-based dual hive scales.",
    version="0.3.0",
)


class MeasurementIn(BaseModel):
    device_id: str = Field(..., examples=["hive_scale_dual_01"])
    claim_code: Optional[str] = Field(default=None, min_length=4, max_length=128)
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


class ClaimDeviceIn(BaseModel):
    claim_code: str = Field(..., min_length=4, max_length=128)
    display_name: Optional[str] = None
    scale_1_display_name: Optional[str] = None
    scale_2_display_name: Optional[str] = None


class ShareDeviceIn(BaseModel):
    user_id: str = Field(..., min_length=1)
    role: Literal["admin", "viewer"] = "viewer"


class DeviceChannelsUpdateIn(BaseModel):
    scale_1_display_name: Optional[str] = None
    scale_2_display_name: Optional[str] = None


class AppDeviceConfigUpdate(DeviceConfigUpdate):
    pass


def require_api_key(x_api_key: str = Header(default="")):
    if x_api_key != API_KEY:
        raise HTTPException(status_code=401, detail="Invalid API key")


def require_hivepal_service_key(x_hivepal_service_key: str = Header(default="")):
    if not HIVEPAL_SERVICE_API_KEY:
        raise HTTPException(status_code=500, detail="HIVEPAL_SERVICE_API_KEY is not configured")
    if x_hivepal_service_key != HIVEPAL_SERVICE_API_KEY:
        raise HTTPException(status_code=401, detail="Invalid HivePal service key")


def require_user_id(x_user_id: str = Header(default="")) -> str:
    # Temporary bridge until HivePal JWT/session validation is wired in.
    # In HivePal, replace this with token verification and return the logged-in user id.
    user_id = x_user_id.strip()
    if not user_id:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="Missing X-User-Id")
    return user_id


def normalize_claim_code(claim_code: str) -> str:
    return claim_code.strip().upper().replace(" ", "")


def hash_claim_code(claim_code: str) -> str:
    return hashlib.sha256(normalize_claim_code(claim_code).encode("utf-8")).hexdigest()


def get_conn():
    return psycopg.connect(DATABASE_URL)


def init_db():
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                CREATE TABLE IF NOT EXISTS devices (
                    device_id TEXT PRIMARY KEY,
                    claim_code_hash TEXT,
                    claimed_at TIMESTAMPTZ,
                    display_name TEXT,
                    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
                    last_seen_at TIMESTAMPTZ,
                    last_firmware_version TEXT
                );

                CREATE INDEX IF NOT EXISTS idx_devices_claim_code_hash
                    ON devices (claim_code_hash) WHERE claimed_at IS NULL;

                CREATE TABLE IF NOT EXISTS device_members (
                    device_id TEXT NOT NULL REFERENCES devices(device_id) ON DELETE CASCADE,
                    user_id TEXT NOT NULL,
                    role TEXT NOT NULL CHECK (role IN ('owner', 'admin', 'viewer')),
                    invited_by TEXT,
                    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
                    PRIMARY KEY (device_id, user_id)
                );

                CREATE INDEX IF NOT EXISTS idx_device_members_user
                    ON device_members (user_id, device_id);

                CREATE TABLE IF NOT EXISTS device_channels (
                    device_id TEXT NOT NULL REFERENCES devices(device_id) ON DELETE CASCADE,
                    channel_number INTEGER NOT NULL CHECK (channel_number IN (1, 2)),
                    name TEXT,
                    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
                    PRIMARY KEY (device_id, channel_number)
                );

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

                ALTER TABLE devices ADD COLUMN IF NOT EXISTS claim_code_hash TEXT;
                ALTER TABLE devices ADD COLUMN IF NOT EXISTS claimed_at TIMESTAMPTZ;
                ALTER TABLE devices ADD COLUMN IF NOT EXISTS display_name TEXT;
                ALTER TABLE devices ADD COLUMN IF NOT EXISTS created_at TIMESTAMPTZ NOT NULL DEFAULT now();
                ALTER TABLE devices ADD COLUMN IF NOT EXISTS last_seen_at TIMESTAMPTZ;
                ALTER TABLE devices ADD COLUMN IF NOT EXISTS last_firmware_version TEXT;

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


def ensure_device(device_id: str, claim_code: Optional[str] = None, firmware_version: Optional[str] = None):
    claim_hash = hash_claim_code(claim_code) if claim_code else None
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                INSERT INTO devices (device_id, claim_code_hash, last_seen_at, last_firmware_version)
                VALUES (%s, %s, now(), %s)
                ON CONFLICT (device_id) DO UPDATE
                SET last_seen_at = now(),
                    last_firmware_version = COALESCE(EXCLUDED.last_firmware_version, devices.last_firmware_version),
                    claim_code_hash = CASE
                        WHEN devices.claimed_at IS NULL AND EXCLUDED.claim_code_hash IS NOT NULL
                        THEN EXCLUDED.claim_code_hash
                        ELSE devices.claim_code_hash
                    END;
                """,
                (device_id, claim_hash, firmware_version),
            )
            cur.execute(
                """
                INSERT INTO device_channels (device_id, channel_number, name)
                VALUES (%s, 1, 'Scale 1'), (%s, 2, 'Scale 2')
                ON CONFLICT (device_id, channel_number) DO NOTHING;
                """,
                (device_id, device_id),
            )
            conn.commit()


def ensure_device_config(device_id: str):
    ensure_device(device_id)
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


def require_device_role(user_id: str, device_id: str, roles: list[str]):
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT role FROM device_members
                WHERE device_id = %s AND user_id = %s;
                """,
                (device_id, user_id),
            )
            row = cur.fetchone()
    if not row or row[0] not in roles:
        raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail="Insufficient device permissions")
    return row[0]


def get_device_channels(device_id: str) -> list[dict[str, Any]]:
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT channel_number, name
                FROM device_channels
                WHERE device_id = %s
                ORDER BY channel_number;
                """,
                (device_id,),
            )
            rows = cur.fetchall()
    return [{"channel_number": r[0], "name": r[1]} for r in rows]


def upsert_device_channel_names(cur, device_id: str, scale_1_name: Optional[str], scale_2_name: Optional[str]):
    updates = [(1, scale_1_name), (2, scale_2_name)]
    for channel_number, name in updates:
        if name is None:
            continue
        clean_name = name.strip()
        if not clean_name:
            continue
        cur.execute(
            """
            INSERT INTO device_channels (device_id, channel_number, name)
            VALUES (%s, %s, %s)
            ON CONFLICT (device_id, channel_number) DO UPDATE
            SET name = EXCLUDED.name;
            """,
            (device_id, channel_number, clean_name),
        )


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
    ensure_device(payload.device_id, payload.claim_code, payload.firmware_version)
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
                    "raw_json": psycopg.types.json.Jsonb(payload.model_dump(mode="json", exclude={"claim_code"})),
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


@app.post("/api/v1/app/devices/claim", dependencies=[Depends(require_hivepal_service_key)])
def claim_device(payload: ClaimDeviceIn, user_id: str = Depends(require_user_id)):
    claim_hash = hash_claim_code(payload.claim_code)
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT device_id FROM devices
                WHERE claim_code_hash = %s AND claimed_at IS NULL
                ORDER BY created_at ASC
                LIMIT 1
                FOR UPDATE;
                """,
                (claim_hash,),
            )
            row = cur.fetchone()
            if not row:
                raise HTTPException(status_code=404, detail="No unclaimed device found for this claim code")
            device_id = row[0]
            cur.execute(
                """
                UPDATE devices
                SET claimed_at = now(), display_name = COALESCE(%s, display_name)
                WHERE device_id = %s;
                """,
                (payload.display_name, device_id),
            )
            cur.execute(
                """
                INSERT INTO device_members (device_id, user_id, role)
                VALUES (%s, %s, 'owner')
                ON CONFLICT (device_id, user_id) DO UPDATE SET role = 'owner';
                """,
                (device_id, user_id),
            )
            upsert_device_channel_names(
                cur,
                device_id,
                payload.scale_1_display_name,
                payload.scale_2_display_name,
            )
            conn.commit()
    return {
        "status": "claimed",
        "device_id": device_id,
        "role": "owner",
        "channels": get_device_channels(device_id),
    }


@app.get("/api/v1/app/devices", dependencies=[Depends(require_hivepal_service_key)])
def list_user_devices(user_id: str = Depends(require_user_id)):
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT d.device_id, d.display_name, d.claimed_at, d.last_seen_at,
                       d.last_firmware_version, m.role
                FROM device_members m
                JOIN devices d ON d.device_id = m.device_id
                WHERE m.user_id = %s
                ORDER BY COALESCE(d.display_name, d.device_id);
                """,
                (user_id,),
            )
            rows = cur.fetchall()

            devices = []
            for r in rows:
                cur.execute(
                    """
                    SELECT channel_number, name
                    FROM device_channels
                    WHERE device_id = %s
                    ORDER BY channel_number;
                    """,
                    (r[0],),
                )
                channel_rows = cur.fetchall()
                devices.append(
                    {
                        "device_id": r[0],
                        "display_name": r[1],
                        "claimed_at": r[2],
                        "last_seen_at": r[3],
                        "last_firmware_version": r[4],
                        "role": r[5],
                        "channels": [
                            {"channel_number": c[0], "name": c[1]}
                            for c in channel_rows
                        ],
                    }
                )
    return devices


@app.delete("/api/v1/app/devices/{device_id}", dependencies=[Depends(require_hivepal_service_key)])
def remove_current_user_device(device_id: str, user_id: str = Depends(require_user_id)):
    require_device_role(user_id, device_id, ["owner", "admin", "viewer"])
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                "DELETE FROM device_members WHERE device_id = %s AND user_id = %s;",
                (device_id, user_id),
            )
            cur.execute(
                "SELECT COUNT(*) FROM device_members WHERE device_id = %s;",
                (device_id,),
            )
            remaining_members = cur.fetchone()[0]
            if remaining_members == 0:
                cur.execute(
                    "UPDATE devices SET claimed_at = NULL WHERE device_id = %s;",
                    (device_id,),
                )
            conn.commit()
    return {
        "status": "removed",
        "device_id": device_id,
        "claimable": remaining_members == 0,
    }


@app.get("/api/v1/app/devices/{device_id}/channels", dependencies=[Depends(require_hivepal_service_key)])
def list_device_channels(device_id: str, user_id: str = Depends(require_user_id)):
    require_device_role(user_id, device_id, ["owner", "admin", "viewer"])
    return {"device_id": device_id, "channels": get_device_channels(device_id)}


@app.patch("/api/v1/app/devices/{device_id}/channels", dependencies=[Depends(require_hivepal_service_key)])
def update_device_channels(
    device_id: str,
    payload: DeviceChannelsUpdateIn,
    user_id: str = Depends(require_user_id),
):
    require_device_role(user_id, device_id, ["owner", "admin"])
    with get_conn() as conn:
        with conn.cursor() as cur:
            upsert_device_channel_names(
                cur,
                device_id,
                payload.scale_1_display_name,
                payload.scale_2_display_name,
            )
            conn.commit()
    return {"device_id": device_id, "channels": get_device_channels(device_id)}


@app.get("/api/v1/app/devices/{device_id}/members", dependencies=[Depends(require_hivepal_service_key)])
def list_device_members(device_id: str, user_id: str = Depends(require_user_id)):
    require_device_role(user_id, device_id, ["owner", "admin", "viewer"])
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT user_id, role, invited_by, created_at
                FROM device_members
                WHERE device_id = %s
                ORDER BY created_at ASC;
                """,
                (device_id,),
            )
            rows = cur.fetchall()
    return [
        {
            "user_id": r[0],
            "role": r[1],
            "invited_by": r[2],
            "created_at": r[3],
        }
        for r in rows
    ]


@app.post("/api/v1/app/devices/{device_id}/members", dependencies=[Depends(require_hivepal_service_key)])
def share_device(device_id: str, payload: ShareDeviceIn, user_id: str = Depends(require_user_id)):
    require_device_role(user_id, device_id, ["owner"])
    if payload.user_id == user_id:
        raise HTTPException(status_code=400, detail="Use your existing owner access instead of sharing with yourself")
    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                INSERT INTO device_members (device_id, user_id, role, invited_by)
                VALUES (%s, %s, %s, %s)
                ON CONFLICT (device_id, user_id) DO UPDATE
                SET role = EXCLUDED.role, invited_by = EXCLUDED.invited_by;
                """,
                (device_id, payload.user_id, payload.role, user_id),
            )
            conn.commit()
    return {"status": "shared", "device_id": device_id, "user_id": payload.user_id, "role": payload.role}


@app.delete("/api/v1/app/devices/{device_id}/members/{member_user_id}", dependencies=[Depends(require_hivepal_service_key)])
def revoke_device_member(device_id: str, member_user_id: str, user_id: str = Depends(require_user_id)):
    require_device_role(user_id, device_id, ["owner"])
    if member_user_id == user_id:
        raise HTTPException(status_code=400, detail="Use remove device to remove your own access")

    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT role FROM device_members
                WHERE device_id = %s AND user_id = %s;
                """,
                (device_id, member_user_id),
            )
            row = cur.fetchone()
            if not row:
                raise HTTPException(status_code=404, detail="Device member not found")
            if row[0] == "owner":
                raise HTTPException(status_code=400, detail="Owner access cannot be revoked here")

            cur.execute(
                "DELETE FROM device_members WHERE device_id = %s AND user_id = %s;",
                (device_id, member_user_id),
            )
            conn.commit()
    return {"status": "revoked", "device_id": device_id, "user_id": member_user_id}


@app.get("/api/v1/app/devices/{device_id}/config", dependencies=[Depends(require_hivepal_service_key)])
def get_device_config_from_app(device_id: str, user_id: str = Depends(require_user_id)):
    require_device_role(user_id, device_id, ["owner", "admin", "viewer"])
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


@app.get("/api/v1/app/devices/{device_id}/measurements", dependencies=[Depends(require_hivepal_service_key)])
def list_device_measurements(
    device_id: str,
    limit: int = 200,
    start_at: Optional[datetime] = None,
    end_at: Optional[datetime] = None,
    user_id: str = Depends(require_user_id),
):
    require_device_role(user_id, device_id, ["owner", "admin", "viewer"])
    limit = min(max(limit, 1), 10000)
    where_parts = ["device_id = %s"]
    params: list[Any] = [device_id]

    if start_at is not None:
        where_parts.append("measured_at >= %s")
        params.append(start_at)

    if end_at is not None:
        where_parts.append("measured_at <= %s")
        params.append(end_at)

    params.append(limit)

    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                f"""
                SELECT id, device_id, measured_at, received_at, scale_1_weight_kg,
                       scale_2_weight_kg, hive_1_temp_c, hive_2_temp_c,
                       ambient_temp_c, ambient_humidity_percent, battery_voltage,
                       rssi_dbm, firmware_version, config_version, sd_ok, rtc_ok, sht_ok,
                       scale_1_raw, scale_2_raw
                FROM measurements
                WHERE {' AND '.join(where_parts)}
                ORDER BY measured_at DESC
                LIMIT %s;
                """,
                params,
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


@app.get("/api/v1/app/devices/{device_id}/measurements/latest", dependencies=[Depends(require_hivepal_service_key)])
def latest_device_measurements(device_id: str, limit: int = 50, user_id: str = Depends(require_user_id)):
    require_device_role(user_id, device_id, ["owner", "admin", "viewer"])
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
                WHERE device_id = %s
                ORDER BY measured_at DESC
                LIMIT %s;
                """,
                (device_id, limit),
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


@app.patch("/api/v1/app/devices/{device_id}/config", dependencies=[Depends(require_hivepal_service_key)])
def update_device_config_from_app(device_id: str, patch: AppDeviceConfigUpdate, user_id: str = Depends(require_user_id)):
    require_device_role(user_id, device_id, ["owner", "admin"])
    return update_device_config(device_id, patch)


@app.get("/api/v1/time", dependencies=[Depends(require_api_key)])
def get_server_time():
    now = datetime.now(timezone.utc)
    return {
        "timestamp": now.isoformat(),
        "unix": int(now.timestamp()),
        "timezone": "UTC",
    }