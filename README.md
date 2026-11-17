# Corridor

Real-time flight briefing backend with a pilot physiology layer. Give it a route and it
returns the altitude profile, an estimated blood-oxygen curve, FAA supplemental-oxygen
findings (14 CFR 91.211), a fatigue score, and the weather hazard areas the route touches.

Educational project. Not medical advice and not for operational flight planning.

## Status

| Part | State |
|---|---|
| C++20 engine: atmosphere, oxygen model, 91.211 checks, fatigue | Built, tested |
| C++20 engine: great-circle geometry, corridor/hazard intersection | Built, tested |
| Briefing builder and JSON output, demo CLI | Built, tested |
| HTTP service: `/briefing`, `/airports`, `/health`, `/metrics` | Built, tested, exercised with curl |
| Live weather in the service: hot-reloaded snapshot, stale-data handling | Built, tested, run end to end with live METARs |
| METAR ingestion worker (Python) | Run live: publishes the snapshot and writes METARs, airports and feed status to Postgres |
| Postgres/PostGIS schema, Docker Compose | Running locally; schema loaded, re-runs do not duplicate rows |
| Redis | Left in Docker Compose. The service answers from snapshot files |
| Hazard areas: G-AIRMET ingestion, route intersection, entry/exit timing, altitude check | Built, tested, run end to end on live data; result matches PostGIS |
| SIGMET parsing | Written from the documented fields; none was active to test against |
| WebSocket route alerts (`/alerts`): RFC 6455 handshake and framing, per-route change detection | Built, tested, run live with two listeners |
| Saved routes that persist across connections | Built; last status is kept in `run/saved_routes.tsv` and reported on the next connection |
| Destination forecast at arrival (TAF) | Built; the group covering ETA is briefed, and a worse TEMPO or PROB is called out |
| Cruise wind and the groundspeed it makes | Built; GFS wind at cruise becomes one tailwind, and the ETA moves with it |
| Demo page: map, hazard areas, altitude and SpO2 chart, live alerts | Built; checked in a desktop browser with live data, not on a phone |
| Snapshot history and briefing replay | Built, tested; the same epochs and clock return the same briefing |
| Engine tests on every push | GitHub Actions workflow `engine` |
| Trust score | Built. SpO2 gap vs Cottrell 1995 and Muhm 2007, plus the median wind shift on 20 routes |
| Linux web service: container, TLS proxy, request ids | Built. `deploy/docker-compose.yml` starts the C++ server and the proxy |
| ADS-B traffic inside the corridor, at briefing time | Built, tested |
| Load test and a public deploy | Not started |

## Demo

One command builds the server if needed, starts the weather, hazard, forecast, wind, and traffic workers, and serves the page:

```bash
tools/run_demo.sh 8080
```

Then open http://127.0.0.1:8080/. It loads a flight from Phoenix to New Century
AirCenter in Olathe, Kansas, with current weather at both ends, the hazard areas the
route crosses, the equipped aircraft inside the corridor at that moment, the estimated
blood-oxygen curve, and a live alert feed. Pick any other
pair of airports to re-brief. The map needs internet access (Leaflet and OpenStreetMap
tiles); everything else is served by the C++ service.

## Linux web service

The same binary runs behind a proxy. The proxy terminates TLS and serves the page. The C++ process speaks plain HTTP on the Docker network and is not published to the host. `/health` is the container healthcheck: a stale feed still answers, with `"status": "degraded"`, and the check fails only when the process stops answering. Every request is one log line, `request id=… method=… path=… status=…`. The proxy sends its request id, and the server logs that same id.

```bash
docker compose -f deploy/docker-compose.yml up --build
curl -k https://127.0.0.1:8443/health
curl -k "https://127.0.0.1:8443/briefing?from=KPHX&to=KIXD&alt=13500&gs=140"
```

Open https://127.0.0.1:8443/. The certificate is for localhost, so the browser warns. Snapshot files mounted from `run/` are optional. This compose file does not start Postgres or Redis.

