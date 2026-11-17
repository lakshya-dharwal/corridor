"""Pulls live ADS-B positions from the OpenSky Network.

    python3 traffic_worker.py --dry-run
    python3 traffic_worker.py --snapshot traffic.tsv --loop 300

The snapshot is who was inside the searched rectangle at one clock reading.
It is not a forecast of who will be there later. Only ADS-B positions are
kept: multilateration and FLARM are dropped, and so is anyone on the ground
or whose position is already more than a minute old.

One bounding-box request covers the demo's airports. A failed fetch leaves
the previous snapshot in place, so an outage never looks like an empty sky.
The rectangle is written into the snapshot header. The service uses it to
tell a route the fetch did not actually cover.

If DATABASE_URL is set, the same rows are stored in Postgres.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
import urllib.parse
import urllib.request
from datetime import datetime, timezone

try:
    from publish import publish
except ImportError:  # invoked as a module from the repo root
    from ingest.publish import publish

OPENSKY_URL = "https://opensky-network.org/api/states/all"
FEED_NAME = "traffic"

# Continental box around the demo airports, with room for the 25 nm corridor
# the engine draws around a route.
SOUTH, WEST, NORTH, EAST = 32.0, -125.0, 49.0, -83.0

# OpenSky position_source: 0 is ADS-B. 1 ASTERIX, 2 MLAT, 3 FLARM.
ADSB_SOURCE = 0
MAX_POSITION_AGE_S = 60

METERS_TO_FEET = 3.280839895
MPS_TO_KNOTS = 1.943844492

UPSERT_SQL = """
INSERT INTO traffic (icao, seen_at, callsign, alt_ft, groundspeed_kt, track_deg, geom)
VALUES (%(icao)s, to_timestamp(%(seen_epoch)s), %(callsign)s, %(alt_ft)s,
        %(groundspeed_kt)s, %(track_deg)s, ST_GeogFromText(%(wkt)s))
