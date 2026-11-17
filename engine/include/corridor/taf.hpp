// Destination forecast at arrival. A TAF is a stack of time windows; the
// briefing uses the prevailing window that covers the ETA, and mentions a
// TEMPO or PROB group only when it is worse than that window.
//
// Snapshot format: a header line "#corridor-taf v1 generated=<epoch>", then one
// tab-separated row per forecast group with 16 fields (empty field = unknown):
//   station issued_epoch valid_from valid_to group_from group_to change
//   prevailing flight_category ceiling_ft visibility_sm wind_dir_deg
//   wind_speed_kt wind_gust_kt probability raw_text
// change is empty (the base line), FM, BECMG, TEMPO, or PROB.
// prevailing is 1 for a line that replaces the forecast and 0 for a temporary one.
#pragma once

#include <cstdint>
#include <istream>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "corridor/snapshot_store.hpp"

namespace corridor {

// TAFs are republished a few times an hour. Past this age the feed is stale:
// the last forecast is still shown, but it no longer drives alerts.
inline constexpr std::int64_t kStaleTafSeconds = 30 * 60;

struct TafGroup {
    std::string station;
    std::int64_t issued_epoch = 0;
    std::int64_t valid_from = 0;
    std::int64_t valid_to = 0;
    std::int64_t group_from = 0;
    std::int64_t group_to = 0;
    std::string change;
    bool prevailing = false;
    std::string flight_category;
    std::optional<double> ceiling_ft;
    std::optional<double> visibility_sm;
    std::optional<double> wind_dir_deg;
    std::optional<double> wind_speed_kt;
    std::optional<double> wind_gust_kt;
    std::optional<double> probability;
    std::string raw_text;
};

struct TafSnapshot {
    std::int64_t generated_epoch = 0;
    // Groups stay in file order, which is the order the forecast was written.
    std::map<std::string, std::vector<TafGroup>> stations;
};

// The forecast that applies at one moment. tempo_category is set only when a
// temporary group covering that moment is a worse flight category.
struct ArrivalForecast {
    std::string station;
    std::int64_t eta_epoch = 0;
    double eta_min = 0;
    std::string flight_category;
    std::optional<double> ceiling_ft;
    std::optional<double> visibility_sm;
    std::optional<double> wind_dir_deg;
    std::optional<double> wind_speed_kt;
    std::optional<double> wind_gust_kt;
    std::string change;
    std::int64_t group_from = 0;
    std::int64_t group_to = 0;
    std::string tempo_category;
    std::string tempo_change;  // "TEMPO" or "PROB30"
    std::optional<double> tempo_gust_kt;
    std::string raw_text;
};

bool parse_taf_snapshot(std::istream& in, TafSnapshot& out, std::string& error);

// Higher means worse weather. Unknown categories rank below VFR.
int category_rank(const std::string& category);

// The prevailing group covering eta_epoch, or nullopt when none does.
std::optional<ArrivalForecast> forecast_at(const TafSnapshot& snapshot, const std::string& station,
                                           std::int64_t eta_epoch, double eta_min);

class TafStore : public SnapshotStore<TafSnapshot> {
public:
    explicit TafStore(std::string path)
        : SnapshotStore<TafSnapshot>(std::move(path), &parse_taf_snapshot) {}
};

}  // namespace corridor
