// HTTP endpoints, written as plain functions of a Request so they can be tested
// without opening a socket.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "corridor/briefing.hpp"
#include "corridor/hazards.hpp"
#include "corridor/http.hpp"
#include "corridor/taf.hpp"
#include "corridor/weather.hpp"
#include "corridor/wind.hpp"

namespace corridor {

using AirportTable = std::map<std::string, Airport>;

// METARs are issued hourly; an observation older than this is treated as stale:
// it is still shown, flagged, but no longer used in calculations.
inline constexpr std::int64_t kStaleWeatherSeconds = 90 * 60;

// The hazard worker republishes every few minutes. Past this age the feed is
// reported as stale. Areas are still used until their own expiry time, since
// each one carries its validity window.
inline constexpr std::int64_t kStaleHazardSeconds = 30 * 60;

// Built-in fallback airports, used when no weather snapshot covers a station.
const AirportTable& demo_airports();

// GET /briefing?from=KPHX&to=KIXD&alt=13500&gs=140&altimeter=29.92
//               &dep_hour=9&pressurized=false&step=5
// Airports resolve from the weather snapshot first, then the fallback table.
// Without an explicit altimeter, a fresh departure (else destination) METAR
// supplies it. Hazard areas that have expired are ignored.
// 400 on a bad parameter, 404 on an unknown airport.
Response briefing_endpoint(const Request& request, const AirportTable& airports,
                           const WeatherSnapshot* weather = nullptr,
                           std::int64_t now_epoch = 0,
                           const HazardSnapshot* hazards = nullptr,
                           const TafSnapshot* tafs = nullptr,
                           const WindSnapshot* winds = nullptr);

// Replay from archived generations. Returns nullopt when the request does not
// name one, so the caller keeps using the live snapshots. A named feed is read
// from `<history_root>/<feed>/<epoch>.tsv`; a feed that is not named is omitted,
// not filled from the live file. `evaluated_at` is the clock for the arrival
// time, the wind hour, and stale checks. If it is absent, the newest named
// generation is the clock.
std::optional<Response> replay_briefing(const Request& request, const AirportTable& airports,
                                        const std::string& history_root);

// GET /airports
Response airports_endpoint(const AirportTable& airports,
                           const WeatherSnapshot* weather = nullptr);

// GET /hazards. Every unexpired hazard area with its polygon, for drawing on a map.
Response hazards_endpoint(const HazardSnapshot* hazards = nullptr, std::int64_t now_epoch = 0);

// GET /health. "degraded" when a configured feed is missing or stale; the
// service still answers briefings in that state.
Response health_endpoint(WeatherStore* weather = nullptr, std::int64_t now_epoch = 0,
                         HazardStore* hazards = nullptr, TafStore* tafs = nullptr,
                         WindStore* winds = nullptr);

// ---------- Route status, used for alerts ----------

struct RouteHazard {
    std::string id;
    std::string label;  // e.g. "G-AIRMET ICE"
    std::optional<double> low_ft, high_ft, enter_min, exit_min;
    std::optional<bool> at_altitude;
};

// The parts of a briefing a pilot wants to be told about when they change.
struct RouteStatus {
    std::string departure, destination;
    // Flight category from a fresh METAR; empty when there is none.
    std::string departure_category, destination_category;
    // Prevailing TAF category at ETA, and a worse temporary group ("TEMPO LIFR").
    // Empty when the forecast feed is missing or stale.
    std::string arrival_category, arrival_tempo;
    // Along-track wind actually used. Positive is a tailwind. Empty when the
    // briefing flew the typed airspeed.
    std::optional<int> tailwind_kt;
    bool crew_oxygen_required = false;
    bool passenger_oxygen_required = false;
    double min_spo2 = 1.0;
    std::string hazard_feed = "not configured";  // "ok", "stale" or "not configured"
    std::vector<RouteHazard> hazards;
    // Generations this status was computed from. Zero means that feed was not
    // used. A newer epoch with the same picture is not a change.
    std::int64_t evaluated_at = 0;
    std::int64_t weather_epoch = 0;
    std::int64_t hazards_epoch = 0;
    std::int64_t taf_epoch = 0;
    std::int64_t wind_epoch = 0;
};

// Status for the route described by the same query parameters as /briefing.
// On a bad request returns nullopt and, if `error` is given, fills it with the
// response /briefing would have sent.
std::optional<RouteStatus> route_status(const Request& request, const AirportTable& airports,
                                        const WeatherSnapshot* weather = nullptr,
                                        std::int64_t now_epoch = 0,
                                        const HazardSnapshot* hazards = nullptr,
                                        Response* error = nullptr,
                                        const TafSnapshot* tafs = nullptr,
                                        const WindSnapshot* winds = nullptr);

// One plain-English line per change worth alerting on; empty if nothing changed.
std::vector<std::string> diff_route_status(const RouteStatus& before, const RouteStatus& after);

std::string route_status_json(const RouteStatus& status);

// {"error": "..."} with the given HTTP status.
Response error_json(int status, const std::string& message);

// Feed gauges and counters in Prometheus text format.
std::string weather_metrics_text(WeatherStore& store, std::int64_t now_epoch);
std::string hazard_metrics_text(HazardStore& store, std::int64_t now_epoch);
std::string taf_metrics_text(TafStore& store, std::int64_t now_epoch);
std::string wind_metrics_text(WindStore& store, std::int64_t now_epoch);

}  // namespace corridor
