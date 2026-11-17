CREATE EXTENSION IF NOT EXISTS postgis;

CREATE TABLE IF NOT EXISTS airports (
    icao          TEXT PRIMARY KEY,
    name          TEXT NOT NULL,
    elevation_ft  REAL NOT NULL,
    geom          GEOGRAPHY(POINT, 4326) NOT NULL
);
CREATE INDEX IF NOT EXISTS airports_geom_idx ON airports USING GIST (geom);

CREATE TABLE IF NOT EXISTS metars (
    station          TEXT NOT NULL,
    observed_at      TIMESTAMPTZ NOT NULL,
    flight_category  TEXT NOT NULL,
    ceiling_ft       INTEGER,
    visibility_sm    REAL,
    wind_dir_deg     REAL,
    wind_speed_kt    REAL,
    temp_c           REAL,
    altimeter_inhg   REAL,
    raw_text         TEXT,
    payload          JSONB NOT NULL,
    ingested_at      TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (station, observed_at)
);
CREATE INDEX IF NOT EXISTS metars_latest_idx ON metars (station, observed_at DESC);

-- SIGMET / AIRMET areas. The engine intersects these with the route corridor.
CREATE TABLE IF NOT EXISTS hazards (
    id           TEXT PRIMARY KEY,
    kind         TEXT NOT NULL,
    hazard_type  TEXT,
    valid_from   TIMESTAMPTZ NOT NULL,
    valid_to     TIMESTAMPTZ NOT NULL,
    geom         GEOGRAPHY(POLYGON, 4326) NOT NULL,
    payload      JSONB NOT NULL
);
CREATE INDEX IF NOT EXISTS hazards_geom_idx ON hazards USING GIST (geom);
CREATE INDEX IF NOT EXISTS hazards_valid_idx ON hazards (valid_to);

-- Routes a pilot is watching; the alert service re-briefs these when data changes.
CREATE TABLE IF NOT EXISTS saved_routes (
    id                  BIGINT GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    departure           TEXT NOT NULL REFERENCES airports (icao),
    destination         TEXT NOT NULL REFERENCES airports (icao),
    cruise_alt_ft       REAL NOT NULL,
    groundspeed_kt      REAL NOT NULL,
    pressurized         BOOLEAN NOT NULL DEFAULT false,
    last_briefing_hash  TEXT,
    created_at          TIMESTAMPTZ NOT NULL DEFAULT now()
);

-- ADS-B positions. The engine reads the snapshot file, not this table.
CREATE TABLE IF NOT EXISTS traffic (
    icao            TEXT NOT NULL,
    seen_at         TIMESTAMPTZ NOT NULL,
    callsign        TEXT,
    alt_ft          REAL,
    groundspeed_kt  REAL,
    track_deg       REAL,
    geom            GEOGRAPHY(POINT, 4326) NOT NULL,
    PRIMARY KEY (icao, seen_at)
);
CREATE INDEX IF NOT EXISTS traffic_seen_idx ON traffic (seen_at DESC);

-- One row per feed, so a stale-feed alert is a single query.
CREATE TABLE IF NOT EXISTS feed_status (
    feed             TEXT PRIMARY KEY,
    last_success_at  TIMESTAMPTZ,
    last_error       TEXT
);
