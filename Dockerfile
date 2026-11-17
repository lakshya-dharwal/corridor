# Public Corridor service. One container runs the ingest workers and the C++ server.
# The host in front of it (Render) terminates TLS. The process speaks plain HTTP.
FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends g++ cmake make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY engine/ engine/
RUN cmake -S engine -B build && cmake --build build --target corridor_server -j

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends python3 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --create-home --shell /usr/sbin/nologin corridor

COPY --from=build /src/build/corridor_server /usr/local/bin/corridor_server
COPY ingest/ /app/ingest/
COPY web/ /app/web/
COPY deploy/start.sh /app/start.sh

RUN chmod 755 /app/start.sh \
    && mkdir -p /var/lib/corridor/history \
    && chown -R corridor:corridor /var/lib/corridor /app

USER corridor
WORKDIR /app

ENV CORRIDOR_HOST=0.0.0.0 \
    CORRIDOR_WEB_ROOT=/app/web \
    CORRIDOR_WEATHER_SNAPSHOT=/var/lib/corridor/weather.tsv \
    CORRIDOR_HAZARD_SNAPSHOT=/var/lib/corridor/hazards.tsv \
    CORRIDOR_TAF_SNAPSHOT=/var/lib/corridor/taf.tsv \
    CORRIDOR_WIND_SNAPSHOT=/var/lib/corridor/wind.tsv \
    CORRIDOR_TRAFFIC_SNAPSHOT=/var/lib/corridor/traffic.tsv \
    CORRIDOR_HISTORY=/var/lib/corridor/history \
    CORRIDOR_SAVED_ROUTES=/var/lib/corridor/saved_routes.tsv

EXPOSE 8080
CMD ["/app/start.sh"]
