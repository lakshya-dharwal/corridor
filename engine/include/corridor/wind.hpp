// Cruise wind along a route. The number a pilot types is airspeed. This turns
// the forecast wind at cruise altitude into one along-track component and a
// groundspeed, so the time en route, and the forecast that covers the landing,
// move with the wind.
//
// The component is an average over the route, not a new wind at every minute.
// Climb and descent stay on that same groundspeed.
//
// Snapshot format: a header line "#corridor-wind v1 generated=<epoch>", then one
// tab-separated row per forecast point with 6 fields:
//   lat lon valid_epoch alt_ft dir_deg speed_kt
// dir_deg is the direction the wind is from. Rows for one place and time at
// several altitudes let the reader interpolate to cruise.
#pragma once

#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "corridor/geo.hpp"
#include "corridor/snapshot_store.hpp"

namespace corridor {

// The worker republishes every few minutes. Past this age the last wind is
// still described, but the typed airspeed is what the briefing flies.
inline constexpr std::int64_t kStaleWindSeconds = 30 * 60;

struct WindReport {
    double lat_deg = 0.0;
    double lon_deg = 0.0;
    std::int64_t valid_epoch = 0;
    double alt_ft = 0.0;
    double dir_deg = 0.0;
    double speed_kt = 0.0;
};

struct WindSnapshot {
    std::int64_t generated_epoch = 0;
    std::vector<WindReport> reports;
};

// Positive is a tailwind, negative a headwind.
struct RouteWind {
    double airspeed_kt = 0.0;
    double tailwind_kt = 0.0;
    double groundspeed_kt = 0.0;
};

bool parse_wind_snapshot(std::istream& in, WindSnapshot& out, std::string& error);

// Along-track component of one wind. wind_from_deg is where it blows from.
double along_track_kt(double wind_from_deg, double wind_speed_kt, double course_deg);

// Average along-track wind at cruise_alt_ft. Empty when the snapshot does not
// cover at least half the route. now_epoch places each sample in the forecast
// hour it will actually be flown.
std::optional<RouteWind> route_wind(const WindSnapshot& snapshot,
                                    const std::vector<LatLon>& waypoints, double cruise_alt_ft,
                                    std::int64_t now_epoch, double airspeed_kt);

class WindStore : public SnapshotStore<WindSnapshot> {
public:
    explicit WindStore(std::string path)
        : SnapshotStore<WindSnapshot>(std::move(path), &parse_wind_snapshot) {}
};

}  // namespace corridor