## Run it

```bash
cd engine
cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
./build/corridor_cli --from KPHX --to KIXD --alt 13500 --gs 140
```

Without CMake:

```bash
cd engine && mkdir -p build
c++ -std=c++20 -O2 -Iinclude src/*.cpp tests/test_engine.cpp -o build/test_engine && ./build/test_engine
c++ -std=c++20 -O2 -Iinclude src/*.cpp apps/corridor_cli.cpp -o build/corridor_cli
```

Start the service (binds to 127.0.0.1; set `CORRIDOR_HOST=0.0.0.0` inside a container):

```bash
c++ -std=c++20 -O2 -Iinclude src/*.cpp apps/corridor_server.cpp -o build/corridor_server && ./build/corridor_server 8080
```

```bash
curl "http://127.0.0.1:8080/briefing?from=KPHX&to=KIXD&alt=13500&gs=140"
```

`/briefing` parameters: `from`, `to` (required), `alt` (ft), `gs` (kt), `altimeter` (inHg),
`dep_hour` (0-24), `pressurized` (true/false), `step` (profile minutes). Invalid input
returns 400, an unknown airport 404. `/metrics` serves Prometheus counters.

The server is a small HTTP/1.1 implementation on POSIX sockets with a worker pool, GET
only, one request per connection, plus WebSocket upgrades. It has no TLS, so in
production it belongs behind a reverse proxy.

Run it with live weather and hazards (three terminals, from the repo root):

```bash
python3 ingest/metar_worker.py --ids KPHX,KIXD,KDEN,KSEA --snapshot weather.tsv --loop 300
```

```bash
python3 ingest/hazard_worker.py --snapshot hazards.tsv --loop 300
```

```bash
./engine/build/corridor_server 8080 weather.tsv hazards.tsv
```

With the database (stores every observation for history):

```bash
docker compose up -d
python3 -m venv .venv && ./.venv/bin/pip install "psycopg[binary]"
export DATABASE_URL="postgresql://corridor:corridor_dev@127.0.0.1:5432/corridor"
./.venv/bin/python ingest/metar_worker.py --ids KPHX,KIXD,KDEN,KSEA --snapshot weather.tsv --loop 300
```

The database password in `docker-compose.yml` is for local development only.

## How live data reaches the service

```
aviationweather.gov -> metar_worker.py -> weather.tsv (atomic rename) -> corridor_server
                                       -> Postgres (history, when DATABASE_URL is set)
```

- The worker publishes the latest observations to a snapshot file; the service checks it
  once a second and reloads when it changes. The service never touches the database, so a
  database outage cannot take briefings down.
- Any station the worker ingests becomes a usable airport, with position and elevation
  from the feed.
- Without an explicit `altimeter`, a briefing uses the departure METAR's altimeter setting.
- Observations older than 90 minutes are still shown, flagged `"stale": true`, and no
  longer used in calculations.
- A corrupt or missing snapshot is rejected whole and the last good one keeps being
  served. `/health` reports `degraded` when weather is missing or stale, and `/metrics`
  exposes `corridor_weather_newest_observation_age_seconds` for a stale-feed alert.

## Hazard areas

`hazard_worker.py` pulls SIGMETs and G-AIRMET areas (turbulence, icing, IFR, mountain
obscuration, low-level wind shear, surface wind) and publishes them the same way as
weather. For each area within 25 nm of the route, a briefing reports:

- `enter_min` and `exit_min`: when the flight is inside it.
- `at_your_altitude`: whether the aircraft is inside the area's altitude band during that
  time. Turbulence from FL180 to FL350 does not affect a flight at 14,500 ft. `null`
  means the feed gives no altitude band (IFR, mountain obscuration).

Areas past their own expiry time are dropped. If the feed goes quiet, `hazard_feed.status`
becomes `stale` after 30 minutes, and unexpired areas keep being reported. The worker
publishes nothing if either source feed is unreachable, so an outage never looks like
clear skies.

