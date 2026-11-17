"""Pulls TAFs from aviationweather.gov and publishes the forecast groups.

    python3 taf_worker.py --ids KPHX,KIXD --dry-run
    python3 taf_worker.py --ids KPHX,KIXD --snapshot taf.tsv --loop 300

Each row is one forecast group with ceiling and visibility already resolved, so
the C++ service can pick the group that covers an arrival time. A TEMPO or PROB
line inherits the prevailing ceiling and visibility when it does not state its
own. The service hot-reloads the snapshot; a failed fetch leaves the previous
file in place.
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

API_URL = "https://aviationweather.gov/api/data/taf"
CEILING_COVERS = {"BKN", "OVC", "OVX", "VV"}
PREVAILING = {None, "FM", "BECMG"}

SNAPSHOT_FIELDS = [
    "station", "issued_epoch", "valid_from", "valid_to", "group_from", "group_to", "change",
    "prevailing", "flight_category", "ceiling_ft", "visibility_sm", "wind_dir_deg",
    "wind_speed_kt", "wind_gust_kt", "probability", "raw_text",
]


def fetch_tafs(station_ids: list[str], timeout: float = 15.0) -> list[dict]:
    query = urllib.parse.urlencode({"ids": ",".join(station_ids), "format": "json"})
    request = urllib.request.Request(
        f"{API_URL}?{query}", headers={"User-Agent": "corridor-ingest/0.1"}
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        body = response.read().decode("utf-8").strip()
    return json.loads(body) if body else []


def parse_visibility(value) -> float | None:
    """The API reports visibility as a number or a string such as '6+'."""
    if value is None or value == "":
        return None
    try:
        return float(str(value).rstrip("+"))
    except ValueError:
        return None


def ceiling_ft(group: dict) -> tuple[int | None, bool]:
    """Lowest ceiling, and whether this group stated a sky condition at all.

    An empty cloud list on a TEMPO line means 'no change', not 'clear'.
    Vertical visibility is an indefinite ceiling.
    """
    clouds = group.get("clouds") or []
    stated = bool(clouds) or group.get("vertVis") is not None
    bases = [
        layer["base"]
        for layer in clouds
        if layer.get("cover") in CEILING_COVERS and layer.get("base") is not None
    ]
    if group.get("vertVis") is not None:
        bases.append(group["vertVis"])
    return (min(bases) if bases else None), stated


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


def _wind(value):
    return value if isinstance(value, (int, float)) else None


def resolve_groups(taf: dict) -> list[dict]:
    """One record per group. Prevailing lines replace the carried state; temporary
    lines overlay only the fields they state."""
    carried = {
        "ceiling": None, "vis": None, "wdir": None, "wspd": None, "wgst": None,
    }
    issued = int(datetime.fromisoformat(taf["issueTime"].replace("Z", "+00:00")).timestamp())
    records = []
    for group in taf.get("fcsts") or []:
        change = group.get("fcstChange")
        ceiling, sky_stated = ceiling_ft(group)
        vis = parse_visibility(group.get("visib"))
        vis_stated = vis is not None
        wind_dir = _wind(group.get("wdir"))
        prevailing = change in PREVAILING

        def apply(target: dict) -> None:
            if sky_stated:
                target["ceiling"] = ceiling
            if vis_stated:
                target["vis"] = vis
            if wind_dir is not None:
                target["wdir"] = wind_dir
            if group.get("wspd") is not None:
                target["wspd"] = group["wspd"]
            if group.get("wgst") is not None:
                target["wgst"] = group["wgst"]

        if prevailing:
            if change in (None, "FM"):
                carried = {"ceiling": None, "vis": None, "wdir": None, "wspd": None, "wgst": None}
            apply(carried)
            used = carried
        else:
            used = dict(carried)
            apply(used)

        records.append({
            "station": taf["icaoId"],
            "issued_epoch": issued,
            "valid_from": int(taf["validTimeFrom"]),
            "valid_to": int(taf["validTimeTo"]),
            "group_from": int(group["timeFrom"]),
            "group_to": int(group["timeTo"]),
            "change": change or "",
            "prevailing": 1 if prevailing else 0,
            "flight_category": flight_category(used["ceiling"], used["vis"]),
            "ceiling_ft": used["ceiling"],
            "visibility_sm": used["vis"],
            "wind_dir_deg": used["wdir"],
            "wind_speed_kt": used["wspd"],
            "wind_gust_kt": used["wgst"],
            "probability": group.get("probability"),
            "raw_text": taf.get("rawTAF") or "",
        })
    return records


def write_snapshot(records: list[dict], path: str) -> int:
    epoch = int(time.time())
    lines = [f"#corridor-taf v1 generated={epoch}"]
    for record in records:
        fields = ["" if record[name] is None else str(record[name]) for name in SNAPSHOT_FIELDS]
        lines.append("\t".join(field.replace("\t", " ").replace("\n", " ") for field in fields))
    publish(path, "\n".join(lines) + "\n", feed="taf", epoch=epoch)
    return len(records)


def run_once(station_ids: list[str], dry_run: bool, snapshot_path: str | None) -> None:
    records = []
    for taf in fetch_tafs(station_ids):
        if not taf.get("fcsts") or not taf.get("icaoId"):
            continue
        records.extend(resolve_groups(taf))
    stamp = datetime.now(timezone.utc).isoformat(timespec="seconds")
    if dry_run:
        for record in records:
            print(json.dumps({k: str(v) for k, v in record.items()}))
        print(f"fetched {len(records)} forecast groups (dry run, nothing stored)", file=sys.stderr)
        return
    if not snapshot_path:
        sys.exit("nothing to do: pass --snapshot PATH or use --dry-run")
    if not records:
        print(f"{stamp} feed returned no TAFs; snapshot left unchanged", file=sys.stderr)
        return
    written = write_snapshot(records, snapshot_path)
    print(f"{stamp} wrote {written} forecast groups to {snapshot_path}", file=sys.stderr)


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
        except Exception as error:
            print(f"ingest failed: {error}", file=sys.stderr)
            if not args.loop:
                sys.exit(1)
        if not args.loop:
            break
        time.sleep(args.loop)


if __name__ == "__main__":
    main()
