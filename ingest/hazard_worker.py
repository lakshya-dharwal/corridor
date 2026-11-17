"""Pulls SIGMET and G-AIRMET hazard areas from aviationweather.gov.

    python3 hazard_worker.py --dry-run
    python3 hazard_worker.py --snapshot hazards.tsv --loop 300

--snapshot publishes the active areas to the file the C++ service hot-reloads.
If DATABASE_URL is set, every area is also stored in Postgres/PostGIS.

G-AIRMET parsing was built against live data. No SIGMET was active when this was
written, so normalize_sigmet follows the documented field names and has not yet
been exercised on a real one.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import time
import urllib.request
from datetime import datetime, timezone

SIGMET_URL = "https://aviationweather.gov/api/data/airsigmet?format=json"
GAIRMET_URL = "https://aviationweather.gov/api/data/gairmet?format=json"
FEED_NAME = "hazards"

# G-AIRMET products that are areas a pilot should be warned about. Freezing-level
# lines (FZLVL, M_FZLVL) are reference information, not hazards.
GAIRMET_HAZARDS = {"TURB-LO", "TURB-HI", "ICE", "IFR", "MT_OBSC", "LLWS", "SFC_WND"}

# Column order of the snapshot file (engine/include/corridor/hazards.hpp).
SNAPSHOT_FIELDS = ["id", "kind", "hazard_type", "valid_from", "valid_to", "low_ft", "high_ft"]

UPSERT_SQL = """
INSERT INTO hazards (id, kind, hazard_type, valid_from, valid_to, geom, payload)
VALUES (%(id)s, %(kind)s, %(hazard_type)s, to_timestamp(%(valid_from)s),
        to_timestamp(%(valid_to)s), ST_GeogFromText(%(wkt)s), %(payload)s)
