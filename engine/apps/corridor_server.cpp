// Corridor HTTP service.
//
//   corridor_server [port] [weather_snapshot_path] [hazard_snapshot_path]
//
//   CORRIDOR_PORT               port, default 8080
//   CORRIDOR_HOST               bind address, default 127.0.0.1
//   CORRIDOR_WEATHER_SNAPSHOT   snapshot file written by ingest/metar_worker.py
//   CORRIDOR_HAZARD_SNAPSHOT    snapshot file written by ingest/hazard_worker.py
//   CORRIDOR_WEB_ROOT           folder holding the demo page, default "web"
//
// Routes: /  (demo page)  /health  /airports  /briefing  /hazards  /metrics
//         /alerts  (WebSocket; same query parameters as /briefing)
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "corridor/alerts.hpp"
#include "corridor/api.hpp"
#include "corridor/hazards.hpp"
#include "corridor/http.hpp"
#include "corridor/weather.hpp"
#include "corridor/websocket.hpp"

using namespace corridor;

namespace {

HttpServer* g_server = nullptr;

void on_signal(int) {
    if (g_server) g_server->stop();
}

std::int64_t now_epoch() { return static_cast<std::int64_t>(std::time(nullptr)); }

std::string setting(int argc, char** argv, int index, const char* env_name,
                    const char* fallback) {
    if (argc > index) return argv[index];
    const char* value = std::getenv(env_name);
    return value ? value : fallback;
}

}  // namespace

int main(int argc, char** argv) {
    const int port = std::atoi(setting(argc, argv, 1, "CORRIDOR_PORT", "8080").c_str());
    const std::string host = setting(0, nullptr, 1, "CORRIDOR_HOST", "127.0.0.1");
    const std::string weather_path = setting(argc, argv, 2, "CORRIDOR_WEATHER_SNAPSHOT", "");
    const std::string hazard_path = setting(argc, argv, 3, "CORRIDOR_HAZARD_SNAPSHOT", "");
    const std::string web_root = setting(0, nullptr, 1, "CORRIDOR_WEB_ROOT", "web");
    if (port <= 0 || port > 65535) {
        std::fprintf(stderr, "invalid port\n");
        return 2;
    }

    const AirportTable& airports = demo_airports();
    std::unique_ptr<WeatherStore> weather_owner;
    std::unique_ptr<HazardStore> hazard_owner;
    if (!weather_path.empty()) weather_owner = std::make_unique<WeatherStore>(weather_path);
    if (!hazard_path.empty()) hazard_owner = std::make_unique<HazardStore>(hazard_path);
    WeatherStore* weather = weather_owner.get();
    HazardStore* hazards = hazard_owner.get();

    // Holding the shared_ptrs keeps one consistent snapshot alive for a whole
    // evaluation even if a reload swaps in a newer one meanwhile.
    auto status_of = [&](const Request& request, Response* error) {
        const auto wx = weather ? weather->current() : nullptr;
        const auto hz = hazards ? hazards->current() : nullptr;
        return route_status(request, airports, wx.get(), now_epoch(), hz.get(), error);
    };

    AlertHub hub(
        [&](const Request& request) { return status_of(request, nullptr); },
        [&]() -> std::uint64_t {
            // current() is what notices a changed file, so the hub's tick doubles
            // as the reload check when no HTTP requests are arriving.
            if (weather) weather->current();
            if (hazards) hazards->current();
            return (weather ? weather->reloads() : 0) + (hazards ? hazards->reloads() : 0);
        });

    HttpServer server;
    server.get("/health", [&](const Request&) {
        return health_endpoint(weather, now_epoch(), hazards);
    });
    server.get("/airports", [&](const Request&) {
        const auto wx = weather ? weather->current() : nullptr;
        return airports_endpoint(airports, wx.get());
    });
    server.get("/briefing", [&](const Request& request) {
        const auto wx = weather ? weather->current() : nullptr;
        const auto hz = hazards ? hazards->current() : nullptr;
        return briefing_endpoint(request, airports, wx.get(), now_epoch(), hz.get());
    });
    server.get("/hazards", [&](const Request&) {
        const auto hz = hazards ? hazards->current() : nullptr;
        return hazards_endpoint(hz.get(), now_epoch());
    });
    // The demo page. Only this one fixed file is served; no request path ever
    // reaches the filesystem.
    server.get("/", [&](const Request&) {
        std::ifstream file(web_root + "/index.html", std::ios::binary);
        if (!file) return error_json(404, "demo page not found; set CORRIDOR_WEB_ROOT");
        std::ostringstream body;
        body << file.rdbuf();
        Response response;
        response.content_type = "text/html; charset=utf-8";
        response.body = body.str();
        return response;
    });
    server.get("/metrics", [&](const Request&) {
        Response response;
        response.content_type = "text/plain; version=0.0.4";
        response.body = server.metrics_text() + hub.metrics_text();
        if (weather) response.body += weather_metrics_text(*weather, now_epoch());
        if (hazards) response.body += hazard_metrics_text(*hazards, now_epoch());
        return response;
    });
    server.upgrade("/alerts", [&](const Request& request, int fd) -> std::optional<Response> {
        // Validate everything before switching protocols, so a bad route or a bad
        // handshake gets an ordinary HTTP error the client can read.
        Response refusal;
        const std::optional<RouteStatus> status = status_of(request, &refusal);
        if (!status) return refusal;
        std::string why;
        const std::optional<std::string> handshake = websocket_handshake_response(request, why);
        if (!handshake) return error_json(400, why);
        if (!hub.add(fd, request, *status, *handshake)) {
            return error_json(503, "too many alert connections");
        }
        return std::nullopt;  // the hub owns the socket now
    });

    g_server = &server;
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    std::signal(SIGPIPE, SIG_IGN);

    hub.start();
    std::fprintf(stderr, "corridor_server listening on %s:%d, weather: %s, hazards: %s\n",
                 host.c_str(), port, weather ? weather_path.c_str() : "not configured",
                 hazards ? hazard_path.c_str() : "not configured");
    // Browsers open spare connections and hold them idle, so leave headroom.
    const bool started = server.listen(host, static_cast<std::uint16_t>(port), 8);
    hub.stop();
    if (!started) {
        std::perror("could not start server");
        return 1;
    }
    std::fprintf(stderr, "corridor_server stopped cleanly\n");
    return 0;
}
