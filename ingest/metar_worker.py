"""Pulls current METARs from aviationweather.gov and upserts them into Postgres.

    python3 metar_worker.py --ids KPHX,KIXD --dry-run                     # fetch and print
    python3 metar_worker.py --ids KPHX,KIXD --snapshot weather.tsv         # publish for the service
    python3 metar_worker.py --ids KPHX,KIXD --snapshot weather.tsv --loop 300

--snapshot publishes the latest observations to the file the C++ service hot-reloads.
If DATABASE_URL is set, every observation is also stored in Postgres for history.
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

API_URL = "https://aviationweather.gov/api/data/metar"
HPA_PER_INHG = 33.8639
FEET_PER_METER = 3.28084
CEILING_COVERS = {"BKN", "OVC", "OVX"}

# Column order of the snapshot file read by the C++ service (engine/include/corridor/weather.hpp).
SNAPSHOT_FIELDS = [
    "station", "lat", "lon", "elevation_ft", "observed_epoch", "flight_category", "ceiling_ft",
    "visibility_sm", "wind_dir_deg", "wind_speed_kt", "temp_c", "altimeter_inhg", "raw_text",
]

UPSERT_SQL = """
INSERT INTO metars (station, observed_at, flight_category, ceiling_ft, visibility_sm,
                    wind_dir_deg, wind_speed_kt, temp_c, altimeter_inhg, raw_text, payload)
VALUES (%(station)s, %(observed_at)s, %(flight_category)s, %(ceiling_ft)s, %(visibility_sm)s,
        %(wind_dir_deg)s, %(wind_speed_kt)s, %(temp_c)s, %(altimeter_inhg)s, %(raw_text)s,
        %(payload)s)
ON CONFLICT (station, observed_at) DO NOTHING
"""

FEED_NAME = "metar"

AIRPORT_SQL = """
INSERT INTO airports (icao, name, elevation_ft, geom)
VALUES (%(station)s, %(name)s, %(elevation_ft)s,
        ST_SetSRID(ST_MakePoint(%(lon)s, %(lat)s), 4326)::geography)
