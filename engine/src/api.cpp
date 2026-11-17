#include "corridor/api.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace corridor {

namespace {

// Thrown by the parameter helpers; carries the HTTP status to return.
struct ParamError {
    int status;
    std::string message;
};

std::optional<std::string> param(const Request& request, const std::string& name) {
    const auto it = request.query.find(name);
    if (it == request.query.end() || it->second.empty()) return std::nullopt;
    return it->second;
}

// Whole-string numeric parse with a range check. Rejects "12abc", "nan", "".
double number_param(const Request& request, const std::string& name, double fallback,
                    double low, double high) {
    const auto text = param(request, name);
    if (!text) return fallback;
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(text->c_str(), &end);
    const bool parsed = end != text->c_str() && *end == '\0' && errno == 0;
    if (!parsed || !(value >= low && value <= high)) {
        std::ostringstream message;
        message << name << " must be a number between " << low << " and " << high;
        throw ParamError{400, message.str()};
    }
    return value;
}

bool bool_param(const Request& request, const std::string& name, bool fallback) {
    const auto text = param(request, name);
    if (!text) return fallback;
    if (*text == "true" || *text == "1") return true;
    if (*text == "false" || *text == "0") return false;
    throw ParamError{400, name + " must be true or false"};
}

const StationWeather* station(const WeatherSnapshot* weather, const std::string& code) {
    if (!weather) return nullptr;
    const auto it = weather->stations.find(code);
    return it == weather->stations.end() ? nullptr : &it->second;
}

Airport airport_param(const Request& request, const std::string& name,
                      const AirportTable& airports, const WeatherSnapshot* weather) {
    const auto code = param(request, name);
    if (!code) throw ParamError{400, name + " is required"};
    if (const StationWeather* w = station(weather, *code)) {
        return Airport{w->station, w->pos, w->elevation_ft};
    }
    const auto it = airports.find(*code);
    // The code is not echoed back: it is unvalidated input going into JSON.
    if (it == airports.end()) throw ParamError{404, "unknown airport in " + name};
    return it->second;
}

std::int64_t age_seconds(const StationWeather& w, std::int64_t now_epoch) {
    return std::max<std::int64_t>(0, now_epoch - w.observed_epoch);
}

bool is_fresh(const StationWeather& w, std::int64_t now_epoch) {
    return age_seconds(w, now_epoch) <= kStaleWeatherSeconds;
}

std::int64_t snapshot_age_seconds(const HazardSnapshot& snapshot, std::int64_t now_epoch) {
    return std::max<std::int64_t>(0, now_epoch - snapshot.generated_epoch);
}

std::string optional_json(const std::optional<double>& value, int precision) {
    if (!value) return "null";
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << *value;
    return out.str();
}

std::string station_json(const StationWeather* w, std::int64_t now_epoch) {
    if (!w) return "null";
    std::ostringstream out;
    out << "{\"station\": \"" << json_escape(w->station) << "\", \"flight_category\": \""
        << json_escape(w->flight_category) << "\", \"ceiling_ft\": "
        << optional_json(w->ceiling_ft, 0) << ", \"visibility_sm\": "
        << optional_json(w->visibility_sm, 1) << ", \"wind_dir_deg\": "
        << optional_json(w->wind_dir_deg, 0) << ", \"wind_speed_kt\": "
        << optional_json(w->wind_speed_kt, 0) << ", \"temp_c\": "
        << optional_json(w->temp_c, 1) << ", \"altimeter_inhg\": "
        << optional_json(w->altimeter_inhg, 2) << ", \"age_min\": "
        << age_seconds(*w, now_epoch) / 60 << ", \"stale\": "
        << (is_fresh(*w, now_epoch) ? "false" : "true") << ", \"raw\": \""
        << json_escape(w->raw_text) << "\"}";
    return out.str();
}

// Newest observation age across all stations, or nullopt for an empty snapshot.
std::optional<std::int64_t> newest_age_seconds(const WeatherSnapshot& snapshot,
                                               std::int64_t now_epoch) {
    std::optional<std::int64_t> newest;
    for (const auto& entry : snapshot.stations) {
        const std::int64_t age = age_seconds(entry.second, now_epoch);
        if (!newest || age < *newest) newest = age;
    }
    return newest;
}

// A request turned into a flight plan and briefed against the live data. Both
// /briefing and the alert stream are views of this.
struct Evaluated {
    FlightPlan plan;
    Briefing briefing;
    int step = 5;
    const StationWeather* departure_weather = nullptr;
    const StationWeather* destination_weather = nullptr;
    std::string altimeter_source = "standard 29.92";
    bool has_hazard_feed = false;
    std::int64_t hazard_feed_age = 0;
    std::size_t active_areas = 0;
    bool has_taf = false;
    bool taf_fresh = false;
    std::int64_t taf_age = 0;
    std::optional<ArrivalForecast> arrival;
    bool has_wind = false;
    bool wind_fresh = false;
    bool wind_applied = false;
    std::int64_t wind_age = 0;
    double airspeed_kt = 0.0;
    std::optional<RouteWind> wind;
};

// Throws ParamError for a bad request and std::invalid_argument for a plan the
// engine rejects.
Evaluated evaluate(const Request& request, const AirportTable& airports,
                   const WeatherSnapshot* weather, std::int64_t now_epoch,
                   const HazardSnapshot* hazards, const TafSnapshot* tafs,
                   const WindSnapshot* winds) {
    Evaluated result;
    FlightPlan& plan = result.plan;
    plan.departure = airport_param(request, "from", airports, weather);
    plan.destination = airport_param(request, "to", airports, weather);
    plan.cruise_alt_ft = number_param(request, "alt", 8000.0, 0.0, 60000.0);
    plan.groundspeed_kt = number_param(request, "gs", 140.0, 30.0, 700.0);
    plan.departure_local_hour = number_param(request, "dep_hour", 9.0, 0.0, 24.0);
    plan.pressurized = bool_param(request, "pressurized", false);
    result.step = static_cast<int>(number_param(request, "step", 5.0, 1.0, 60.0));
    if (plan.departure.icao == plan.destination.icao) {
        throw ParamError{400, "from and to must be different airports"};
    }

    result.departure_weather = station(weather, plan.departure.icao);
    result.destination_weather = station(weather, plan.destination.icao);

    // Altimeter: an explicit request wins, then a fresh METAR, then standard.
    if (param(request, "altimeter")) {
        plan.altimeter_inhg = number_param(request, "altimeter", 29.92, 27.0, 32.0);
        result.altimeter_source = "request";
    } else {
        for (const StationWeather* w : {result.departure_weather, result.destination_weather}) {
            if (w && w->altimeter_inhg && is_fresh(*w, now_epoch)) {
                plan.altimeter_inhg = *w->altimeter_inhg;
                result.altimeter_source = "METAR " + w->station;
                break;
            }
        }
    }

    std::vector<Hazard> active;
    if (hazards) {
        for (const Hazard& hazard : hazards->areas) {
            if (hazard.valid_to_epoch >= now_epoch) active.push_back(hazard);
        }
        result.has_hazard_feed = true;
        result.hazard_feed_age = snapshot_age_seconds(*hazards, now_epoch);
        result.active_areas = active.size();
    }

    result.airspeed_kt = plan.groundspeed_kt;
    if (winds) {
        result.has_wind = true;
        result.wind_age = std::max<std::int64_t>(0, now_epoch - winds->generated_epoch);
        result.wind_fresh = winds->generated_epoch > 0 && result.wind_age <= kStaleWindSeconds;
        result.wind = route_wind(*winds, {plan.departure.pos, plan.destination.pos},
                                 plan.cruise_alt_ft, now_epoch, plan.groundspeed_kt);
        if (result.wind_fresh && result.wind) {
            plan.groundspeed_kt = result.wind->groundspeed_kt;
            result.wind_applied = true;
        }
    }

    result.briefing = build_briefing(plan, active);
    if (tafs) {
        result.has_taf = true;
        result.taf_age = std::max<std::int64_t>(0, now_epoch - tafs->generated_epoch);
        result.taf_fresh = tafs->generated_epoch > 0 && result.taf_age <= kStaleTafSeconds;
        const std::int64_t eta =
            now_epoch + std::llround(result.briefing.duration_min * 60.0);
        result.arrival = forecast_at(*tafs, plan.destination.icao, eta, result.briefing.duration_min);
    }
    return result;
}

std::string arrival_json(const Evaluated& e) {
    std::ostringstream out;
    out << std::fixed << "{\"status\": \"" << (e.taf_fresh ? "ok" : "stale") << "\", \"age_min\": "
        << e.taf_age / 60 << ", \"station\": \"" << json_escape(e.plan.destination.icao)
        << "\", \"eta_min\": " << std::setprecision(0) << e.briefing.duration_min;
    if (!e.arrival) {
        out << ", \"flight_category\": null}";
        return out.str();
    }
    const ArrivalForecast& arrival = *e.arrival;
    out << ", \"flight_category\": \"" << json_escape(arrival.flight_category)
        << "\", \"ceiling_ft\": " << optional_json(arrival.ceiling_ft, 0)
        << ", \"visibility_sm\": " << optional_json(arrival.visibility_sm, 1)
        << ", \"wind_dir_deg\": " << optional_json(arrival.wind_dir_deg, 0)
        << ", \"wind_speed_kt\": " << optional_json(arrival.wind_speed_kt, 0)
        << ", \"wind_gust_kt\": " << optional_json(arrival.wind_gust_kt, 0)
        << ", \"change\": \"" << json_escape(arrival.change) << "\", \"tempo_category\": "
        << (arrival.tempo_category.empty() ? "null" : "\"" + json_escape(arrival.tempo_category) + "\"")
        << ", \"tempo_change\": "
        << (arrival.tempo_change.empty() ? "null" : "\"" + json_escape(arrival.tempo_change) + "\"")
        << ", \"tempo_gust_kt\": " << optional_json(arrival.tempo_gust_kt, 0) << ", \"raw\": \""
        << json_escape(arrival.raw_text) << "\"}";
    return out.str();
}

std::string wind_json(const Evaluated& e) {
    const char* status = "none";
    if (!e.wind_fresh) status = "stale";
    else if (e.wind_applied) status = "ok";
    std::ostringstream out;
    out << std::fixed << "{\"status\": \"" << status << "\", \"age_min\": " << e.wind_age / 60
        << ", \"airspeed_kt\": " << std::setprecision(0) << e.airspeed_kt
        << ", \"altitude_ft\": " << e.plan.cruise_alt_ft << ", \"tailwind_kt\": ";
    if (e.wind_applied && e.wind) {
        out << std::setprecision(1) << e.wind->tailwind_kt << ", \"groundspeed_kt\": "
            << e.wind->groundspeed_kt;
    } else {
        out << "null, \"groundspeed_kt\": null";
    }
    out << "}";
    return out.str();
}

const char* hazard_feed_status(const Evaluated& e) {
    if (!e.has_hazard_feed) return "not configured";
    return e.hazard_feed_age <= kStaleHazardSeconds ? "ok" : "stale";
}

std::string describe(const RouteHazard& hazard) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(0) << hazard.label;
    if (hazard.low_ft || hazard.high_ft) {
        out << ", " << hazard.low_ft.value_or(0.0) << " to ";
        if (hazard.high_ft) {
            out << *hazard.high_ft << " ft";
        } else {
            out << "unlimited";
        }
    }
    if (hazard.enter_min && hazard.exit_min) {
        out << ", minutes " << *hazard.enter_min << " to " << *hazard.exit_min;
    }
    if (hazard.at_altitude) out << (*hazard.at_altitude ? ", at your altitude" : ", not at your altitude");
    return out.str();
}

