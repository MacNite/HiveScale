import os
from datetime import datetime, timezone
from typing import Optional

import psycopg
from fastapi import FastAPI, Header, HTTPException, Depends
from pydantic import BaseModel, Field


DATABASE_URL = os.environ["DATABASE_URL"]
API_KEY = os.environ["API_KEY"]

app = FastAPI(
    title="HiveScale API",
    description="HTTP endpoint for ESP32-based dual hive scales.",
    version="0.1.0",
)


class MeasurementIn(BaseModel):
    device_id: str = Field(..., examples=["hive_scale_dual_01"])
    timestamp: Optional[datetime] = Field(
        default=None,
        description="Device timestamp. If omitted, server time is used.",
    )

    scale_1_weight_kg: Optional[float] = None
    scale_2_weight_kg: Optional[float] = None

    hive_1_temp_c: Optional[float] = None
    hive_2_temp_c: Optional[float] = None

    ambient_temp_c: Optional[float] = None
    ambient_humidity_percent: Optional[float] = None

    battery_voltage: Optional[float] = None
    rssi_dbm: Optional[int] = None


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

                    raw_json JSONB NOT NULL
                );

                CREATE INDEX IF NOT EXISTS idx_measurements_device_time
                ON measurements (device_id, measured_at DESC);
                """
            )
        conn.commit()


@app.on_event("startup")
def startup():
    init_db()


@app.get("/health")
def health():
    return {"status": "ok"}


@app.post("/api/v1/measurements", dependencies=[Depends(require_api_key)])
def create_measurement(payload: MeasurementIn):
    measured_at = payload.timestamp or datetime.now(timezone.utc)

    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                INSERT INTO measurements (
                    device_id,
                    measured_at,
                    scale_1_weight_kg,
                    scale_2_weight_kg,
                    hive_1_temp_c,
                    hive_2_temp_c,
                    ambient_temp_c,
                    ambient_humidity_percent,
                    battery_voltage,
                    rssi_dbm,
                    raw_json
                )
                VALUES (
                    %(device_id)s,
                    %(measured_at)s,
                    %(scale_1_weight_kg)s,
                    %(scale_2_weight_kg)s,
                    %(hive_1_temp_c)s,
                    %(hive_2_temp_c)s,
                    %(ambient_temp_c)s,
                    %(ambient_humidity_percent)s,
                    %(battery_voltage)s,
                    %(rssi_dbm)s,
                    %(raw_json)s
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
                    "raw_json": payload.model_dump_json(),
                },
            )
            new_id = cur.fetchone()[0]
        conn.commit()

    return {
        "status": "ok",
        "id": new_id,
        "device_id": payload.device_id,
        "measured_at": measured_at.isoformat(),
    }


@app.get("/api/v1/measurements/latest")
def latest_measurements(limit: int = 50):
    limit = min(max(limit, 1), 500)

    with get_conn() as conn:
        with conn.cursor() as cur:
            cur.execute(
                """
                SELECT
                    id,
                    device_id,
                    measured_at,
                    received_at,
                    scale_1_weight_kg,
                    scale_2_weight_kg,
                    hive_1_temp_c,
                    hive_2_temp_c,
                    ambient_temp_c,
                    ambient_humidity_percent,
                    battery_voltage,
                    rssi_dbm
                FROM measurements
                ORDER BY measured_at DESC
                LIMIT %s;
                """,
                (limit,),
            )

            rows = cur.fetchall()

    return [
        {
            "id": row[0],
            "device_id": row[1],
            "measured_at": row[2],
            "received_at": row[3],
            "scale_1_weight_kg": row[4],
            "scale_2_weight_kg": row[5],
            "hive_1_temp_c": row[6],
            "hive_2_temp_c": row[7],
            "ambient_temp_c": row[8],
            "ambient_humidity_percent": row[9],
            "battery_voltage": row[10],
            "rssi_dbm": row[11],
        }
        for row in rows
    ]


# ---------------------------------------------------------------------------
# Future beelogger.de compatibility section
# ---------------------------------------------------------------------------
#
# Later we can add a route here that accepts or emits data in a format compatible
# with the beelogger.de project.
#
# Possible future options:
#
# 1. Accept Beelogger-style GET/POST parameters and translate them into the
#    internal measurements table.
#
# 2. Add an export endpoint that maps HiveScale database rows into the format
#    expected by Beelogger-compatible tools.
#
# Example placeholder:
#
# @app.post("/api/v1/beelogger/measurements")
# def create_beelogger_measurement(...):
#     ...
#
# Keep the internal database schema clean and stable. Compatibility layers should
# translate external formats into our canonical HiveScale measurement model.
# ---------------------------------------------------------------------------