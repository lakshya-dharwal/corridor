// How much of a briefing to trust, and why.
//
// The SpO2 gap is against published measurements, not against a curve we fitted.
// The wind number is the median change in the 91.211 oxygen call across a fixed
// set of routes, flown once at the typed airspeed and once with the forecast wind.
// A stale feed lowers the score. The same snapshots always produce the same score.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "corridor/wind.hpp"

namespace corridor {

struct TrustReport {
    int score = 0;  // 0..100
    double model_gap_pts = 0.0;
    std::optional<double> wind_shift_min;
    int routes = 0;
    std::string feeds;  // "fresh", "stale", or "none"
    std::string why;
};

// A stale flag applies only when that feed is on this briefing. The wind study
// still runs on a stale wind file: the minutes belong to that snapshot.
TrustReport trust_report(const WindSnapshot* winds, std::int64_t now_epoch, bool weather_on,
                         bool weather_stale, bool taf_on, bool taf_stale, bool wind_stale,
                         bool traffic_on = false, bool traffic_stale = false);

std::string trust_json(const TrustReport& report);

}  // namespace corridor