void diff_category(const std::string& airport, const std::string& before,
                   const std::string& after, std::vector<std::string>& changes) {
    if (before == after) return;
    if (after.empty()) {
        changes.push_back(airport + " weather is no longer current");
    } else if (before.empty()) {
        changes.push_back(airport + " weather is current again: " + after);
    } else {
        changes.push_back(airport + " flight category changed from " + before + " to " + after);
    }
}

void diff_arrival(const std::string& airport, const std::string& before, const std::string& after,
                  std::vector<std::string>& changes) {
    if (before == after) return;
    if (after.empty()) {
        changes.push_back(airport + " arrival forecast is no longer current");
    } else if (before.empty()) {
        changes.push_back(airport + " arrival forecast is current again: " + after);
    } else {
        changes.push_back(airport + " at arrival changed from " + before + " to " + after);
    }
}

void diff_tempo(const std::string& airport, const std::string& before, const std::string& after,
                std::vector<std::string>& changes) {
    if (before == after) return;
    if (after.empty()) {
        changes.push_back(airport + " arrival tempo cleared");
    } else if (before.empty()) {
        changes.push_back(airport + " at arrival now includes " + after);
    } else {
        changes.push_back(airport + " arrival tempo changed from " + before + " to " + after);
    }
}

std::string wind_phrase(int knots) {
    if (knots >= 0) return std::to_string(knots) + " kt tailwind";
    return std::to_string(-knots) + " kt headwind";
}

