import contextlib
import json
import os
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from urllib import error, request

ROOT = Path(__file__).resolve().parents[1]
TOKEN = "integration-test-token-local-only"
OPENER = request.build_opener(request.ProxyHandler({}))


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def api(url, payload=None, authenticated=True):
    headers = {"Authorization": "Bearer " + TOKEN} if authenticated else {}
    content = None
    if payload is not None:
        headers["Content-Type"] = "application/json"
        content = json.dumps(payload).encode()
    with OPENER.open(request.Request(url, data=content, headers=headers), timeout=3) as response:
        data = response.read()
        return response.status, data


def until(check, seconds=25):
    end = time.monotonic() + seconds
    last_error = None
    while time.monotonic() < end:
        try:
            value = check()
            if value:
                return value
        except error.HTTPError as failure:
            raise AssertionError(f"HTTP {failure.code}: {failure.read().decode()}") from failure
        except (OSError, sqlite3.OperationalError) as failure:
            last_error = failure
        time.sleep(0.2)
    raise AssertionError("timed out waiting for local service") from last_error


@contextlib.contextmanager
def process(command, log_path):
    with log_path.open("w+") as log:
        proc = subprocess.Popen(command, stdout=log, stderr=log,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        try:
            yield proc
        except BaseException:
            log.seek(0)
            print(log.read(), file=sys.stderr)
            raise
        finally:
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=5)


def main():
    server_port = free_port()
    with tempfile.TemporaryDirectory(prefix="meteo-integration-") as temp:
        folder = Path(temp)
        database = folder / "meteo.sqlite3"
        server_url = f"http://127.0.0.2:{server_port}"
        dashboard_url = f"http://localhost:{server_port}"
        ingest_url = server_url + "/v1/batches"
        settings = folder / "server.json"
        settings.write_text(json.dumps({"token": TOKEN, "listen": "127.0.0.2", "port": server_port,
                                        "database": str(database)}))
        sample_at = int(time.time() * 1000) - 60_000
        sample = {
            "station": "meteo-1", "boot": "a000000000000001", "seq": 1,
            "measured_at_ms": sample_at,
            "temp_bme_c": -3.25, "temp_ds_c": None, "humidity_pct": 49.15,
            "pressure_mmhg": 753.1, "co2_ppm": 885,
        }
        second = sample | {"seq": 2, "measured_at_ms": sample_at + 30_000, "co2_ppm": None}
        command = [sys.executable, str(ROOT / "server/server.py"), "--config", str(settings)]
        with process(command, folder / "server.log"):
            until(lambda: api(server_url + "/api/history"))
            try:
                api(ingest_url, [sample], authenticated=False)
                raise AssertionError("accepted missing bearer token")
            except error.HTTPError as denied:
                assert denied.code == 401

            assert api(ingest_url, [sample, second])[0] == 204
            assert api(ingest_url, [sample])[0] == 204
            with contextlib.closing(sqlite3.connect(database)) as db:
                rows = db.execute("""
                    SELECT seq, measured_at_ms, temp_bme_c, temp_ds_c, humidity_pct, pressure_mmhg, co2_ppm
                    FROM readings ORDER BY seq
                """).fetchall()
            assert rows == [
                (1, sample_at, -3.25, None, 49.15, 753.1, 885),
                (2, sample_at + 30_000, -3.25, None, 49.15, 753.1, None),
            ]

            historic_at = (sample_at - 200 * 86_400_000) // 43_200_000 * 43_200_000 + 60_000
            historic = sample | {"seq": 3, "measured_at_ms": historic_at, "co2_ppm": 600}
            peak = historic | {"seq": 4, "measured_at_ms": historic_at + 60_000, "co2_ppm": 1200}
            assert api(ingest_url, [historic, peak])[0] == 204
            periods = {
                "1h": (1, 0), "6h": (6, 0), "24h": (24, 0), "7d": (168, 15), "30d": (720, 60),
                "90d": (2160, 180), "180d": (4320, 360), "1y": (8760, 720),
            }
            for period, (hours, minutes) in periods.items():
                response = json.loads(api(server_url + "/api/history?period=" + period)[1])
                assert response["rows"] and response["latest_ms"] == second["measured_at_ms"]
                assert response["end_ms"] - response["start_ms"] == hours * 3_600_000
                assert response["bucket_ms"] == minutes * 60_000
                old = [row for row in response["rows"] if row["t"] < historic_at + 120_000]
                if period == "1y":
                    assert len(old) == 1
                    assert (old[0]["co2_min"], old[0]["co2_ppm"], old[0]["co2_max"]) == (600, 900, 1200)
                else:
                    assert not old
            assert api(dashboard_url + "/static/vendor/chart.umd.min.js")[0] == 200
            assert api(dashboard_url + "/")[0] == 200
            assert json.loads(api(dashboard_url + "/api/history")[1])["latest_ms"] == second["measured_at_ms"]
            if len(sys.argv) > 1:
                subprocess.run(["node", str(ROOT / "tests/dashboard.cjs"), dashboard_url, sys.argv[1]], check=True)
        with process(command, folder / "server-restart.log"):
            result = until(lambda: api(server_url + "/api/history?period=24h"))
            assert len(json.loads(result[1])["rows"]) == 2
    print("Verified batch authentication, original timestamps, duplicate suppression, and SQLite persistence.")


if __name__ == "__main__":
    main()