ON CONFLICT (icao, seen_at) DO NOTHING
"""

PURGE_SQL = "DELETE FROM traffic WHERE seen_at < now() - interval '1 day'"

ENSURE_SQL = """
CREATE TABLE IF NOT EXISTS traffic (
    icao            TEXT NOT NULL,
    seen_at         TIMESTAMPTZ NOT NULL,
    callsign        TEXT,
    alt_ft          REAL,
    groundspeed_kt  REAL,
    track_deg       REAL,
    geom            GEOGRAPHY(POINT, 4326) NOT NULL,
    PRIMARY KEY (icao, seen_at)
)
"""

FEED_OK_SQL = """
INSERT INTO feed_status (feed, last_success_at, last_error) VALUES (%(feed)s, now(), NULL)
ON CONFLICT (feed) DO UPDATE SET last_success_at = now(), last_error = NULL
"""


def fetch_states(timeout: float = 30.0, attempts: int = 3) -> dict:
    query = urllib.parse.urlencode({
        "lamin": SOUTH, "lomin": WEST, "lamax": NORTH, "lomax": EAST,
    })
    request = urllib.request.Request(
        f"{OPENSKY_URL}?{query}", headers={"User-Agent": "corridor-ingest/0.1"}
    )
    for attempt in range(1, attempts + 1):
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                payload = json.loads(response.read().decode("utf-8"))
            if not isinstance(payload, dict) or payload.get("states") is None:
                raise RuntimeError("OpenSky returned no state list")
            return payload
        except OSError as error:  # URLError and socket timeouts are both OSError
            if attempt == attempts:
                raise
            print(f"fetch attempt {attempt} failed ({error}); retrying", file=sys.stderr)
            time.sleep(2 * attempt)
    raise RuntimeError("OpenSky fetch failed")


def _callsign(value) -> str:
    text = str(value or "").strip().upper()
    cleaned = "".join(ch for ch in text if ch.isalnum())
    return cleaned[:8]


def _icao(value) -> str | None:
    text = str(value or "").strip().lower()
    if len(text) != 6 or any(ch not in "0123456789abcdef" for ch in text):
        return None
    return text


def normalize(payload: dict) -> tuple[int, list[dict]]:
    """Airborne ADS-B rows fresh at the response clock. Raises nothing:
    a well-formed list with nobody in it is an empty sky, which is publishable."""
    now = int(payload.get("time") or time.time())
    rows: dict[str, dict] = {}
    for state in payload.get("states") or []:
        if not isinstance(state, list) or len(state) < 17:
            continue
        if state[16] != ADSB_SOURCE or state[8] is not False:
            continue
        icao = _icao(state[0])
        seen = state[3]
        lat, lon = state[6], state[5]
        if icao is None or seen is None or lat is None or lon is None:
            continue
        seen = int(seen)
        if seen <= 0 or now - seen > MAX_POSITION_AGE_S or seen > now + 120:
            continue
        if not (-90.0 <= float(lat) <= 90.0 and -180.0 <= float(lon) <= 180.0):
            continue
        altitude = None if state[7] is None else round(float(state[7]) * METERS_TO_FEET)
        if altitude is not None and not -2000 <= altitude <= 70000:
            continue
        speed = None if state[9] is None else round(float(state[9]) * MPS_TO_KNOTS)
        if speed is not None and not 0 <= speed <= 800:
            speed = None
        track = None if state[10] is None else round(float(state[10]), 1)
        if track is not None and not 0.0 <= track <= 360.0:
            track = None
        current = rows.get(icao)
        if current is not None and current["seen_epoch"] >= seen:
            continue
        rows[icao] = {
            "icao": icao,
            "callsign": _callsign(state[1]),
            "lat": float(lat),
            "lon": float(lon),
            "alt_ft": altitude,
            "groundspeed_kt": speed,
            "track_deg": track,
            "seen_epoch": seen,
        }
    return now, [rows[key] for key in sorted(rows)]


def _field(value) -> str:
    if value is None:
        return ""
    if isinstance(value, float):
        return f"{value:.1f}".rstrip("0").rstrip(".") if value != int(value) else str(int(value))
    return str(value)


def write_snapshot(generated: int, rows: list[dict], path: str) -> None:
    coverage = f"{SOUTH:.1f},{WEST:.1f},{NORTH:.1f},{EAST:.1f}"
    lines = [f"#corridor-traffic v1 generated={generated} coverage={coverage}"]
    for row in rows:
        fields = [
            row["icao"], row["callsign"], f"{row['lat']:.4f}", f"{row['lon']:.4f}",
            _field(row["alt_ft"]), _field(row["groundspeed_kt"]), _field(row["track_deg"]),
            str(row["seen_epoch"]),
        ]
        lines.append("\t".join(fields))
    publish(path, "\n".join(lines) + "\n", feed=FEED_NAME, epoch=generated)


def store(generated: int, rows: list[dict], database_url: str) -> int:
    import psycopg

    stored = 0
    with psycopg.connect(database_url) as connection:
        connection.execute(ENSURE_SQL)
        for row in rows:
            wkt = f"POINT({row['lon']} {row['lat']})"
            connection.execute(UPSERT_SQL, {**row, "wkt": wkt})
            stored += 1
        connection.execute(PURGE_SQL)
        connection.execute(FEED_OK_SQL, {"feed": FEED_NAME})
        connection.commit()
    return stored


def run_once(dry_run: bool, snapshot_path: str | None) -> None:
    generated, rows = normalize(fetch_states())
    stamp = datetime.now(timezone.utc).isoformat(timespec="seconds")
    if dry_run:
        print(f"fetched {len(rows)} airborne ADS-B aircraft (dry run, nothing stored)",
              file=sys.stderr)
        for row in rows[:4]:
            print(json.dumps(row))
        return
    if not snapshot_path and not os.environ.get("DATABASE_URL"):
        sys.exit("nothing to do: pass --snapshot PATH, set DATABASE_URL, or use --dry-run")
    if snapshot_path:
        # Zero aircraft is a real answer, so it is published too.
        write_snapshot(generated, rows, snapshot_path)
        print(f"{stamp} wrote {len(rows)} aircraft at {generated} to {snapshot_path}",
              file=sys.stderr)
    database_url = os.environ.get("DATABASE_URL")
    if database_url:
        stored = store(generated, rows, database_url)
        print(f"{stamp} stored {stored} aircraft", file=sys.stderr)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dry-run", action="store_true", help="print instead of storing")
    parser.add_argument("--snapshot", help="write the snapshot file the C++ service reads")
    parser.add_argument("--loop", type=int, default=0, help="repeat every N seconds")
    args = parser.parse_args()

    while True:
        try:
            run_once(args.dry_run, args.snapshot)
        except Exception as error:  # keep the loop alive through feed outages
            print(f"traffic ingest failed: {error}", file=sys.stderr)
            if not args.loop:
                sys.exit(1)
        if not args.loop:
            break
        time.sleep(args.loop)


if __name__ == "__main__":
    main()