void diff_wind(const std::optional<int>& before, const std::optional<int>& after,
               std::vector<std::string>& changes) {
    if (before == after) return;
    if (!before) {
        changes.push_back("Wind along the route is " + wind_phrase(*after));
    } else if (!after) {
        changes.push_back("Wind along the route is no longer available");
    } else if (std::abs(*after - *before) >= 8) {
        changes.push_back("Wind along the route changed from " + wind_phrase(*before) + " to " +
                          wind_phrase(*after));
    }
}

}  // namespace

Response error_json(int status, const std::string& message) {
    Response response;
    response.status = status;
    response.body = "{\"error\": \"" + json_escape(message) + "\"}\n";
    return response;
}

const AirportTable& demo_airports() {
    static const AirportTable table{
        {"KPHX", {"KPHX", {33.4343, -112.0116}, 1135.0}},
        {"KIXD", {"KIXD", {38.8309, -94.8903}, 1087.0}},
        {"KOJC", {"KOJC", {38.8476, -94.7376}, 1096.0}},
        {"KABQ", {"KABQ", {35.0402, -106.6092}, 5355.0}},
        {"KDEN", {"KDEN", {39.8617, -104.6732}, 5434.0}},
    };
    return table;
}

Response briefing_endpoint(const Request& request, const AirportTable& airports,
                           const WeatherSnapshot* weather, std::int64_t now_epoch,
                           const HazardSnapshot* hazards, const TafSnapshot* tafs,
                           const WindSnapshot* winds) {
    try {
        const Evaluated e = evaluate(request, airports, weather, now_epoch, hazards, tafs, winds);

        std::vector<std::pair<std::string, std::string>> extra;
        if (weather) {
            extra.emplace_back(
                "weather",
                "{\"altimeter_source\": \"" + json_escape(e.altimeter_source) +
                    "\", \"altimeter_inhg\": " + optional_json(e.plan.altimeter_inhg, 2) +
                    ",\n    \"departure\": " + station_json(e.departure_weather, now_epoch) +
                    ",\n    \"destination\": " + station_json(e.destination_weather, now_epoch) +
                    "}");
        }
        if (e.has_hazard_feed) {
            extra.emplace_back("hazard_feed",
                               std::string("{\"status\": \"") + hazard_feed_status(e) +
                                   "\", \"age_min\": " + std::to_string(e.hazard_feed_age / 60) +
                                   ", \"active_areas\": " + std::to_string(e.active_areas) + "}");
        }
        if (e.has_taf) extra.emplace_back("arrival", arrival_json(e));
        if (e.has_wind) extra.emplace_back("wind", wind_json(e));

        Response response;
        response.body = to_json(e.briefing, e.plan, e.step, extra);
        return response;
    } catch (const ParamError& error) {
        return error_json(error.status, error.message);
    } catch (const std::invalid_argument& error) {
        return error_json(400, error.what());
    }
}

