// Turns a flight plan into a briefing: altitude profile, oxygen curve, FAA oxygen
// findings, fatigue score, and the hazard areas the route corridor touches.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "corridor/geo.hpp"
#include "corridor/physiology.hpp"

namespace corridor {

struct Airport {
    std::string icao;
    LatLon pos;
    double elevation_ft = 0.0;
};

struct FlightPlan {
    Airport departure;
    Airport destination;
    std::vector<LatLon> via;            // optional waypoints between the two airports
    double cruise_alt_ft = 8000.0;
    double groundspeed_kt = 140.0;
    double climb_fpm = 700.0;
    double descent_fpm = 500.0;
    bool pressurized = false;
    double altimeter_inhg = 29.92;
    double departure_local_hour = 9.0;  // 0..24
};

struct ProfilePoint {
    double minute = 0.0;
    double distance_nm = 0.0;
    LatLon pos;
    double altitude_ft = 0.0;
    double cabin_pressure_alt_ft = 0.0;
    double spo2 = 0.0;
    HypoxiaRisk risk = HypoxiaRisk::None;
};

struct Hazard {
    std::string id;
    std::string kind;  // e.g. "SIGMET", "G-AIRMET"
    std::vector<LatLon> polygon;
    std::string hazard_type;  // e.g. "TURB-LO", "ICE", "IFR"
    std::int64_t valid_from_epoch = 0;
    std::int64_t valid_to_epoch = 0;
    std::optional<double> low_ft;   // altitude band; empty when the feed gives none
    std::optional<double> high_ft;
};

// A hazard area the route corridor passes through.
struct HazardHit {
    Hazard hazard;
    std::optional<double> enter_min;  // first and last minute of the flight in the area
    std::optional<double> exit_min;
    // Whether the aircraft is inside the hazard's altitude band while in the
    // area. Empty when the hazard has no altitude band.
    std::optional<bool> at_altitude;
};

struct Briefing {
    double distance_nm = 0.0;
    double duration_min = 0.0;
    std::vector<ProfilePoint> profile;  // one point per minute
    double min_spo2 = 1.0;
    double mean_spo2 = 1.0;
    double minutes_below_90 = 0.0;
    OxygenRuleReport oxygen;
    double fatigue = 0.0;
    std::vector<std::string> hazards_on_route;  // "kind id" labels, same order as hazard_hits
    std::vector<HazardHit> hazard_hits;
};

// Throws std::invalid_argument on an unusable plan.
Briefing build_briefing(const FlightPlan& plan, const std::vector<Hazard>& hazards = {},
                        double corridor_half_width_nm = 25.0);

// Escapes a string for use inside a JSON string literal.
std::string json_escape(const std::string& text);

// profile_step_min thins the profile in the output (1 = every minute).
// extra_sections are (key, already-serialized JSON value) pairs added to the
// top-level object, e.g. live weather supplied by the service layer.
std::string to_json(const Briefing& briefing, const FlightPlan& plan, int profile_step_min = 5,
                    const std::vector<std::pair<std::string, std::string>>& extra_sections = {});

}  // namespace corridor
