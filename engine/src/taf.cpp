#include "corridor/taf.hpp"

#include <cctype>
#include <cstdlib>

#include "parse_util.hpp"

namespace corridor {

namespace {

constexpr std::size_t kFieldCount = 16;

bool valid_station(const std::string& code) {
    if (code.empty() || code.size() > 8) return false;
    for (unsigned char c : code) {
        if (!std::isalnum(c)) return false;
    }
    return true;
}

bool epoch_field(const std::string& text, std::int64_t& out) {
    double value = 0.0;
    if (!detail::to_double(text, value) || value <= 0.0) return false;
    out = static_cast<std::int64_t>(value);
    return true;
}

bool covers(const TafGroup& group, std::int64_t epoch) {
    return group.group_from <= epoch && epoch < group.group_to;
}

std::string tempo_label(const TafGroup& group) {
    if (group.change == "PROB" && group.probability) {
        return "PROB" + std::to_string(static_cast<int>(*group.probability));
    }
    return group.change.empty() ? "TEMPO" : group.change;
}

}  // namespace

int category_rank(const std::string& category) {
    if (category == "VFR") return 1;
    if (category == "MVFR") return 2;
    if (category == "IFR") return 3;
    if (category == "LIFR") return 4;
    return 0;
}

bool parse_taf_snapshot(std::istream& in, TafSnapshot& out, std::string& error) {
    using detail::to_optional;

    TafSnapshot snapshot;
    std::string line;
    if (!std::getline(in, line) || line.rfind("#corridor-taf v1", 0) != 0) {
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
            error = where + "expected 16 fields, found " + std::to_string(f.size());
            return false;
        }

        TafGroup group;
        group.station = f[0];
        if (!valid_station(group.station)) {
            error = where + "invalid station code";
            return false;
        }
        if (!epoch_field(f[1], group.issued_epoch) || !epoch_field(f[2], group.valid_from) ||
            !epoch_field(f[3], group.valid_to) || !epoch_field(f[4], group.group_from) ||
            !epoch_field(f[5], group.group_to) || group.group_to <= group.group_from) {
            error = where + "invalid forecast window";
            return false;
        }
        group.change = f[6];
        if (f[7] != "0" && f[7] != "1") {
            error = where + "prevailing must be 0 or 1";
            return false;
        }
        group.prevailing = f[7] == "1";
        group.flight_category = f[8];
        if (!to_optional(f[9], group.ceiling_ft) || !to_optional(f[10], group.visibility_sm) ||
            !to_optional(f[11], group.wind_dir_deg) || !to_optional(f[12], group.wind_speed_kt) ||
            !to_optional(f[13], group.wind_gust_kt) || !to_optional(f[14], group.probability)) {
            error = where + "invalid numeric field";
            return false;
        }
        group.raw_text = f[15];
        snapshot.stations[group.station].push_back(std::move(group));
    }

    out = std::move(snapshot);
    return true;
}

std::optional<ArrivalForecast> forecast_at(const TafSnapshot& snapshot, const std::string& station,
                                           std::int64_t eta_epoch, double eta_min) {
    const auto it = snapshot.stations.find(station);
    if (it == snapshot.stations.end()) return std::nullopt;

    const TafGroup* prevailing = nullptr;
    const TafGroup* worst_tempo = nullptr;
    for (const TafGroup& group : it->second) {
        if (!covers(group, eta_epoch)) continue;
        if (group.prevailing) {
            if (!prevailing || group.group_from >= prevailing->group_from) prevailing = &group;
        } else if (category_rank(group.flight_category) > 0 &&
                   (!worst_tempo || category_rank(group.flight_category) >
                                        category_rank(worst_tempo->flight_category))) {
            worst_tempo = &group;
        }
    }
    if (!prevailing || prevailing->flight_category.empty()) return std::nullopt;

    ArrivalForecast arrival;
    arrival.station = prevailing->station;
    arrival.eta_epoch = eta_epoch;
    arrival.eta_min = eta_min;
    arrival.flight_category = prevailing->flight_category;
    arrival.ceiling_ft = prevailing->ceiling_ft;
    arrival.visibility_sm = prevailing->visibility_sm;
    arrival.wind_dir_deg = prevailing->wind_dir_deg;
    arrival.wind_speed_kt = prevailing->wind_speed_kt;
    arrival.wind_gust_kt = prevailing->wind_gust_kt;
    arrival.change = prevailing->change;
    arrival.group_from = prevailing->group_from;
    arrival.group_to = prevailing->group_to;
    arrival.raw_text = prevailing->raw_text;
    if (worst_tempo &&
        category_rank(worst_tempo->flight_category) > category_rank(prevailing->flight_category)) {
        arrival.tempo_category = worst_tempo->flight_category;
        arrival.tempo_change = tempo_label(*worst_tempo);
    }
    // A gust on a temporary line is worth showing even when the category holds.
    if (worst_tempo && worst_tempo->wind_gust_kt) arrival.tempo_gust_kt = worst_tempo->wind_gust_kt;
    return arrival;
}

}  // namespace corridor
