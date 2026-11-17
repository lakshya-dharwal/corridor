#include "corridor/briefing.hpp"

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace corridor {

namespace {

std::vector<LatLon> waypoints_of(const FlightPlan& plan) {
    std::vector<LatLon> points;
    points.push_back(plan.departure.pos);
    points.insert(points.end(), plan.via.begin(), plan.via.end());
    points.push_back(plan.destination.pos);
    return points;
}

}  // namespace

std::string json_escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (static_cast<unsigned char>(c) < 0x20) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
            out += buf;
        } else {
            out += c;
        }
    }
    return out;
}

Briefing build_briefing(const FlightPlan& plan, const std::vector<Hazard>& hazards,
                        double corridor_half_width_nm) {
    if (plan.groundspeed_kt <= 0.0 || plan.climb_fpm <= 0.0 || plan.descent_fpm <= 0.0) {
        throw std::invalid_argument("groundspeed, climb and descent rates must be positive");
    }
    if (plan.cruise_alt_ft < std::max(plan.departure.elevation_ft, plan.destination.elevation_ft)) {
        throw std::invalid_argument("cruise altitude is below an airport elevation");
    }

    const std::vector<LatLon> waypoints = waypoints_of(plan);

    Briefing briefing;
    briefing.distance_nm = route_length_nm(waypoints);
    briefing.duration_min = briefing.distance_nm / plan.groundspeed_kt * 60.0;
    const double total = briefing.duration_min;

    // Altitude is the lowest of: cruise, the climb line from departure, and the
    // descent line into the destination. Short flights never reach cruise.
    auto altitude_at = [&](double minute) {
        const double climb = plan.departure.elevation_ft + plan.climb_fpm * minute;
        const double descent = plan.destination.elevation_ft + plan.descent_fpm * (total - minute);
        return std::min({plan.cruise_alt_ft, climb, descent});
    };

    auto add_point = [&](double minute) {
        ProfilePoint point;
        point.minute = minute;
        point.distance_nm = plan.groundspeed_kt * minute / 60.0;
        point.pos = point_along_route(waypoints, point.distance_nm);
        point.altitude_ft = altitude_at(minute);
        point.cabin_pressure_alt_ft = pressure_altitude_ft(
            cabin_altitude_ft(point.altitude_ft, plan.pressurized), plan.altimeter_inhg);
        point.spo2 = estimated_spo2(point.cabin_pressure_alt_ft);
        point.risk = classify_spo2(point.spo2);
        briefing.profile.push_back(point);
    };

    for (double minute = 0.0; minute < total; minute += 1.0) add_point(minute);
    add_point(total);

    std::vector<AltitudeSample> samples;
    samples.reserve(briefing.profile.size());
    double spo2_sum = 0.0;
    for (std::size_t i = 0; i < briefing.profile.size(); ++i) {
        const ProfilePoint& point = briefing.profile[i];
        samples.push_back({point.minute, point.cabin_pressure_alt_ft});
        briefing.min_spo2 = std::min(briefing.min_spo2, point.spo2);
        spo2_sum += point.spo2;
        if (i + 1 < briefing.profile.size() && point.spo2 < 0.90) {
            briefing.minutes_below_90 += briefing.profile[i + 1].minute - point.minute;
        }
    }
    briefing.mean_spo2 = spo2_sum / static_cast<double>(briefing.profile.size());
    briefing.oxygen = check_far_91_211(samples);
    briefing.fatigue =
        fatigue_score(total / 60.0, plan.departure_local_hour, briefing.mean_spo2);

    for (const Hazard& hazard : hazards) {
        if (!corridor_intersects_polygon(waypoints, hazard.polygon, corridor_half_width_nm)) {
            continue;
        }
        HazardHit hit;
        hit.hazard = hazard;
        const bool has_band = hazard.low_ft || hazard.high_ft;
        bool in_band = false;
        for (const ProfilePoint& point : briefing.profile) {
            if (!point_near_polygon(point.pos, hazard.polygon, corridor_half_width_nm)) continue;
            if (!hit.enter_min) hit.enter_min = point.minute;
            hit.exit_min = point.minute;
            if (point.altitude_ft >= hazard.low_ft.value_or(0.0) &&
                point.altitude_ft <= hazard.high_ft.value_or(1e9)) {
                in_band = true;
            }
        }
        if (has_band && hit.enter_min) hit.at_altitude = in_band;
        briefing.hazards_on_route.push_back(hazard.kind + " " + hazard.id);
        briefing.hazard_hits.push_back(std::move(hit));
    }
    return briefing;
}

