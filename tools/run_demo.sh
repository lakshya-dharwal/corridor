#!/usr/bin/env bash
# Starts the whole demo: the feed workers and the server, then serves the page.
#
#   tools/run_demo.sh [port]        then open http://127.0.0.1:<port>/
#
# STATIONS overrides the airports to ingest. If DATABASE_URL is set the workers
# also store everything in Postgres.
set -euo pipefail
cd "$(dirname "$0")/.."

PORT="${1:-8080}"
STATIONS="${STATIONS:-KPHX,KIXD,KOJC,KDEN,KSEA,KABQ,KLAS,KSLC,KDFW,KORD,KMSP,KATL,KLAX,KSFO}"
PY=python3
[ -x .venv/bin/python ] && PY=.venv/bin/python

mkdir -p run engine/build
if [ ! -x engine/build/corridor_server ] || [ -n "$(find engine/src engine/include engine/apps -newer engine/build/corridor_server -print -quit)" ]; then
    echo "building corridor_server..." >&2
    c++ -std=c++20 -O2 -Iengine/include engine/src/*.cpp engine/apps/corridor_server.cpp \
        -o engine/build/corridor_server
fi

"$PY" ingest/metar_worker.py --ids "$STATIONS" --snapshot run/weather.tsv --loop 300 &
"$PY" ingest/hazard_worker.py --snapshot run/hazards.tsv --loop 300 &
"$PY" ingest/taf_worker.py --ids "$STATIONS" --snapshot run/taf.tsv --loop 300 &
"$PY" ingest/wind_worker.py --snapshot run/wind.tsv --loop 300 &
"$PY" ingest/traffic_worker.py --snapshot run/traffic.tsv --loop 300 &
trap 'kill $(jobs -p) 2>/dev/null || true' EXIT

# Give the first fetch a moment so the page opens with live data already loaded.
for _ in $(seq 1 40); do
    [ -s run/weather.tsv ] && [ -s run/hazards.tsv ] && [ -s run/taf.tsv ] && [ -s run/wind.tsv ] && [ -s run/traffic.tsv ] && break
    sleep 0.5
done

echo "demo page: http://127.0.0.1:${PORT}/" >&2
CORRIDOR_TAF_SNAPSHOT=run/taf.tsv CORRIDOR_WIND_SNAPSHOT=run/wind.tsv \
CORRIDOR_TRAFFIC_SNAPSHOT=run/traffic.tsv CORRIDOR_WEB_ROOT=web \
    engine/build/corridor_server "$PORT" run/weather.tsv run/hazards.tsv
