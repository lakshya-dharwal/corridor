// Live hazard areas (SIGMETs and G-AIRMETs) for the service.
//
// Snapshot format: a header line "#corridor-hazards v1 generated=<epoch>", then
// one tab-separated row per area with 8 fields (empty altitude = unknown):
//   id kind hazard_type valid_from_epoch valid_to_epoch low_ft high_ft polygon
// where polygon is "lat,lon;lat,lon;..." with at least three points.
#pragma once

#include <cstdint>
#include <istream>
#include <string>
#include <utility>
#include <vector>

#include "corridor/briefing.hpp"
#include "corridor/snapshot_store.hpp"

namespace corridor {

struct HazardSnapshot {
    std::int64_t generated_epoch = 0;
    std::vector<Hazard> areas;
};

// False with a message in `error` if anything is malformed; a bad row rejects
// the whole snapshot. An empty list of areas is valid (quiet weather).
bool parse_hazard_snapshot(std::istream& in, HazardSnapshot& out, std::string& error);

class HazardStore : public SnapshotStore<HazardSnapshot> {
public:
    explicit HazardStore(std::string path)
        : SnapshotStore<HazardSnapshot>(std::move(path), &parse_hazard_snapshot) {}
};

}  // namespace corridor
