#include "corridor/traffic.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

#include "parse_util.hpp"

namespace corridor {

namespace {

constexpr std::size_t kFieldCount = 8;
constexpr double kLatNm = 60.0;

bool valid_icao(const std::string& text) {
    if (text.size() != 6) return false;
    for (unsigned char c : text) {
        if (!std::isxdigit(c) || std::isupper(c)) return false;
    }
    return true;
}

bool valid_callsign(const std::string& text) {
    if (text.size() > 8) return false;
    for (unsigned char c : text) {
        if (!std::isalnum(c) || std::islower(c)) return false;
    }
    return true;
}

bool parse_coverage(const std::string& header, TrafficSnapshot& snapshot, std::string& error) {
    const std::size_t at = header.find("coverage=");
    if (at == std::string::npos) return true;
    std::string token = header.substr(at + 9);
    const std::size_t end = token.find_first_of(" \t\r");
    if (end != std::string::npos) token.resize(end);
    const std::vector<std::string> parts = detail::split(token, ',');
    double south = 0.0, west = 0.0, north = 0.0, east = 0.0;
    if (parts.size() != 4 || !detail::to_double(parts[0], south) ||
        !detail::to_double(parts[1], west) || !detail::to_double(parts[2], north) ||
        !detail::to_double(parts[3], east) || !detail::valid_position(south, west) ||
        !detail::valid_position(north, east) || !(south < north) || !(west < east)) {
        error = "invalid coverage rectangle";
        return false;
    }
    snapshot.has_coverage = true;
    snapshot.coverage = {south, west, north, east};
    return true;
}

bool inside_inset(LatLon p, const TrafficCoverage& box, double inset_nm) {
    if (p.lat_deg < box.south || p.lat_deg > box.north || p.lon_deg < box.west ||
        p.lon_deg > box.east) {
        return false;
    }
    const double lon_scale = kLatNm * std::max(0.2, std::cos(p.lat_deg * 3.14159265358979323846 / 180.0));
    const double south_nm = (p.lat_deg - box.south) * kLatNm;
    const double north_nm = (box.north - p.lat_deg) * kLatNm;
    const double west_nm = (p.lon_deg - box.west) * lon_scale;
    const double east_nm = (box.east - p.lon_deg) * lon_scale;
    return std::min({south_nm, north_nm, west_nm, east_nm}) >= inset_nm;
}

double altitude_at(const std::vector<ProfilePoint>& profile, double along_nm) {
    if (profile.empty()) return 0.0;
    const ProfilePoint* best = &profile.front();
    double best_gap = std::fabs(best->distance_nm - along_nm);
    for (const ProfilePoint& point : profile) {
        const double gap = std::fabs(point.distance_nm - along_nm);
        if (gap < best_gap) {
            best = &point;
            best_gap = gap;
        }
    }
    return best->altitude_ft;
}

}  // namespace

bool parse_traffic_snapshot(std::istream& in, TrafficSnapshot& out, std::string& error) {
    TrafficSnapshot snapshot;
    std::string line;
    if (!std::getline(in, line) || line.rfind("#corridor-traffic v1", 0) != 0) {
        error = "missing or unsupported header";
        return false;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::size_t generated = line.find("generated=");
    if (generated != std::string::npos) {
        snapshot.generated_epoch = std::strtoll(line.c_str() + generated + 10, nullptr, 10);
    }
    if (snapshot.generated_epoch <= 0) {
        error = "missing generated time";
        return false;
    }
    if (!parse_coverage(line, snapshot, error)) return false;

    int line_number = 1;
    std::vector<std::string> seen;
    while (std::getline(in, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const std::string where = "line " + std::to_string(line_number) + ": ";
        const std::vector<std::string> f = detail::split(line, '\t');
        if (f.size() != kFieldCount) {
            error = where + "expected 8 fields, found " + std::to_string(f.size());
            return false;
        }

        TrafficAircraft aircraft;
        aircraft.icao = f[0];
        aircraft.callsign = f[1];
        if (!valid_icao(aircraft.icao) || !valid_callsign(aircraft.callsign)) {
            error = where + "invalid icao or callsign";
            return false;
        }
        if (std::find(seen.begin(), seen.end(), aircraft.icao) != seen.end()) {
            error = where + "duplicate icao";
            return false;
        }
        if (!detail::to_double(f[2], aircraft.pos.lat_deg) ||
            !detail::to_double(f[3], aircraft.pos.lon_deg) ||
            !detail::valid_position(aircraft.pos.lat_deg, aircraft.pos.lon_deg)) {
            error = where + "invalid position";
            return false;
        }
        if (!detail::to_optional(f[4], aircraft.alt_ft) ||
            (aircraft.alt_ft && (*aircraft.alt_ft < -2000.0 || *aircraft.alt_ft > 70000.0))) {
            error = where + "invalid altitude";
            return false;
        }
        if (!detail::to_optional(f[5], aircraft.groundspeed_kt) ||
            (aircraft.groundspeed_kt &&
             (*aircraft.groundspeed_kt < 0.0 || *aircraft.groundspeed_kt > 800.0))) {
            error = where + "invalid groundspeed";
            return false;
        }
        if (!detail::to_optional(f[6], aircraft.track_deg) ||
            (aircraft.track_deg && (*aircraft.track_deg < 0.0 || *aircraft.track_deg > 360.0))) {
            error = where + "invalid track";
            return false;
        }
        double seen_epoch = 0.0;
        if (!detail::to_double(f[7], seen_epoch) || seen_epoch <= 0.0) {
            error = where + "invalid seen time";
            return false;
        }
        aircraft.seen_epoch = static_cast<std::int64_t>(seen_epoch);
        if (aircraft.seen_epoch > snapshot.generated_epoch + 120) {
            error = where + "position time is after the snapshot";
            return false;
        }
        seen.push_back(aircraft.icao);
        snapshot.aircraft.push_back(std::move(aircraft));
    }

    out = std::move(snapshot);
    return true;
}

bool route_inside_coverage(const TrafficSnapshot& snapshot, const std::vector<LatLon>& waypoints,
                           double half_width_nm) {
    if (!snapshot.has_coverage || waypoints.size() < 2) return false;
    for (const LatLon& point : sample_route(waypoints, half_width_nm)) {
        if (!inside_inset(point, snapshot.coverage, half_width_nm)) return false;
    }
    return true;
}

std::vector<TrafficHit> traffic_on_route(const TrafficSnapshot& snapshot,
                                         const std::vector<LatLon>& waypoints,
                                         const std::vector<ProfilePoint>& profile,
                                         double half_width_nm) {
    std::vector<TrafficHit> hits;
    if (waypoints.size() < 2) return hits;
    for (const TrafficAircraft& aircraft : snapshot.aircraft) {
        if (snapshot.generated_epoch - aircraft.seen_epoch > kTrafficPositionSeconds) continue;
        const RouteProjection projection = project_onto_route(aircraft.pos, waypoints);
        if (projection.cross_nm > half_width_nm) continue;
        TrafficHit hit;
        hit.aircraft = aircraft;
        hit.along_nm = projection.along_nm;
        hit.cross_nm = projection.cross_nm;
        if (aircraft.alt_ft) {
            const double planned = altitude_at(profile, projection.along_nm);
            hit.near_planned_altitude = std::fabs(*aircraft.alt_ft - planned) <= kTrafficAltitudeBandFt;
        }
        hits.push_back(std::move(hit));
    }
    std::sort(hits.begin(), hits.end(), [](const TrafficHit& a, const TrafficHit& b) {
        if (a.along_nm != b.along_nm) return a.along_nm < b.along_nm;
        return a.aircraft.icao < b.aircraft.icao;
    });
    return hits;
}

}  // namespace corridor