std::optional<RouteStatus> route_status(const Request& request, const AirportTable& airports,
                                        const WeatherSnapshot* weather, std::int64_t now_epoch,
                                        const HazardSnapshot* hazards, Response* error,
                                        const TafSnapshot* tafs, const WindSnapshot* winds) {
    try {
        const Evaluated e =
            evaluate(request, airports, weather, now_epoch, hazards, tafs, winds);

        RouteStatus status;
        status.departure = e.plan.departure.icao;
        status.destination = e.plan.destination.icao;
        if (e.departure_weather && is_fresh(*e.departure_weather, now_epoch)) {
            status.departure_category = e.departure_weather->flight_category;
        }
        if (e.destination_weather && is_fresh(*e.destination_weather, now_epoch)) {
            status.destination_category = e.destination_weather->flight_category;
        }
        if (e.taf_fresh && e.arrival) {
            status.arrival_category = e.arrival->flight_category;
            if (!e.arrival->tempo_category.empty()) {
                status.arrival_tempo = e.arrival->tempo_change + " " + e.arrival->tempo_category;
            }
        }
        if (e.wind_applied && e.wind) {
            status.tailwind_kt = static_cast<int>(std::lround(e.wind->tailwind_kt));
        }
        status.crew_oxygen_required = e.briefing.oxygen.crew_oxygen_required;
        status.passenger_oxygen_required = e.briefing.oxygen.passenger_oxygen_required;
        status.min_spo2 = e.briefing.min_spo2;
        status.hazard_feed = hazard_feed_status(e);
        for (const HazardHit& hit : e.briefing.hazard_hits) {
            status.hazards.push_back({hit.hazard.id, hit.hazard.kind + " " + hit.hazard.hazard_type,
                                      hit.hazard.low_ft, hit.hazard.high_ft, hit.enter_min,
                                      hit.exit_min, hit.at_altitude});
        }
        return status;
    } catch (const ParamError& failure) {
        if (error) *error = error_json(failure.status, failure.message);
    } catch (const std::invalid_argument& failure) {
        if (error) *error = error_json(400, failure.what());
    }
    return std::nullopt;
}

