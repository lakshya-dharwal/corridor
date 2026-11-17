#include "corridor/trust.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

#include "corridor/briefing.hpp"
#include "corridor/physiology.hpp"

namespace corridor {

namespace {

// Cottrell et al., Aviat Space Environ Med 1995;66:126-130.
// Preflight mean SpO2 of 38 pilots: 97.0%.
constexpr double kCottrellGroundPct = 97.0;

// Muhm et al., N Engl J Med 2007;357:18-27.
// 502 unacclimatized adults in a chamber. Mean SpO2 fell 4.4 points from
// 650 ft to 8,000 ft (95% CI 3.9 to 4.9).
constexpr double kMuhmFromFt = 650.0;
constexpr double kMuhmToFt = 8000.0;
constexpr double kMuhmDropPts = 4.4;

constexpr double kCruiseFt = 13500.0;
constexpr double kAirspeedKt = 140.0;

struct Field {
    const char* icao;
    double lat;
    double lon;
    double elev;
};

// Twenty city pairs inside the wind grid. Elevations are the field elevations
// the live feed publishes, so the climb is the real one.
constexpr Field kFields[] = {
    {"KPHX", 33.4278, -112.0037, 1109.0}, {"KIXD", 38.8221, -94.8902, 1056.0},
    {"KDEN", 39.8466, -104.6562, 5433.0}, {"KDFW", 32.8974, -97.0220, 551.0},
    {"KLAS", 36.0719, -115.1634, 2172.0}, {"KLAX", 33.9382, -118.3866, 98.0},
    {"KORD", 41.9602, -87.9316, 663.0},   {"KSFO", 37.6196, -122.3656, 7.0},
    {"KSEA", 47.4447, -122.3144, 377.0},  {"KSLC", 40.7707, -111.9650, 4219.0},
    {"KABQ", 35.0419, -106.6154, 5315.0}, {"KATL", 33.6297, -84.4422, 1014.0},
    {"KMSP", 44.8852, -93.2313, 837.0},
};

constexpr int kPairs[][2] = {
    {0, 1},  {0, 2},  {0, 3},  {0, 4},  {5, 2},  {5, 3},  {5, 6},  {7, 2},  {7, 0},  {8, 2},
    {8, 9},  {2, 6}, {2, 3},  {2, 11}, {9, 6},  {9, 3},  {10, 6}, {10, 11}, {12, 0}, {11, 2},
};

double pct(double fraction) { return fraction * 100.0; }

double model_gap_pts() {
    const double ground = pct(estimated_spo2(0.0));
    const double drop = pct(estimated_spo2(kMuhmFromFt)) - pct(estimated_spo2(kMuhmToFt));
    const double ground_gap = std::fabs(ground - kCottrellGroundPct);
    const double drop_gap = std::fabs(drop - kMuhmDropPts);
    return (ground_gap + drop_gap) / 2.0;
}

double oxygen_call_min(const OxygenRuleReport& report) {
    return std::max(0.0, report.minutes_12500_to_14000 - 30.0) + report.minutes_above_14000;
}

FlightPlan plan_between(const Field& from, const Field& to, double groundspeed_kt) {
    FlightPlan plan;
    plan.departure = Airport{from.icao, {from.lat, from.lon}, from.elev};
    plan.destination = Airport{to.icao, {to.lat, to.lon}, to.elev};
    plan.cruise_alt_ft = kCruiseFt;
    plan.groundspeed_kt = groundspeed_kt;
    plan.pressurized = false;
    plan.altimeter_inhg = 29.92;
    return plan;
}

double median_of(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    const std::size_t n = values.size();
    if (n % 2 == 1) return values[n / 2];
    return 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

struct WindStudy {
    std::optional<double> median_min;
    int routes = 0;
};

WindStudy wind_study(const WindSnapshot& winds, std::int64_t now_epoch) {
    std::vector<double> shifts;
    for (const auto& pair : kPairs) {
        const Field& from = kFields[pair[0]];
        const Field& to = kFields[pair[1]];
        const FlightPlan calm = plan_between(from, to, kAirspeedKt);
        const auto flow = route_wind(winds, {calm.departure.pos, calm.destination.pos}, kCruiseFt,
                                     now_epoch, kAirspeedKt);
        if (!flow) continue;
        const FlightPlan blown = plan_between(from, to, flow->groundspeed_kt);
        const double quiet = oxygen_call_min(build_briefing(calm).oxygen);
        const double moved = oxygen_call_min(build_briefing(blown).oxygen);
        shifts.push_back(std::fabs(moved - quiet));
    }
    WindStudy study;
    study.routes = static_cast<int>(shifts.size());
    if (!shifts.empty()) study.median_min = median_of(std::move(shifts));
    return study;
}

std::string one_decimal(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << value;
    return out.str();
}

}  // namespace

TrustReport trust_report(const WindSnapshot* winds, std::int64_t now_epoch, bool weather_on,
                         bool weather_stale, bool taf_on, bool taf_stale, bool wind_stale,
                         bool traffic_on, bool traffic_stale) {
    TrustReport report;
    report.model_gap_pts = model_gap_pts();
    const bool configured = weather_on || taf_on || winds != nullptr || traffic_on;
    const bool stale = weather_stale || taf_stale || wind_stale || traffic_stale;
    if (!configured) report.feeds = "none";
    else if (stale) report.feeds = "stale";
    else report.feeds = "fresh";

    if (winds) {
        const WindStudy study = wind_study(*winds, now_epoch);
        report.routes = study.routes;
        report.wind_shift_min = study.median_min;
    }

    double score = 100.0 - report.model_gap_pts;
    if (stale) score -= 30.0;
    if (!report.wind_shift_min) score -= 20.0;
    else score -= std::min(40.0, *report.wind_shift_min);
    report.score = static_cast<int>(std::lround(std::clamp(score, 0.0, 100.0)));

    std::ostringstream why;
    why << "SpO2 is " << one_decimal(report.model_gap_pts)
        << " pts off published studies (Cottrell 1995, Muhm 2007). ";
    if (report.wind_shift_min) {
        why << "Wind moves the oxygen call by " << one_decimal(*report.wind_shift_min)
            << " min across " << report.routes << " routes. ";
    } else {
        why << "No wind benchmark on this briefing. ";
    }
    if (report.feeds == "stale") why << "A feed is stale.";
    else if (report.feeds == "fresh") why << "Feeds are fresh.";
    else why << "No live feed on this briefing.";
    report.why = why.str();
    return report;
}

std::string trust_json(const TrustReport& report) {
    std::ostringstream out;
    out << std::fixed << "{\"score\": " << report.score << ", \"model_gap_pts\": "
        << std::setprecision(1) << report.model_gap_pts << ", \"wind_shift_min\": ";
    if (report.wind_shift_min) out << std::setprecision(1) << *report.wind_shift_min;
    else out << "null";
    out << ", \"routes\": " << report.routes << ", \"feeds\": \"" << report.feeds << "\", \"why\": \""
        << json_escape(report.why) << "\"}";
    return out.str();
}

}  // namespace corridor
