// Live ADS-B traffic for the service.
//
// A snapshot is where equipped aircraft were at one clock reading. It is not a
// prediction of who will still be there when the flight arrives, and it does
// not include aircraft that are not broadcasting.
//
// Snapshot format: a header
//   #corridor-traffic v1 generated=<epoch> coverage=<south>,<west>,<north>,<east>
// then one tab-separated row per aircraft with 8 fields (empty = unknown):
//   icao callsign lat lon alt_ft gs_kt track_deg seen_epoch
// icao is 6 lowercase hex characters. coverage is the rectangle the worker
// actually searched, in degrees. A route that leaves that rectangle, shrunk by
// the corridor width, is not treated as an empty sky.
#pragma once

#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <vector>

#include "corridor/briefing.hpp"
#include "corridor/snapshot_store.hpp"

namespace corridor {

// The worker republishes every few minutes. Past this age the rows are still
// shown and flagged, and they stop driving alerts.
inline constexpr std::int64_t kStaleTrafficSeconds = 8 * 60;

// A row whose position is already this old at publish time is dropped. The
// worker itself only writes positions younger than a minute.
inline constexpr std::int64_t kTrafficPositionSeconds = 90;

// "Near the altitude this plan uses" at the point on the route closest to the
// aircraft. A snapshot fact, not a conflict prediction.
inline constexpr double kTrafficAltitudeBandFt = 3000.0;

// Same half-width the hazard corridor uses.
inline constexpr double kTrafficCorridorNm = 25.0;

// Rectangle the feed searched. South/north are latitudes, west/east longitudes.
struct TrafficCoverage {
    double south = 0.0;
    double west = 0.0;
    double north = 0.0;
    double east = 0.0;
};

struct TrafficAircraft {
    std::string icao;
    std::string callsign;  // empty when the aircraft did not broadcast one
    LatLon pos;
    std::optional<double> alt_ft;
    std::optional<double> groundspeed_kt;
    std::optional<double> track_deg;
    std::int64_t seen_epoch = 0;
};

struct TrafficSnapshot {
    std::int64_t generated_epoch = 0;
    bool has_coverage = false;
    TrafficCoverage coverage;
    std::vector<TrafficAircraft> aircraft;
};

// An aircraft inside the corridor at the snapshot clock.
struct TrafficHit {
    TrafficAircraft aircraft;
    double along_nm = 0.0;
    double cross_nm = 0.0;
    bool near_planned_altitude = false;
};

// False with a message in `error` if anything is malformed; a bad row rejects
// the whole snapshot. An empty aircraft list is valid.
bool parse_traffic_snapshot(std::istream& in, TrafficSnapshot& out, std::string& error);

// True when every sample of the route sits inside the searched rectangle by at
// least half_width_nm, so an aircraft in the corridor could not have been
// outside the fetch. False when the snapshot names no rectangle.
bool route_inside_coverage(const TrafficSnapshot& snapshot, const std::vector<LatLon>& waypoints,
                           double half_width_nm = kTrafficCorridorNm);

// Airborne positions from the snapshot that lie within half_width_nm of the
// route, ordered along the route. Positions already stale when the snapshot
// was published are omitted.
std::vector<TrafficHit> traffic_on_route(const TrafficSnapshot& snapshot,
                                         const std::vector<LatLon>& waypoints,
                                         const std::vector<ProfilePoint>& profile,
                                         double half_width_nm = kTrafficCorridorNm);

class TrafficStore : public SnapshotStore<TrafficSnapshot> {
public:
    explicit TrafficStore(std::string path)
        : SnapshotStore<TrafficSnapshot>(std::move(path), &parse_traffic_snapshot) {}
};

}  // namespace corridor