std::vector<std::string> diff_route_status(const RouteStatus& before, const RouteStatus& after) {
    std::vector<std::string> changes;
    diff_category(after.departure, before.departure_category, after.departure_category, changes);
    diff_category(after.destination, before.destination_category, after.destination_category,
                  changes);
    diff_arrival(after.destination, before.arrival_category, after.arrival_category, changes);
    diff_tempo(after.destination, before.arrival_tempo, after.arrival_tempo, changes);
    diff_wind(before.tailwind_kt, after.tailwind_kt, changes);

    if (before.crew_oxygen_required != after.crew_oxygen_required) {
        changes.push_back(after.crew_oxygen_required
                              ? "Crew oxygen is now required (14 CFR 91.211)"
                              : "Crew oxygen is no longer required");
    }
    if (before.passenger_oxygen_required != after.passenger_oxygen_required) {
        changes.push_back(after.passenger_oxygen_required
                              ? "Passenger oxygen is now required (14 CFR 91.211)"
                              : "Passenger oxygen is no longer required");
    }

    auto contains = [](const std::vector<RouteHazard>& list, const std::string& id) {
        return std::any_of(list.begin(), list.end(),
                           [&](const RouteHazard& hazard) { return hazard.id == id; });
    };
    for (const RouteHazard& hazard : after.hazards) {
        if (!contains(before.hazards, hazard.id)) {
            changes.push_back("New hazard on route: " + describe(hazard));
        }
    }
    for (const RouteHazard& hazard : before.hazards) {
        if (!contains(after.hazards, hazard.id)) {
            changes.push_back("Hazard no longer on route: " + hazard.label);
        }
    }

    if (before.hazard_feed != after.hazard_feed) {
        if (after.hazard_feed == "stale") {
            changes.push_back("Hazard data is now stale");
        } else if (after.hazard_feed == "ok") {
            changes.push_back("Hazard data is current again");
        }
    }
    return changes;
}