Cross-check on live data: for Denver to Seattle the engine found 7 areas, and an
independent PostGIS query (`ST_DWithin` on the same route and distance) also found 7.

## Forecast at arrival

`taf_worker.py` pulls TAFs for the same stations and publishes one row per forecast
group to `run/taf.tsv` (`CORRIDOR_TAF_SNAPSHOT`). A briefing takes the flight's duration,
finds the prevailing group (base, FM, or BECMG) that covers the ETA, and reports that
flight category as `arrival`. A TEMPO or PROB group in the same window is included only
when its category is worse than the prevailing one. A missing group means the TAF does
not reach the arrival; the current METAR is still shown as "now".

The alert watch treats that category like a field changing: `KIXD at arrival changed
from VFR to IFR`. A stale forecast file (older than 30 minutes) is still shown and
flagged, and it stops driving alerts until a fresh snapshot arrives. A TAF outage does
not touch the METAR snapshot.

## Wind and groundspeed

The speed in the request is airspeed. `wind_worker.py` publishes NOAA GFS winds, via
Open-Meteo, on a grid of pressure levels (`run/wind.tsv`, `CORRIDOR_WIND_SNAPSHOT`).
The FAA wind/temp tables that used to carry this were retired with the NAM model.
The engine interpolates to cruise altitude, averages the along-track component over
the route, and flies the resulting groundspeed. A tailwind shortens the flight, so
the arrival forecast is read at an earlier hour, and the time spent at altitude
changes the oxygen finding. A headwind does the opposite. With no fresh wind, the
typed airspeed is the groundspeed.

## Traffic in the corridor

`traffic_worker.py` asks the OpenSky Network for ADS-B positions inside a rectangle
that covers the demo airports (`run/traffic.tsv`, `CORRIDOR_TRAFFIC_SNAPSHOT`).
Multilateration, FLARM, aircraft on the ground, and positions already older than a
minute are dropped. A failed fetch leaves the previous file in place, so an outage
never looks like an empty sky.

A briefing lists the aircraft within 25 nm of the route and says whether each one is
within 3,000 ft of the altitude the plan uses at that point. That is where they are
now. It is not who will be there on arrival, and aircraft that are not broadcasting
are absent. The rectangle is in the snapshot header. A route that leaves it is
`partial`: the aircraft that were seen are still listed, and an empty result is not
reported as an empty sky. A file older than 8 minutes is `stale`. It is still shown,
and it stops driving alerts until a fresh snapshot arrives.

Alerts, while the picture is fresh and the route is covered, say when an aircraft
enters or leaves the corridor. They do not claim a conflict later in the flight.

## Replaying a briefing

Each publish keeps the file it wrote, under `run/history/<feed>/<epoch>.tsv`
(`CORRIDOR_HISTORY` overrides the directory). The live snapshot is unchanged:
the service still hot-reloads the latest file, and a missing history directory
does not affect a live briefing. The newest 48 generations of each feed are kept.

A briefing names the generations it used:

```json
"sources": {"evaluated_at": 1700000000, "weather": 1700000000, "hazards": null, "taf": null, "wind": null, "traffic": null}
```

Asking for those generations again returns the same inputs:

```
/briefing?from=KPHX&to=KIXD&alt=13500&gs=140&weather_at=1700000000&hazards_at=1700000000&taf_at=1700000000&wind_at=1700000000&evaluated_at=1700000000
```

A replay uses only the generations named in the query. A feed left out is
omitted, not filled in from the live file, so the result does not depend on
what the workers published later. `evaluated_at` is the clock for the arrival
time, the wind hour, and the stale checks. A missing generation is a 404. A
new snapshot epoch does not by itself raise an alert.

