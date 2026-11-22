import argparse
import hmac
import json
import socket
import sqlite3
import time
from contextlib import ExitStack, asynccontextmanager, closing
from pathlib import Path
from typing import Annotated, Literal

import uvicorn
from fastapi import FastAPI, HTTPException, Request, Response
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, ConfigDict, Field, TypeAdapter, ValidationError
from starlette.concurrency import run_in_threadpool

ROOT = Path(__file__).resolve().parent
METRICS = ("temp_bme_c", "temp_ds_c", "humidity_pct", "pressure_mmhg", "co2_ppm")
PERIODS = {
    "1h": (1, 0),
    "6h": (6, 0),
    "24h": (24, 0),
    "7d": (7 * 24, 15),
    "30d": (30 * 24, 60),
    "90d": (90 * 24, 180),
    "180d": (180 * 24, 360),
    "1y": (365 * 24, 720),
}


class Sample(BaseModel):
    model_config = ConfigDict(extra="forbid", strict=True, allow_inf_nan=False)
    station: str = Field(pattern=r"^[A-Za-z0-9_-]{1,32}$")
    boot: str = Field(pattern=r"^[a-f0-9]{16}$")
    seq: int = Field(ge=1, le=0xFFFFFFFF)
    measured_at_ms: int = Field(ge=1735689600000, lt=4102444800000)
    temp_bme_c: float | None = Field(ge=-100, le=200)
    temp_ds_c: float | None = Field(ge=-100, le=200)
    humidity_pct: float | None = Field(ge=0, le=100)
    pressure_mmhg: float | None = Field(ge=0, le=1500)
    co2_ppm: int | None = Field(ge=0, le=65535)


BATCH = TypeAdapter(Annotated[list[Sample], Field(min_length=1, max_length=128)])


def create_app(database: Path, token: str):
    database = database.resolve()

    @asynccontextmanager
    async def lifespan(app):
        database.parent.mkdir(parents=True, exist_ok=True)
        with closing(sqlite3.connect(database)) as db, db:
            db.executescript("""
                PRAGMA journal_mode = WAL;
                CREATE TABLE IF NOT EXISTS readings (
                    station TEXT NOT NULL, boot TEXT NOT NULL, seq INTEGER NOT NULL,
                    measured_at_ms INTEGER NOT NULL,
                    temp_bme_c REAL, temp_ds_c REAL, humidity_pct REAL, pressure_mmhg REAL, co2_ppm INTEGER,
                    PRIMARY KEY (station, boot, seq)
                );
                CREATE INDEX IF NOT EXISTS readings_time ON readings(measured_at_ms);
            """)
        yield

    app = FastAPI(title="Метеостанция", lifespan=lifespan)
    app.mount("/static", StaticFiles(directory=ROOT / "static"), name="static")
    expected_token = ("Bearer " + token).encode("utf-8")

    def store(rows):
        with closing(sqlite3.connect(database)) as db, db:
            db.executemany("""
                INSERT INTO readings (
                    station, boot, seq, measured_at_ms,
                    temp_bme_c, temp_ds_c, humidity_pct, pressure_mmhg, co2_ppm
                ) VALUES (?,?,?,?,?,?,?,?,?)
                ON CONFLICT (station, boot, seq) DO NOTHING
            """, [
                (row.station, row.boot, row.seq, row.measured_at_ms,
                 *(getattr(row, metric) for metric in METRICS))
                for row in rows
            ])

    @app.post("/v1/batches", status_code=204)
    async def ingest(request: Request):
        received = request.headers.get("authorization", "").encode("utf-8")
        if not hmac.compare_digest(received, expected_token):
            raise HTTPException(401, "Unauthorized")
        body = bytearray()
        async for chunk in request.stream():
            if len(body) + len(chunk) > 65536:
                raise HTTPException(413, "Batch too large")
            body.extend(chunk)
        try:
            rows = BATCH.validate_json(body)
        except ValidationError as error:
            raise HTTPException(400, "Invalid readings batch") from error
        # Подтверждение отправляется только после фиксации всей партии в SQLite.
        await run_in_threadpool(store, rows)
        return Response(status_code=204)

    @app.get("/api/history")
    def history(period: Literal["1h", "6h", "24h", "7d", "30d", "90d", "180d", "1y"] = "24h"):
        hours, bucket_minutes = PERIODS[period]
        now_ms = int(time.time() * 1000)
        start_ms = now_ms - hours * 3_600_000
        bucket_ms = bucket_minutes * 60_000
        with closing(sqlite3.connect(database)) as db:
            db.row_factory = sqlite3.Row
            latest = db.execute(
                "SELECT max(measured_at_ms) FROM readings WHERE measured_at_ms <= ?", (now_ms,)
            ).fetchone()[0]
            if bucket_ms:
                averages = ", ".join(f"avg({name}) AS {name}" for name in METRICS)
                rows = db.execute(f"""
                    SELECT max((measured_at_ms / ?) * ?, ?) AS t, {averages},
                           min(co2_ppm) AS co2_min, max(co2_ppm) AS co2_max
                    FROM readings
                    WHERE measured_at_ms BETWEEN ? AND ?
                    GROUP BY t ORDER BY t
                """, (bucket_ms, bucket_ms, start_ms, start_ms, now_ms))
            else:
                rows = db.execute("""
                    SELECT measured_at_ms AS t, temp_bme_c, temp_ds_c,
                           humidity_pct, pressure_mmhg, co2_ppm
                    FROM readings WHERE measured_at_ms BETWEEN ? AND ?
                    ORDER BY measured_at_ms
                """, (start_ms, now_ms))
            return {
                "latest_ms": latest, "start_ms": start_ms, "end_ms": now_ms,
                "bucket_ms": bucket_ms, "rows": [dict(row) for row in rows],
            }

    @app.get("/", include_in_schema=False)
    def dashboard():
        return FileResponse(ROOT / "static" / "index.html")

    return app


def serve(app: FastAPI, host: str, port: int):
    if host in ("localhost", "127.0.0.1", "::1", "0.0.0.0", "::"):
        uvicorn.run(app, host=host, port=port)
        return

    # One process listens on the router's LAN address and on localhost.
    with ExitStack() as stack:
        family = socket.AF_INET6 if ":" in host else socket.AF_INET
        lan = stack.enter_context(socket.create_server((host, port), family=family))
        address, bound_port, *_ = lan.getsockname()
        listeners = [lan]
        if address != "127.0.0.1":
            listeners.append(stack.enter_context(socket.create_server(("127.0.0.1", bound_port))))
        uvicorn.Server(uvicorn.Config(app, host=host, port=bound_port)).run(sockets=listeners)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, required=True)
    args = parser.parse_args()
    settings = json.loads(args.config.read_text(encoding="utf-8-sig"))
    token = settings["token"]
    if len(token) < 16 or any(char.isspace() for char in token):
        parser.error("expected a token of at least 16 characters without spaces")
    db_path = args.config.resolve().parent / settings.get("database", "data/meteo.sqlite3")
    serve(create_app(db_path, token), settings.get("listen", "127.0.0.1"), settings.get("port", 8780))