std::string route_status_json(const RouteStatus& status) {
    auto category = [](const std::string& value) {
        return value.empty() ? std::string("null") : "\"" + json_escape(value) + "\"";
    };
    std::ostringstream out;
    out << "{\"from\": \"" << json_escape(status.departure) << "\", \"to\": \""
        << json_escape(status.destination) << "\", \"departure_category\": "
        << category(status.departure_category) << ", \"destination_category\": "
        << category(status.destination_category) << ", \"arrival_category\": "
        << category(status.arrival_category) << ", \"arrival_tempo\": "
        << category(status.arrival_tempo) << ", \"tailwind_kt\": "
        << (status.tailwind_kt ? std::to_string(*status.tailwind_kt) : "null")
        << ", \"crew_oxygen_required\": "
        << (status.crew_oxygen_required ? "true" : "false")
        << ", \"passenger_oxygen_required\": "
        << (status.passenger_oxygen_required ? "true" : "false") << ", \"min_spo2_pct\": "
        << optional_json(status.min_spo2 * 100.0, 1) << ", \"hazard_feed\": \""
        << json_escape(status.hazard_feed) << "\", \"hazards\": [";
    for (std::size_t i = 0; i < status.hazards.size(); ++i) {
        const RouteHazard& hazard = status.hazards[i];
        out << (i ? ", " : "") << "{\"id\": \"" << json_escape(hazard.id) << "\", \"label\": \""
            << json_escape(hazard.label) << "\", \"enter_min\": "
            << optional_json(hazard.enter_min, 0) << ", \"exit_min\": "
            << optional_json(hazard.exit_min, 0) << ", \"at_your_altitude\": "
            << (hazard.at_altitude ? (*hazard.at_altitude ? "true" : "false") : "null") << "}";
    }
    out << "]}";
    return out.str();
}

Response airports_endpoint(const AirportTable& airports, const WeatherSnapshot* weather) {
    AirportTable merged = airports;
    if (weather) {
        for (const auto& [code, w] : weather->stations) {
            merged[code] = Airport{code, w.pos, w.elevation_ft};
        }
    }

    std::ostringstream out;
    out << std::fixed << "{\"airports\": [";
    bool first = true;
    for (const auto& [code, airport] : merged) {
        const bool live = station(weather, code) != nullptr;
        out << (first ? "" : ", ") << "{\"icao\": \"" << json_escape(code) << "\", \"lat\": "
            << std::setprecision(4) << airport.pos.lat_deg << ", \"lon\": "
            << airport.pos.lon_deg << ", \"elevation_ft\": " << std::setprecision(0)
            << airport.elevation_ft << ", \"live_weather\": " << (live ? "true" : "false")
            << "}";
        first = false;
    }
    out << "]}\n";
    Response response;
    response.body = out.str();
    return response;
}

Response hazards_endpoint(const HazardSnapshot* hazards, std::int64_t now_epoch) {
    std::ostringstream out;
    out << std::fixed << "{\"generated_epoch\": " << (hazards ? hazards->generated_epoch : 0)
        << ", \"areas\": [";
    bool first = true;
    for (const Hazard& hazard : hazards ? hazards->areas : std::vector<Hazard>{}) {
        if (hazard.valid_to_epoch < now_epoch) continue;
        out << (first ? "\n  " : ",\n  ") << "{\"id\": \"" << json_escape(hazard.id)
            << "\", \"kind\": \"" << json_escape(hazard.kind) << "\", \"hazard\": \""
            << json_escape(hazard.hazard_type) << "\", \"low_ft\": "
            << optional_json(hazard.low_ft, 0) << ", \"high_ft\": "
            << optional_json(hazard.high_ft, 0) << ", \"valid_until_epoch\": "
            << hazard.valid_to_epoch << ", \"polygon\": [" << std::setprecision(4);
        for (std::size_t i = 0; i < hazard.polygon.size(); ++i) {
            out << (i ? "," : "") << "[" << hazard.polygon[i].lat_deg << ","
                << hazard.polygon[i].lon_deg << "]";
        }
        out << "]}";
        first = false;
    }
    out << (first ? "" : "\n") << "]}\n";
    Response response;
    response.body = out.str();
    return response;
}