std::string to_json(const Briefing& b, const FlightPlan& plan, int profile_step_min,
                    const std::vector<std::pair<std::string, std::string>>& extra_sections) {
    std::ostringstream out;
    out << std::fixed;
    auto pct = [](double fraction) { return fraction * 100.0; };

    out << "{\n";
    out << "  \"route\": {\"from\": \"" << json_escape(plan.departure.icao) << "\", \"to\": \""
        << json_escape(plan.destination.icao) << "\", " << std::setprecision(1)
        << "\"distance_nm\": " << b.distance_nm << ", \"duration_min\": " << b.duration_min
        << ", \"cruise_alt_ft\": " << std::setprecision(0) << plan.cruise_alt_ft
        << ", \"pressurized\": " << (plan.pressurized ? "true" : "false") << "},\n";

    for (const auto& [key, raw_json] : extra_sections) {
        out << "  \"" << json_escape(key) << "\": " << raw_json << ",\n";
    }

    out << std::setprecision(1);
    out << "  \"physiology\": {\"min_spo2_pct\": " << pct(b.min_spo2)
        << ", \"mean_spo2_pct\": " << pct(b.mean_spo2)
        << ", \"minutes_below_90_pct\": " << b.minutes_below_90
        << ", \"fatigue_score\": " << b.fatigue << "},\n";

    out << "  \"oxygen_rules\": {\"crew_oxygen_required\": "
        << (b.oxygen.crew_oxygen_required ? "true" : "false")
        << ", \"passenger_oxygen_required\": "
        << (b.oxygen.passenger_oxygen_required ? "true" : "false") << ", \"findings\": [";
    for (std::size_t i = 0; i < b.oxygen.findings.size(); ++i) {
        out << (i ? ", " : "") << '"' << json_escape(b.oxygen.findings[i]) << '"';
    }
    out << "]},\n";

    auto number_or_null = [&out](const std::optional<double>& value) {
        if (value) {
            out << std::setprecision(0) << *value;
        } else {
            out << "null";
        }
    };
    out << "  \"hazards_on_route\": [";
    for (std::size_t i = 0; i < b.hazard_hits.size(); ++i) {
        const HazardHit& hit = b.hazard_hits[i];
        out << (i ? ",\n    " : "\n    ") << "{\"id\": \"" << json_escape(hit.hazard.id)
            << "\", \"kind\": \"" << json_escape(hit.hazard.kind) << "\", \"hazard\": \""
            << json_escape(hit.hazard.hazard_type) << "\", \"low_ft\": ";
        number_or_null(hit.hazard.low_ft);
        out << ", \"high_ft\": ";
        number_or_null(hit.hazard.high_ft);
        out << ", \"valid_until_epoch\": " << hit.hazard.valid_to_epoch << ", \"enter_min\": ";
        number_or_null(hit.enter_min);
        out << ", \"exit_min\": ";
        number_or_null(hit.exit_min);
        out << ", \"at_your_altitude\": "
            << (hit.at_altitude ? (*hit.at_altitude ? "true" : "false") : "null") << "}";
    }
    out << (b.hazard_hits.empty() ? "" : "\n  ") << "],\n";

    out << "  \"profile\": [\n";
    const std::size_t step = static_cast<std::size_t>(std::max(1, profile_step_min));
    bool first = true;
    for (std::size_t i = 0; i < b.profile.size(); ++i) {
        const bool last = (i + 1 == b.profile.size());
        if (i % step != 0 && !last) continue;
        const ProfilePoint& p = b.profile[i];
        out << (first ? "" : ",\n") << "    {\"minute\": " << std::setprecision(0) << p.minute
            << ", \"lat\": " << std::setprecision(4) << p.pos.lat_deg << ", \"lon\": "
            << p.pos.lon_deg << ", \"altitude_ft\": " << std::setprecision(0) << p.altitude_ft
            << ", \"spo2_pct\": " << std::setprecision(1) << pct(p.spo2) << ", \"risk\": \""
            << to_string(p.risk) << "\"}";
        first = false;
    }
    out << "\n  ],\n";
    out << "  \"disclaimer\": \"Educational estimate only. Not medical advice and not for "
           "operational flight planning.\"\n";
    out << "}\n";
    return out.str();
}

}  // namespace corridor
