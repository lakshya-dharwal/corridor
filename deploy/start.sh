#!/bin/sh
# One process tree for the public service: the feed workers, then the C++ server.
# Render (and anything else) terminates TLS and sets PORT. Postgres is not used.
set -eu

mkdir -p /var/lib/corridor/history
cd /app

STATIONS="${STATIONS:-KPHX,KIXD,KOJC,KDEN,KSEA,KABQ,KLAS,KSLC,KDFW,KORD,KMSP,KATL,KLAX,KSFO}"

supervise() {
    while true; do
        "$@" || true
        sleep 5
    done
}

supervise python3 ingest/metar_worker.py --ids "$STATIONS" --snapshot "$CORRIDOR_WEATHER_SNAPSHOT" --loop 300 &
supervise python3 ingest/hazard_worker.py --snapshot "$CORRIDOR_HAZARD_SNAPSHOT" --loop 300 &
supervise python3 ingest/taf_worker.py --ids "$STATIONS" --snapshot "$CORRIDOR_TAF_SNAPSHOT" --loop 300 &
supervise python3 ingest/wind_worker.py --snapshot "$CORRIDOR_WIND_SNAPSHOT" --loop 300 &
supervise python3 ingest/traffic_worker.py --snapshot "$CORRIDOR_TRAFFIC_SNAPSHOT" --loop 120 &

corridor_server "${PORT:-8080}" "$CORRIDOR_WEATHER_SNAPSHOT" "$CORRIDOR_HAZARD_SNAPSHOT" &
server=$!

stop() {
    kill "$server" 2>/dev/null || true
    kill $(jobs -p) 2>/dev/null || true
    wait || true
}
trap stop TERM INT
wait "$server"