Response health_endpoint(WeatherStore* weather, std::int64_t now_epoch, HazardStore* hazards,
                         TafStore* tafs, WindStore* winds) {
    bool healthy = true;

    std::ostringstream weather_json;
    if (!weather) {
        weather_json << "\"not configured\"";
    } else {
        const auto snapshot = weather->current();
        const auto newest = snapshot ? newest_age_seconds(*snapshot, now_epoch) : std::nullopt;
        healthy = healthy && newest && *newest <= kStaleWeatherSeconds;
        weather_json << "{\"stations\": " << (snapshot ? snapshot->stations.size() : 0)
                     << ", \"newest_observation_age_min\": ";
        if (newest) {
            weather_json << *newest / 60;
        } else {
            weather_json << "null";
        }
        weather_json << ", \"reloads\": " << weather->reloads() << ", \"reload_failures\": "
                     << weather->reload_failures() << ", \"last_error\": \""
                     << json_escape(weather->last_error()) << "\"}";
    }

    std::ostringstream hazard_json;
    if (!hazards) {
        hazard_json << "\"not configured\"";
    } else {
        const auto snapshot = hazards->current();
        healthy = healthy && snapshot &&
                  snapshot_age_seconds(*snapshot, now_epoch) <= kStaleHazardSeconds;
        hazard_json << "{\"areas\": " << (snapshot ? snapshot->areas.size() : 0)
                    << ", \"snapshot_age_min\": ";
        if (snapshot) {
            hazard_json << snapshot_age_seconds(*snapshot, now_epoch) / 60;
        } else {
            hazard_json << "null";
        }
        hazard_json << ", \"reloads\": " << hazards->reloads() << ", \"reload_failures\": "
                    << hazards->reload_failures() << ", \"last_error\": \""
                    << json_escape(hazards->last_error()) << "\"}";
    }

    std::ostringstream taf_json;
    if (!tafs) {
        taf_json << "\"not configured\"";
    } else {
        const auto snapshot = tafs->current();
        const bool fresh = snapshot && snapshot->generated_epoch > 0 &&
                           std::max<std::int64_t>(0, now_epoch - snapshot->generated_epoch) <=
                               kStaleTafSeconds;
        healthy = healthy && fresh;
        taf_json << "{\"stations\": " << (snapshot ? snapshot->stations.size() : 0)
                 << ", \"snapshot_age_min\": ";
        if (snapshot) {
            taf_json << std::max<std::int64_t>(0, now_epoch - snapshot->generated_epoch) / 60;
        } else {
            taf_json << "null";
        }
        taf_json << ", \"reloads\": " << tafs->reloads() << ", \"reload_failures\": "
                 << tafs->reload_failures() << ", \"last_error\": \""
                 << json_escape(tafs->last_error()) << "\"}";
    }

    std::ostringstream wind_health;
    if (!winds) {
        wind_health << "\"not configured\"";
    } else {
        const auto snapshot = winds->current();
        const bool fresh = snapshot && snapshot->generated_epoch > 0 &&
                           std::max<std::int64_t>(0, now_epoch - snapshot->generated_epoch) <=
                               kStaleWindSeconds;
        healthy = healthy && fresh;
        wind_health << "{\"reports\": " << (snapshot ? snapshot->reports.size() : 0)
                    << ", \"snapshot_age_min\": ";
        if (snapshot) {
            wind_health << std::max<std::int64_t>(0, now_epoch - snapshot->generated_epoch) / 60;
        } else {
            wind_health << "null";
        }
        wind_health << ", \"reloads\": " << winds->reloads() << ", \"reload_failures\": "
                    << winds->reload_failures() << ", \"last_error\": \""
                    << json_escape(winds->last_error()) << "\"}";
    }

    Response response;
    response.body = std::string("{\"status\": \"") + (healthy ? "ok" : "degraded") +
                    "\", \"weather\": " + weather_json.str() + ", \"hazards\": " +
                    hazard_json.str() + ", \"taf\": " + taf_json.str() + ", \"wind\": " +
                    wind_health.str() + "}\n";
    return response;
}

