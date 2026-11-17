#include "corridor/hazards.hpp"

#include <cctype>
#include <cstdlib>

#include "parse_util.hpp"

namespace corridor {

namespace {

constexpr std::size_t kFieldCount = 8;

// Identifiers and type names: letters, digits, dash, underscore, dot.
bool valid_token(const std::string& text) {
    if (text.empty() || text.size() > 64) return false;
    for (unsigned char c : text) {
        if (!std::isalnum(c) && c != '-' && c != '_' && c != '.') return false;
    }
    return true;
}

bool parse_polygon(const std::string& text, std::vector<LatLon>& out) {
    for (const std::string& pair : detail::split(text, ';')) {
        const std::vector<std::string> parts = detail::split(pair, ',');
        LatLon point;
        if (parts.size() != 2 || !detail::to_double(parts[0], point.lat_deg) ||
            !detail::to_double(parts[1], point.lon_deg) ||
            !detail::valid_position(point.lat_deg, point.lon_deg)) {
            return false;
        }
        out.push_back(point);
    }
    return out.size() >= 3;
}

}  // namespace

bool parse_hazard_snapshot(std::istream& in, HazardSnapshot& out, std::string& error) {
    HazardSnapshot snapshot;
    std::string line;
    if (!std::getline(in, line) || line.rfind("#corridor-hazards v1", 0) != 0) {
        error = "missing or unsupported header";
        return false;
    }
    const std::size_t generated = line.find("generated=");
    if (generated != std::string::npos) {
        snapshot.generated_epoch = std::strtoll(line.c_str() + generated + 10, nullptr, 10);
    }

    int line_number = 1;
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

        Hazard hazard;
        hazard.id = f[0];
        hazard.kind = f[1];
        hazard.hazard_type = f[2];
        if (!valid_token(hazard.id) || !valid_token(hazard.kind) ||
            !valid_token(hazard.hazard_type)) {
            error = where + "invalid id, kind or hazard type";
            return false;
        }
        double from = 0.0, to = 0.0;
        if (!detail::to_double(f[3], from) || !detail::to_double(f[4], to) || from <= 0.0 ||
            to < from) {
            error = where + "invalid validity window";
            return false;
        }
        hazard.valid_from_epoch = static_cast<std::int64_t>(from);
        hazard.valid_to_epoch = static_cast<std::int64_t>(to);
        if (!detail::to_optional(f[5], hazard.low_ft) ||
            !detail::to_optional(f[6], hazard.high_ft) ||
            (hazard.low_ft && hazard.high_ft && *hazard.high_ft < *hazard.low_ft)) {
            error = where + "invalid altitude band";
            return false;
        }
        if (!parse_polygon(f[7], hazard.polygon)) {
            error = where + "invalid polygon";
            return false;
        }
        snapshot.areas.push_back(std::move(hazard));
    }

    out = std::move(snapshot);
    return true;
}

}  // namespace corridor