ON CONFLICT (icao) DO UPDATE
SET name = EXCLUDED.name, elevation_ft = EXCLUDED.elevation_ft, geom = EXCLUDED.geom
"""

FEED_OK_SQL = """
INSERT INTO feed_status (feed, last_success_at, last_error) VALUES (%(feed)s, now(), NULL)
ON CONFLICT (feed) DO UPDATE SET last_success_at = now(), last_error = NULL
"""

FEED_ERROR_SQL = """
INSERT INTO feed_status (feed, last_error) VALUES (%(feed)s, %(error)s)
ON CONFLICT (feed) DO UPDATE SET last_error = EXCLUDED.last_error
"""


def fetch_metars(station_ids: list[str], timeout: float = 15.0) -> list[dict]:
    query = urllib.parse.urlencode({"ids": ",".join(station_ids), "format": "json"})
    request = urllib.request.Request(
        f"{API_URL}?{query}", headers={"User-Agent": "corridor-ingest/0.1"}
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        body = response.read().decode("utf-8").strip()
    return json.loads(body) if body else []


def parse_visibility(value) -> float | None:
    """The API reports visibility as a number or a string such as '10+'."""
    if value is None or value == "":
        return None
    try:
        return float(str(value).rstrip("+"))
    except ValueError:
        return None


def ceiling_ft(clouds: list[dict] | None) -> int | None:
    """Lowest broken or overcast layer, or None when the sky has no ceiling."""
    bases = [
        layer["base"]
        for layer in clouds or []
        if layer.get("cover") in CEILING_COVERS and layer.get("base") is not None
    ]
    return min(bases) if bases else None


def flight_category(ceiling: int | None, visibility_sm: float | None) -> str:
    """Standard FAA categories from ceiling and visibility; the worse of the two wins."""
    ceil = ceiling if ceiling is not None else 99999
    vis = visibility_sm if visibility_sm is not None else 99.0
    if ceil < 500 or vis < 1:
        return "LIFR"
    if ceil < 1000 or vis < 3:
        return "IFR"
    if ceil <= 3000 or vis <= 5:
        return "MVFR"
    return "VFR"


def normalize(row: dict) -> dict:
    visibility = parse_visibility(row.get("visib"))
    ceiling = ceiling_ft(row.get("clouds"))
    altim_hpa = row.get("altim")
    wind_dir = row.get("wdir")
    elev_m = row.get("elev")
    return {
        "station": row["icaoId"],
        "name": row.get("name") or row["icaoId"],
        "lat": row.get("lat"),
        "lon": row.get("lon"),
        "elevation_ft": round(elev_m * FEET_PER_METER) if elev_m is not None else None,
        "observed_epoch": int(row["obsTime"]),
        "observed_at": datetime.fromtimestamp(int(row["obsTime"]), tz=timezone.utc),
        "flight_category": flight_category(ceiling, visibility),
        "ceiling_ft": ceiling,
        "visibility_sm": visibility,
        "wind_dir_deg": wind_dir if isinstance(wind_dir, (int, float)) else None,  # "VRB"
        "wind_speed_kt": row.get("wspd"),
        "temp_c": row.get("temp"),
        "altimeter_inhg": round(altim_hpa / HPA_PER_INHG, 2) if altim_hpa else None,
        "raw_text": row.get("rawOb"),
        "payload": json.dumps(row),
    }


def store(rows: list[dict], database_url: str) -> int:
    import psycopg  # imported here so --dry-run works without the driver installed

    located = [
        row for row in rows
        if row["lat"] is not None and row["lon"] is not None and row["elevation_ft"] is not None
    ]
    # One transaction: airports, observations and the feed heartbeat land together.
    with psycopg.connect(database_url) as connection:
        with connection.cursor() as cursor:
            cursor.executemany(AIRPORT_SQL, located)
            cursor.executemany(UPSERT_SQL, rows)
            cursor.execute(FEED_OK_SQL, {"feed": FEED_NAME})
    return len(rows)


def record_feed_error(database_url: str, error: Exception) -> None:
    """Best effort: a failing feed should be visible in feed_status, but a failure
    to record it must not mask the original error."""
    try:
        import psycopg

        with psycopg.connect(database_url, connect_timeout=5) as connection:
            connection.execute(FEED_ERROR_SQL, {"feed": FEED_NAME, "error": str(error)[:500]})
    except Exception:
        pass


def write_snapshot(rows: list[dict], path: str) -> int:
    """Publishes the latest observations for the service. Written to a temp file
    and renamed, so the service never reads a half-written snapshot. The same
    bytes are kept under history/weather so that generation can be replayed."""
    epoch = int(time.time())
    lines = [f"#corridor-weather v1 generated={epoch}"]
    written = 0
    for row in rows:
        if row["lat"] is None or row["lon"] is None or row["elevation_ft"] is None:
            continue  # the service needs a position to use the station as an airport
        fields = ["" if row[name] is None else str(row[name]) for name in SNAPSHOT_FIELDS]
        lines.append("\t".join(field.replace("\t", " ").replace("\n", " ") for field in fields))
        written += 1
    publish(path, "\n".join(lines) + "\n", feed="weather", epoch=epoch)
    return written


def run_once(station_ids: list[str], dry_run: bool, snapshot_path: str | None) -> None:
    rows = [normalize(row) for row in fetch_metars(station_ids)]
    stamp = datetime.now(timezone.utc).isoformat(timespec="seconds")
    if dry_run:
        for row in rows:
            printable = {k: str(v) for k, v in row.items() if k != "payload"}
            print(json.dumps(printable, indent=2))
        print(f"fetched {len(rows)} METARs (dry run, nothing stored)", file=sys.stderr)
        return

    database_url = os.environ.get("DATABASE_URL")
    if not snapshot_path and not database_url:
        sys.exit("nothing to do: pass --snapshot PATH, set DATABASE_URL, or use --dry-run")
    if not rows:
        # Keep the previous snapshot rather than publishing an empty one.
        print(f"{stamp} feed returned no METARs; snapshot left unchanged", file=sys.stderr)
        return
    if snapshot_path:
        written = write_snapshot(rows, snapshot_path)
        print(f"{stamp} wrote {written} stations to {snapshot_path}", file=sys.stderr)
    if database_url:
        store(rows, database_url)
        print(f"{stamp} upserted {len(rows)} METARs into the database", file=sys.stderr)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--ids", default="KPHX,KIXD", help="comma-separated ICAO station ids")
    parser.add_argument("--dry-run", action="store_true", help="print instead of storing")
    parser.add_argument("--loop", type=int, default=0, help="repeat every N seconds")
    parser.add_argument("--snapshot", help="write the snapshot file the C++ service reads")
    args = parser.parse_args()

    station_ids = [s.strip().upper() for s in args.ids.split(",") if s.strip()]
    while True:
        try:
            run_once(station_ids, args.dry_run, args.snapshot)
        except Exception as error:  # keep the loop alive through feed outages
            print(f"ingest failed: {error}", file=sys.stderr)
            if os.environ.get("DATABASE_URL") and not args.dry_run:
                record_feed_error(os.environ["DATABASE_URL"], error)
            if not args.loop:
                sys.exit(1)
        if not args.loop:
            break
        time.sleep(args.loop)


if __name__ == "__main__":
    main()