std::string weather_metrics_text(WeatherStore& store, std::int64_t now_epoch) {
    const auto snapshot = store.current();
    const auto newest = snapshot ? newest_age_seconds(*snapshot, now_epoch) : std::nullopt;

    std::ostringstream out;
    out << "# TYPE corridor_weather_stations gauge\n"
        << "corridor_weather_stations " << (snapshot ? snapshot->stations.size() : 0) << "\n";
    if (newest) {
        // Alert on this: a feed outage shows up as a steadily rising age.
        out << "# TYPE corridor_weather_newest_observation_age_seconds gauge\n"
            << "corridor_weather_newest_observation_age_seconds " << *newest << "\n";
    }
    out << "# TYPE corridor_weather_snapshot_reloads_total counter\n"
        << "corridor_weather_snapshot_reloads_total " << store.reloads() << "\n"
        << "# TYPE corridor_weather_snapshot_reload_failures_total counter\n"
        << "corridor_weather_snapshot_reload_failures_total " << store.reload_failures() << "\n";
    return out.str();
}

std::string hazard_metrics_text(HazardStore& store, std::int64_t now_epoch) {
    const auto snapshot = store.current();
    std::ostringstream out;
    out << "# TYPE corridor_hazard_areas gauge\n"
        << "corridor_hazard_areas " << (snapshot ? snapshot->areas.size() : 0) << "\n";
    if (snapshot) {
        out << "# TYPE corridor_hazard_snapshot_age_seconds gauge\n"
            << "corridor_hazard_snapshot_age_seconds "
            << snapshot_age_seconds(*snapshot, now_epoch) << "\n";
    }
    out << "# TYPE corridor_hazard_snapshot_reloads_total counter\n"
        << "corridor_hazard_snapshot_reloads_total " << store.reloads() << "\n"
        << "# TYPE corridor_hazard_snapshot_reload_failures_total counter\n"
        << "corridor_hazard_snapshot_reload_failures_total " << store.reload_failures() << "\n";
    return out.str();
}

std::string taf_metrics_text(TafStore& store, std::int64_t now_epoch) {
    const auto snapshot = store.current();
    std::ostringstream out;
    out << "# TYPE corridor_taf_stations gauge\n"
        << "corridor_taf_stations " << (snapshot ? snapshot->stations.size() : 0) << "\n";
    if (snapshot) {
        out << "# TYPE corridor_taf_snapshot_age_seconds gauge\n"
            << "corridor_taf_snapshot_age_seconds "
            << std::max<std::int64_t>(0, now_epoch - snapshot->generated_epoch) << "\n";
    }
    out << "# TYPE corridor_taf_snapshot_reloads_total counter\n"
        << "corridor_taf_snapshot_reloads_total " << store.reloads() << "\n"
        << "# TYPE corridor_taf_snapshot_reload_failures_total counter\n"
        << "corridor_taf_snapshot_reload_failures_total " << store.reload_failures() << "\n";
    return out.str();
}

std::string wind_metrics_text(WindStore& store, std::int64_t now_epoch) {
    const auto snapshot = store.current();
    std::ostringstream out;
    out << "# TYPE corridor_wind_reports gauge\n"
        << "corridor_wind_reports " << (snapshot ? snapshot->reports.size() : 0) << "\n";
    if (snapshot) {
        out << "# TYPE corridor_wind_snapshot_age_seconds gauge\n"
            << "corridor_wind_snapshot_age_seconds "
            << std::max<std::int64_t>(0, now_epoch - snapshot->generated_epoch) << "\n";
    }
    out << "# TYPE corridor_wind_snapshot_reloads_total counter\n"
        << "corridor_wind_snapshot_reloads_total " << store.reloads() << "\n"
        << "# TYPE corridor_wind_snapshot_reload_failures_total counter\n"
        << "corridor_wind_snapshot_reload_failures_total " << store.reload_failures() << "\n";
    return out.str();
}

}  // namespace corridor
