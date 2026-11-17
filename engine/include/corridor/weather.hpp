// Live weather for the service. Ingestion workers publish a snapshot file
// (written atomically); the service hot-reloads it through a SnapshotStore.
//
// Snapshot format: a header line "#corridor-weather v1 generated=<epoch>", then
// one tab-separated row per station with 13 fields (empty field = unknown):
//   station lat lon elevation_ft observed_epoch flight_category ceiling_ft
//   visibility_sm wind_dir_deg wind_speed_kt temp_c altimeter_inhg raw_text
#pragma once

#include <cstdint>
#include <istream>
#include <map>
#include <optional>
#include <string>
#include <utility>

#include "corridor/geo.hpp"
#include "corridor/snapshot_store.hpp"

namespace corridor {

struct StationWeather {
    std::string station;
    LatLon pos;
    double elevation_ft = 0.0;
    std::int64_t observed_epoch = 0;
    std::string flight_category;
    std::optional<double> ceiling_ft;
    std::optional<double> visibility_sm;
    std::optional<double> wind_dir_deg;
    std::optional<double> wind_speed_kt;
    std::optional<double> temp_c;
    std::optional<double> altimeter_inhg;
    std::string raw_text;
};

struct WeatherSnapshot {
    std::int64_t generated_epoch = 0;
    std::map<std::string, StationWeather> stations;
};

// False with a message in `error` if anything is malformed. A bad row rejects
// the whole snapshot so partial data is never served.
bool parse_snapshot(std::istream& in, WeatherSnapshot& out, std::string& error);

class WeatherStore : public SnapshotStore<WeatherSnapshot> {
public:
    explicit WeatherStore(std::string path)
        : SnapshotStore<WeatherSnapshot>(std::move(path), &parse_snapshot) {}
};

}  // namespace corridor
