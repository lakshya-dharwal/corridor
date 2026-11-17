"""Publishes forecast wind above the route.

The FAA wind/temp tables were retired with the NAM model, so this reads NOAA GFS
pressure-level winds through Open-Meteo. A grid of points covers the demo's
airports. The C++ service interpolates them to cruise altitude and turns the
wind into one along-track component.

    python3 wind_worker.py --dry-run
    python3 wind_worker.py --snapshot wind.tsv --loop 300

A failed fetch leaves the previous snapshot in place.
"""

from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.parse
import urllib.request
from datetime import datetime, timezone

try:
    from publish import publish
except ImportError:  # invoked as a module from the repo root
    from ingest.publish import publish

API_URL = "https://api.open-meteo.com/v1/forecast"

# Standard-atmosphere heights of the GFS pressure levels the cruise band sits in.
LEVELS = ((850, 4780), (700, 9880), (600, 13780), (500, 18280))

# Coarse enough for one request, dense enough that a US route stays inside the
# 500 nm the engine will trust.
LATS = (32.0, 36.0, 40.0, 44.0, 47.0)
LONS = (-122.0, -115.0, -108.0, -101.0, -94.0, -87.0, -80.0)


def fetch(timeout: float = 30.0) -> list[dict]:
    hourly = ",".join(
        name
        for hpa, _ft in LEVELS
        for name in (f"wind_speed_{hpa}hPa", f"wind_direction_{hpa}hPa")
    )
    query = urllib.parse.urlencode({
        "latitude": ",".join(str(lat) for lat in LATS for _lon in LONS),
        "longitude": ",".join(str(lon) for _lat in LATS for lon in LONS),
        "hourly": hourly,
        "wind_speed_unit": "kn",
        "forecast_hours": "18",
        "timezone": "GMT",
    })
    request = urllib.request.Request(
        f"{API_URL}?{query}", headers={"User-Agent": "corridor-ingest/0.1"}
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        body = json.loads(response.read().decode("utf-8"))
    return body if isinstance(body, list) else [body]


def records_of(points: list[dict]) -> list[dict]:
    rows = []
    for point in points:
        hourly = point.get("hourly") or {}
        times = hourly.get("time") or []
        for index, stamp in enumerate(times):
            epoch = int(datetime.fromisoformat(stamp).replace(tzinfo=timezone.utc).timestamp())
            for hpa, alt_ft in LEVELS:
                speed = (hourly.get(f"wind_speed_{hpa}hPa") or [None])[index]
                direction = (hourly.get(f"wind_direction_{hpa}hPa") or [None])[index]
                if speed is None or direction is None:
                    continue
                rows.append({
                    "lat": point["latitude"],
                    "lon": point["longitude"],
                    "valid_epoch": epoch,
                    "alt_ft": alt_ft,
                    "dir_deg": direction,
                    "speed_kt": speed,
                })
    return rows


def write_snapshot(rows: list[dict], path: str) -> int:
    epoch = int(time.time())
    lines = [f"#corridor-wind v1 generated={epoch}"]
    for row in rows:
        fields = [
            f"{row['lat']:.4f}", f"{row['lon']:.4f}", str(row["valid_epoch"]),
            str(row["alt_ft"]), str(row["dir_deg"]), str(row["speed_kt"]),
        ]
        lines.append("\t".join(fields))
    publish(path, "\n".join(lines) + "\n", feed="wind", epoch=epoch)
    return len(rows)


def run_once(dry_run: bool, snapshot_path: str | None) -> None:
    rows = records_of(fetch())
    stamp = datetime.now(timezone.utc).isoformat(timespec="seconds")
    if dry_run:
        print(f"fetched {len(rows)} wind points (dry run, nothing stored)", file=sys.stderr)
        for row in rows[:4]:
            print(json.dumps(row))
        return
    if not snapshot_path:
        sys.exit("nothing to do: pass --snapshot PATH or use --dry-run")
    if not rows:
        print(f"{stamp} feed returned no winds; snapshot left unchanged", file=sys.stderr)
        return
    written = write_snapshot(rows, snapshot_path)
    print(f"{stamp} wrote {written} wind points to {snapshot_path}", file=sys.stderr)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dry-run", action="store_true", help="print instead of storing")
    parser.add_argument("--loop", type=int, default=0, help="repeat every N seconds")
    parser.add_argument("--snapshot", help="write the snapshot file the C++ service reads")
    args = parser.parse_args()
    while True:
        try:
            run_once(args.dry_run, args.snapshot)
        except Exception as error:
            print(f"ingest failed: {error}", file=sys.stderr)
            if not args.loop:
                sys.exit(1)
        if not args.loop:
            break
        time.sleep(args.loop)


if __name__ == "__main__":
    main()
