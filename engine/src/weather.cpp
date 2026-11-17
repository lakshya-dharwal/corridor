#include "corridor/weather.hpp"

#include <cctype>
#include <cstdlib>

#include "parse_util.hpp"

namespace corridor {

namespace {

constexpr std::size_t kFieldCount = 13;

bool valid_station(const std::string& code) {
    if (code.empty() || code.size() > 8) return false;
    for (unsigned char c : code) {
        if (!std::isalnum(c)) return false;
    }
    return true;
}

}  // namespace

bool parse_snapshot(std::istream& in, WeatherSnapshot& out, std::string& error) {
    using detail::to_double;
    using detail::to_optional;

    WeatherSnapshot snapshot;
    std::string line;
    if (!std::getline(in, line) || line.rfind("#corridor-weather v1", 0) != 0) {
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
            error = where + "expected 13 fields, found " + std::to_string(f.size());
            return false;
        }

        StationWeather w;
        w.station = f[0];
        double observed = 0.0;
        if (!valid_station(w.station)) {
            error = where + "invalid station code";
            return false;
        }
        if (!to_double(f[1], w.pos.lat_deg) || !to_double(f[2], w.pos.lon_deg) ||
            !detail::valid_position(w.pos.lat_deg, w.pos.lon_deg)) {
            error = where + "invalid position";
            return false;
        }
        if (!to_double(f[3], w.elevation_ft) || !to_double(f[4], observed) || observed <= 0.0) {
            error = where + "invalid elevation or observation time";
            return false;
        }
        w.observed_epoch = static_cast<std::int64_t>(observed);
        w.flight_category = f[5];
        if (!to_optional(f[6], w.ceiling_ft) || !to_optional(f[7], w.visibility_sm) ||
            !to_optional(f[8], w.wind_dir_deg) || !to_optional(f[9], w.wind_speed_kt) ||
            !to_optional(f[10], w.temp_c) || !to_optional(f[11], w.altimeter_inhg)) {
            error = where + "invalid numeric field";
            return false;
        }
        w.raw_text = f[12];
        snapshot.stations[w.station] = std::move(w);
    }

    out = std::move(snapshot);
    return true;
}

}  // namespace corridor
