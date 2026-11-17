# The briefing process. It speaks plain HTTP on the private network.
# TLS and the demo page belong to the proxy in front of it.
FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends g++ cmake make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY engine/ engine/
RUN cmake -S engine -B build && cmake --build build --target corridor_server -j

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/build/corridor_server /usr/local/bin/corridor_server

EXPOSE 8080
# A feed can be stale and /health still answers 200 with "degraded". The check
# fails only when the process stops answering, which is when the container should.
HEALTHCHECK --interval=10s --timeout=3s --start-period=5s --retries=3 \
    CMD curl -fsS http://127.0.0.1:8080/health || exit 1

ENTRYPOINT ["corridor_server"]