On 17 November 2026 a KPHX to KIXD briefing was questioned because the KIXD TAF contains a TEMPO and later FM groups, and the arrival still came back VFR. The briefing named its inputs: `evaluated_at` 1794900171 (07:22Z), weather 1794899886, hazards 1794899883, taf 1794899882, and wind 1794899882, all from 07:18Z. Replaying those generations at that clock, `/briefing?from=KPHX&to=KIXD&alt=13500&gs=140&weather_at=1794899886&hazards_at=1794899883&taf_at=1794899882&wind_at=1794899882&evaluated_at=1794900171`, returned the same result: 341.6 minutes, 16.2 kt of tailwind, and KIXD at arrival VFR, with 6 sm visibility, no ceiling, and wind 160 at 7. The ETA, 13:04Z, sits in the base group, before the 17Z FM. The 18–21Z TEMPO is gusts only, so it does not change the category. Those history files stay on the machine that published them.

## Route alerts

Open a WebSocket to `/alerts` with the same parameters as `/briefing` and the service
pushes a message whenever that route's picture changes:

```bash
python3 tools/ws_listen.py "ws://127.0.0.1:8080/alerts?from=KPHX&to=KIXD&alt=13500&gs=140"
```

```
STATUS  KPHX VFR -> KIXD VFR, 2 hazards on route, crew oxygen required: True
ALERT   KIXD flight category changed from VFR to IFR
ALERT   Hazard no longer on route: G-AIRMET IFR
```

Alerts fire on: a flight category change at either airport, the destination forecast
at arrival changing, a worse TEMPO or PROB group appearing or clearing, the along-track
wind changing by 8 kt or more, a hazard area entering or leaving the route, the oxygen
requirement changing, and weather, forecast, wind, or hazard data going stale or
recovering. Each message also carries the full current status as JSON.

How it works:

- The route and the handshake are validated before the protocol switch, so a bad request
  gets a normal HTTP error (`404` unknown airport, `426` no upgrade header).
- One hub thread owns every WebSocket. HTTP workers hand the socket over and go back to
  serving requests, so idle listeners never use up the worker pool.
- Routes are re-evaluated only when a snapshot reloads, plus once a minute to catch data
  going stale. Unchanged routes get no message.
- Closing the connection does not forget the route. The last status is written to
  `run/saved_routes.tsv` (`CORRIDOR_SAVED_ROUTES` overrides the path). The next connection
  for the same flight gets `"resumed": true`, and an `alert` listing what changed while
  nobody was listening. A missing journal only means that connection starts fresh. The
  `saved_routes` table in Postgres is the same idea and is not written by the service.
- A client that stops reading is dropped after a 2 second send timeout. The hub pings
  every 30 seconds, answers pings, and sends close code 1001 on shutdown.

## The physiology model

1. Cabin altitude (capped at 8,000 ft when pressurized), corrected to pressure altitude
   with the altimeter setting from the METAR.
2. Ambient pressure from the International Standard Atmosphere.
3. Alveolar oxygen from the alveolar gas equation, with alveolar CO2 falling at altitude
   as hypoxia drives hyperventilation.
4. SpO2 from the Severinghaus oxygen-hemoglobin dissociation curve.

Known limits: it ignores the left shift of the dissociation curve from respiratory
alkalosis, so it reads a few points low at altitude (conservative). It models a healthy,
unacclimatized adult at rest. The fatigue score is a heuristic, not a validated model.

A briefing carries a trust score. The SpO2 gap is the model's miss against two published
measurements: Cottrell 1995 (preflight mean 97.0%) and Muhm 2007 (a 4.4 point drop from
650 ft to 8,000 ft in 502 unacclimatized adults). The wind term is the median change in
the 91.211 oxygen call across 20 fixed routes, flown at 140 kt and again with the
forecast wind. A stale feed takes 30 points off. The same snapshots replay the same score.

## Layout

- `engine/` C++20 library, CLI, and tests
- `ingest/` Python feed workers, including `traffic_worker.py`
- `tools/ws_listen.py` WebSocket client for watching alerts
- `tools/run_demo.sh` one-command demo launcher
- `web/index.html` the demo page
- `db/schema.sql` Postgres/PostGIS schema
- `docker-compose.yml` local Postgres and Redis