ON CONFLICT (id) DO UPDATE SET valid_to = EXCLUDED.valid_to, payload = EXCLUDED.payload
"""

PURGE_SQL = "DELETE FROM hazards WHERE valid_to < now() - interval '1 day'"

FEED_OK_SQL = """
INSERT INTO feed_status (feed, last_success_at, last_error) VALUES (%(feed)s, now(), NULL)
ON CONFLICT (feed) DO UPDATE SET last_success_at = now(), last_error = NULL
"""


def fetch_json(url: str, timeout: float = 20.0, attempts: int = 3) -> list[dict]:
    """An empty body (HTTP 204) means the feed is up and has nothing active.

    The SIGMET endpoint times out intermittently, so network errors are retried
    with a growing pause before the failure is allowed to surface."""
    request = urllib.request.Request(url, headers={"User-Agent": "corridor-ingest/0.1"})
    for attempt in range(1, attempts + 1):
        try:
            with urllib.request.urlopen(request, timeout=timeout) as response:
                body = response.read().decode("utf-8").strip()
            return json.loads(body) if body else []
        except OSError as error:  # URLError and socket timeouts are both OSError
            if attempt == attempts:
                raise
            print(f"fetch attempt {attempt} failed ({error}); retrying", file=sys.stderr)
            time.sleep(2 * attempt)
    return []


def level_to_feet(value, freezing_level_base=None) -> int | None:
    """G-AIRMET levels are hundreds of feet, 'SFC', or 'FZL' (the freezing level)."""
    if value is None:
        return None
    text = str(value).strip().upper()
    if text == "SFC":
        return 0
    if text == "FZL":
        return level_to_feet(freezing_level_base) if freezing_level_base != "FZL" else None
    try:
        return int(text) * 100
    except ValueError:
        return None


def polygon_of(coords) -> list[tuple[float, float]] | None:
    """(lat, lon) ring without the repeated closing point; None if unusable."""
    try:
        points = [(float(c["lat"]), float(c["lon"])) for c in coords or []]
    except (KeyError, TypeError, ValueError):
        return None
    if len(points) > 1 and points[0] == points[-1]:
        points.pop()
    if len(points) < 3:
        return None
    if any(abs(lat) > 90 or abs(lon) > 180 for lat, lon in points):
        return None
    return points


def make_id(kind: str, hazard: str, tag: str, valid_from: int, polygon) -> str:
    """The feeds have no unique id, so derive a stable one from the content.
    Re-fetching the same area yields the same id, which keeps upserts idempotent."""
    digest = hashlib.sha1(f"{kind}|{hazard}|{tag}|{valid_from}|{polygon}".encode()).hexdigest()
    return f"{kind}-{hazard}-{tag}-{digest[:8]}".replace(" ", "_")


def normalize_gairmet(item: dict) -> dict | None:
    hazard = item.get("hazard")
    if item.get("geom") != "AREA" or hazard not in GAIRMET_HAZARDS:
        return None
    polygon = polygon_of(item.get("coords"))
    if polygon is None or not item.get("validTime") or not item.get("expireTime"):
        return None
    valid_from = int(
        datetime.fromisoformat(item["validTime"].replace("Z", "+00:00")).timestamp()
    )
    return {
        "id": make_id("GAIRMET", hazard, str(item.get("tag", "")), valid_from, polygon),
        "kind": "G-AIRMET",
        "hazard_type": hazard,
        "valid_from": valid_from,
        "valid_to": int(item["expireTime"]),
        "low_ft": level_to_feet(item.get("base"), item.get("fzlbase")),
        "high_ft": level_to_feet(item.get("top")),
        "polygon": polygon,
        "payload": json.dumps(item),
    }


def normalize_sigmet(item: dict) -> dict | None:
    polygon = polygon_of(item.get("coords"))
    valid_from, valid_to = item.get("validTimeFrom"), item.get("validTimeTo")
    if polygon is None or valid_from is None or valid_to is None:
        return None
    hazard = str(item.get("hazard") or "UNKNOWN").upper().replace(" ", "_")
    tag = str(item.get("airSigmetId") or item.get("seriesId") or "")
    return {
        "id": make_id("SIGMET", hazard, tag, int(valid_from), polygon),
        "kind": "SIGMET",
        "hazard_type": hazard,
        "valid_from": int(valid_from),
        "valid_to": int(valid_to),
        "low_ft": item.get("altitudeLow1"),
        "high_ft": item.get("altitudeHi1"),
        "polygon": polygon,
        "payload": json.dumps(item),
    }


def fetch_hazards() -> list[dict]:
    """Raises if either feed is unreachable, so a partial outage never publishes
    a snapshot that silently drops a whole class of hazard."""
    areas = [normalize_sigmet(item) for item in fetch_json(SIGMET_URL)]
    areas += [normalize_gairmet(item) for item in fetch_json(GAIRMET_URL)]
    unique = {area["id"]: area for area in areas if area is not None}
    return list(unique.values())


def write_snapshot(areas: list[dict], path: str) -> None:
    lines = [f"#corridor-hazards v1 generated={int(time.time())}"]
    for area in areas:
        fields = ["" if area[name] is None else str(area[name]) for name in SNAPSHOT_FIELDS]
        fields.append(";".join(f"{lat:.4f},{lon:.4f}" for lat, lon in area["polygon"]))
        lines.append("\t".join(fields))
    temp_path = f"{path}.tmp"
    with open(temp_path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    os.replace(temp_path, path)  # atomic: the service never sees a partial file


def to_wkt(polygon) -> str:
    ring = polygon + [polygon[0]]  # PostGIS wants a closed ring, in lon lat order
    return "POLYGON((" + ", ".join(f"{lon} {lat}" for lat, lon in ring) + "))"


def store(areas: list[dict], database_url: str) -> tuple[int, int]:
    import psycopg

    stored = skipped = 0
    with psycopg.connect(database_url) as connection:
        for area in areas:
            try:
                with connection.transaction():  # savepoint: one bad shape skips one row
                    connection.execute(UPSERT_SQL, {**area, "wkt": to_wkt(area["polygon"])})
                stored += 1
            except psycopg.Error as error:
                skipped += 1
                print(f"skipped {area['id']}: {error}", file=sys.stderr)
        connection.execute(PURGE_SQL)
        connection.execute(FEED_OK_SQL, {"feed": FEED_NAME})
    return stored, skipped


def run_once(dry_run: bool, snapshot_path: str | None) -> None:
    areas = fetch_hazards()
    stamp = datetime.now(timezone.utc).isoformat(timespec="seconds")
    if dry_run:
        for area in areas:
            print(f"{area['id']:<34} {area['hazard_type']:<8} "
                  f"{str(area['low_ft']):>6} to {str(area['high_ft']):<6} ft  "
                  f"{len(area['polygon'])} points")
        print(f"fetched {len(areas)} hazard areas (dry run, nothing stored)", file=sys.stderr)
        return

    database_url = os.environ.get("DATABASE_URL")
    if not snapshot_path and not database_url:
        sys.exit("nothing to do: pass --snapshot PATH, set DATABASE_URL, or use --dry-run")
    if snapshot_path:
        # Zero areas is a real answer (quiet weather), so it is published too.
        write_snapshot(areas, snapshot_path)
        print(f"{stamp} wrote {len(areas)} hazard areas to {snapshot_path}", file=sys.stderr)
    if database_url:
        stored, skipped = store(areas, database_url)
        print(f"{stamp} upserted {stored} hazard areas, skipped {skipped}", file=sys.stderr)


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
            print(f"hazard ingest failed: {error}", file=sys.stderr)
            if not args.loop:
                sys.exit(1)
        if not args.loop:
            break
        time.sleep(args.loop)


if __name__ == "__main__":
    main()
