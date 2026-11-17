#include "corridor/saved_routes.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <utility>

namespace corridor {

namespace {

constexpr char kField = '\x1f';
constexpr char kRecord = '\x1e';

std::string upper_copy(std::string value) {
    for (char& c : value) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return value;
}

const std::string* query_value(const Request& request, const char* name) {
    const auto it = request.query.find(name);
    if (it == request.query.end() || it->second.empty()) return nullptr;
    return &it->second;
}

std::string scrub(std::string value) {
    for (char& c : value) {
        if (c == '\t' || c == '\n' || c == '\r' || c == kField || c == kRecord) c = ' ';
    }
    return value;
}

std::string optional_field(const std::optional<double>& value) {
    if (!value) return {};
    std::ostringstream out;
    out << std::fixed << *value;
    return out.str();
}

std::optional<double> optional_number(const std::string& text) {
    if (text.empty()) return std::nullopt;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0') return std::nullopt;
    return value;
}

std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> parts;
    std::string current;
    for (const char c : text) {
        if (c == separator) {
            parts.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(std::move(current));
    return parts;
}

std::string encode_hazards(const std::vector<RouteHazard>& hazards) {
    std::ostringstream out;
    for (std::size_t i = 0; i < hazards.size(); ++i) {
        const RouteHazard& hazard = hazards[i];
        if (i) out << kRecord;
        out << scrub(hazard.id) << kField << scrub(hazard.label) << kField
            << optional_field(hazard.enter_min) << kField << optional_field(hazard.exit_min)
            << kField
            << (hazard.at_altitude ? (*hazard.at_altitude ? "1" : "0") : "");
    }
    return out.str();
}

std::string encode_traffic(const std::vector<RouteTraffic>& aircraft) {
    std::ostringstream out;
    for (std::size_t i = 0; i < aircraft.size(); ++i) {
        const RouteTraffic& one = aircraft[i];
        if (i) out << kRecord;
        out << scrub(one.icao) << kField << scrub(one.callsign) << kField
            << optional_field(one.alt_ft) << kField << optional_field(one.along_nm) << kField
            << (one.near_planned_altitude ? "1" : "0");
    }
    return out.str();
}

std::vector<RouteTraffic> decode_traffic(const std::string& text) {
    std::vector<RouteTraffic> aircraft;
    if (text.empty()) return aircraft;
    for (const std::string& record : split(text, kRecord)) {
        const std::vector<std::string> fields = split(record, kField);
        if (fields.size() != 5 || fields[0].empty()) continue;
        RouteTraffic one;
        one.icao = fields[0];
        one.callsign = fields[1];
        one.alt_ft = optional_number(fields[2]);
        one.along_nm = optional_number(fields[3]).value_or(0.0);
        one.near_planned_altitude = fields[4] == "1";
        aircraft.push_back(std::move(one));
    }
    return aircraft;
}

std::vector<RouteHazard> decode_hazards(const std::string& text) {
    std::vector<RouteHazard> hazards;
    if (text.empty()) return hazards;
    for (const std::string& record : split(text, kRecord)) {
        const std::vector<std::string> fields = split(record, kField);
        if (fields.size() != 5 || fields[0].empty()) continue;
        RouteHazard hazard;
        hazard.id = fields[0];
        hazard.label = fields[1];
        hazard.enter_min = optional_number(fields[2]);
        hazard.exit_min = optional_number(fields[3]);
        if (fields[4] == "1") hazard.at_altitude = true;
        else if (fields[4] == "0") hazard.at_altitude = false;
        hazards.push_back(std::move(hazard));
    }
    return hazards;
}

std::string encode_route(const std::string& key, const RouteStatus& status) {
    std::ostringstream out;
    out << std::fixed << key << '\t' << scrub(status.departure) << '\t' << scrub(status.destination)
        << '\t' << scrub(status.departure_category) << '\t' << scrub(status.destination_category)
        << '\t' << (status.crew_oxygen_required ? '1' : '0') << '\t'
        << (status.passenger_oxygen_required ? '1' : '0') << '\t' << status.min_spo2 << '\t'
        << scrub(status.hazard_feed) << '\t' << encode_hazards(status.hazards) << '\t'
        << scrub(status.arrival_category) << '\t' << scrub(status.arrival_tempo) << '\t';
    if (status.tailwind_kt) out << *status.tailwind_kt;
    out << '\t' << scrub(status.traffic_feed) << '\t' << encode_traffic(status.traffic) << '\n';
    return out.str();
}

std::optional<std::pair<std::string, RouteStatus>> decode_route(const std::string& line) {
    const std::vector<std::string> fields = split(line, '\t');
    if ((fields.size() != 10 && fields.size() != 12 && fields.size() != 13 && fields.size() != 15) ||
        fields[0].empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const double spo2 = std::strtod(fields[7].c_str(), &end);
    if (end == fields[7].c_str() || *end != '\0') return std::nullopt;

    RouteStatus status;
    status.departure = fields[1];
    status.destination = fields[2];
    status.departure_category = fields[3];
    status.destination_category = fields[4];
    status.crew_oxygen_required = fields[5] == "1";
    status.passenger_oxygen_required = fields[6] == "1";
    status.min_spo2 = spo2;
    status.hazard_feed = fields[8];
    status.hazards = decode_hazards(fields[9]);
    if (fields.size() >= 12) {
        status.arrival_category = fields[10];
        status.arrival_tempo = fields[11];
    }
    if (fields.size() >= 13 && !fields[12].empty()) {
        char* tail = nullptr;
        const long knots = std::strtol(fields[12].c_str(), &tail, 10);
        if (tail != fields[12].c_str() && *tail == '\0') status.tailwind_kt = static_cast<int>(knots);
    }
    if (fields.size() == 15) {
        status.traffic_feed = fields[13];
        status.traffic = decode_traffic(fields[14]);
    }
    return std::make_pair(fields[0], std::move(status));
}

}  // namespace

std::string route_key(const Request& request) {
    const std::string* from = query_value(request, "from");
    const std::string* to = query_value(request, "to");
    const std::string* alt = query_value(request, "alt");
    const std::string* gs = query_value(request, "gs");
    const std::string* pressurized = query_value(request, "pressurized");
    const bool cabin = pressurized && (*pressurized == "true" || *pressurized == "1");
    return upper_copy(from ? *from : "") + "|" + upper_copy(to ? *to : "") + "|" +
           (alt ? *alt : "8000") + "|" + (gs ? *gs : "140") + "|" + (cabin ? "1" : "0");
}

SavedRoutes::SavedRoutes(std::string path) : path_(std::move(path)) { load(); }

std::optional<RouteStatus> SavedRoutes::find(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = routes_.find(key);
    if (it == routes_.end()) return std::nullopt;
    return it->second;
}

void SavedRoutes::remember(const std::string& key, const RouteStatus& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    routes_[key] = status;
    save();
}

std::size_t SavedRoutes::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return routes_.size();
}

void SavedRoutes::load() {
    if (path_.empty()) return;
    std::ifstream in(path_);
    if (!in) return;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto decoded = decode_route(line);
        if (!decoded) continue;
        routes_.emplace(decoded->first, decoded->second);
    }
}

void SavedRoutes::save() const {
    if (path_.empty()) return;
    const std::string temporary = path_ + ".tmp";
    std::ofstream out(temporary, std::ios::trunc);
    if (!out) {
        if (!warned_) {
            std::fprintf(stderr, "saved routes: cannot write %s\n", temporary.c_str());
            warned_ = true;
        }
        return;
    }
    for (const auto& [key, status] : routes_) out << encode_route(key, status);
    out.close();
    if (!out || std::rename(temporary.c_str(), path_.c_str()) != 0) {
        if (!warned_) {
            std::fprintf(stderr, "saved routes: cannot replace %s\n", path_.c_str());
            warned_ = true;
        }
        ::remove(temporary.c_str());
    }
}

}  // namespace corridor
