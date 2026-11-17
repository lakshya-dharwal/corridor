#include "corridor/wind.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <map>
#include <utility>

#include "parse_util.hpp"

namespace corridor {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxRangeNm = 500.0;
constexpr std::int64_t kMaxHourSkew = 90 * 60;
constexpr double kMinGroundspeedKt = 30.0;

double rad(double deg) { return deg * kPi / 180.0; }

struct Vec {
    double u = 0.0;  // knots, eastward
    double v = 0.0;  // knots, northward
};

Vec to_vector(double from_deg, double speed_kt) {
    return {-speed_kt * std::sin(rad(from_deg)), -speed_kt * std::cos(rad(from_deg))};
}

struct Hour {
    std::int64_t epoch = 0;
    std::vector<std::pair<double, Vec>> levels;  // altitude, vector
};

struct Site {
    double lat = 0.0;
    double lon = 0.0;
    std::vector<Hour> hours;
};

std::vector<Site> sites_of(const WindSnapshot& snapshot) {
    std::map<std::pair<long long, long long>, Site> grouped;
    for (const WindReport& report : snapshot.reports) {
        const auto key = std::make_pair(std::llround(report.lat_deg * 1000.0),
                                         std::llround(report.lon_deg * 1000.0));
        Site& site = grouped[key];
        site.lat = report.lat_deg;
        site.lon = report.lon_deg;
        auto hour = std::find_if(site.hours.begin(), site.hours.end(), [&](const Hour& item) {
            return item.epoch == report.valid_epoch;
        });
        if (hour == site.hours.end()) {
            site.hours.push_back(Hour{report.valid_epoch, {}});
            hour = std::prev(site.hours.end());
        }
        hour->levels.emplace_back(report.alt_ft, to_vector(report.dir_deg, report.speed_kt));
    }
    std::vector<Site> sites;
    for (auto& [key, site] : grouped) {
        (void)key;
        for (Hour& hour : site.hours) {
            std::sort(hour.levels.begin(), hour.levels.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });
        }
        sites.push_back(std::move(site));
    }
    return sites;
}

std::optional<Vec> vector_at(const Site& site, double alt_ft, std::int64_t when) {
    const Hour* hour = nullptr;
    std::int64_t best = 0;
    for (const Hour& candidate : site.hours) {
        const std::int64_t skew = std::llabs(candidate.epoch - when);
        if (skew > kMaxHourSkew) continue;
        if (!hour || skew < best) {
            hour = &candidate;
            best = skew;
        }
    }
    if (!hour || hour->levels.empty()) return std::nullopt;

    const auto& levels = hour->levels;
    if (alt_ft <= levels.front().first) return levels.front().second;
    if (alt_ft >= levels.back().first) return levels.back().second;
    for (std::size_t i = 0; i + 1 < levels.size(); ++i) {
        if (alt_ft > levels[i + 1].first) continue;
        const double span = levels[i + 1].first - levels[i].first;
        const double t = span <= 0.0 ? 0.0 : (alt_ft - levels[i].first) / span;
        const Vec& lo = levels[i].second;
        const Vec& hi = levels[i + 1].second;
        return Vec{lo.u + t * (hi.u - lo.u), lo.v + t * (hi.v - lo.v)};
    }
    return levels.back().second;
}

std::optional<Vec> wind_at(const std::vector<Site>& sites, LatLon where, double alt_ft,
                           std::int64_t when) {
    double u = 0.0;
    double v = 0.0;
    double weight = 0.0;
    for (const Site& site : sites) {
        const auto flow = vector_at(site, alt_ft, when);
        if (!flow) continue;
        const double distance = distance_nm(where, LatLon{site.lat, site.lon});
        if (distance > kMaxRangeNm) continue;
        const double scale = std::max(distance, 10.0);
        const double w = 1.0 / (scale * scale);
        u += w * flow->u;
        v += w * flow->v;
        weight += w;
    }
    if (weight <= 0.0) return std::nullopt;
    return Vec{u / weight, v / weight};
}

double tail_of(Vec flow, double course_deg) {
    return flow.u * std::sin(rad(course_deg)) + flow.v * std::cos(rad(course_deg));
}

// Distance-weighted mean tailwind, using groundspeed to decide which forecast
// hour each sample is flown in. Empty when less than half the route has a wind.
std::optional<double> mean_tailwind(const std::vector<Site>& sites,
                                    const std::vector<LatLon>& waypoints, double length_nm,
                                    double cruise_alt_ft, std::int64_t now_epoch, double gs_kt) {
    constexpr double kStepNm = 50.0;
    std::vector<double> marks;
    for (double dist = 0.0; dist < length_nm; dist += kStepNm) marks.push_back(dist);
    marks.push_back(length_nm);

    double covered = 0.0;
    double weighted = 0.0;
    for (std::size_t i = 0; i + 1 < marks.size(); ++i) {
        const double segment = marks[i + 1] - marks[i];
        const double mid = (marks[i] + marks[i + 1]) / 2.0;
        const LatLon here = point_along_route(waypoints, mid);
        const LatLon ahead = point_along_route(waypoints, std::min(length_nm, mid + 10.0));
        const std::int64_t when =
            now_epoch + static_cast<std::int64_t>(std::llround(mid / gs_kt * 3600.0));
        const auto flow = wind_at(sites, here, cruise_alt_ft, when);
        if (!flow) continue;
        const double course = initial_bearing_deg(here, ahead);
        covered += segment;
        weighted += tail_of(*flow, course) * segment;
    }
    if (covered < 0.5 * length_nm || covered <= 0.0) return std::nullopt;
    return weighted / covered;
}

}  // namespace

double along_track_kt(double wind_from_deg, double wind_speed_kt, double course_deg) {
    return tail_of(to_vector(wind_from_deg, wind_speed_kt), course_deg);
}

bool parse_wind_snapshot(std::istream& in, WindSnapshot& out, std::string& error) {
    WindSnapshot snapshot;
    std::string line;
    if (!std::getline(in, line) || line.rfind("#corridor-wind v1", 0) != 0) {
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
        if (f.size() != 6) {
            error = where + "expected 6 fields, found " + std::to_string(f.size());
            return false;
        }
        WindReport report;
        double epoch = 0.0;
        if (!detail::to_double(f[0], report.lat_deg) || !detail::to_double(f[1], report.lon_deg) ||
            !detail::valid_position(report.lat_deg, report.lon_deg) ||
            !detail::to_double(f[2], epoch) || epoch <= 0.0 ||
            !detail::to_double(f[3], report.alt_ft) || report.alt_ft <= 0.0 ||
            !detail::to_double(f[4], report.dir_deg) ||
            !detail::to_double(f[5], report.speed_kt) || report.speed_kt < 0.0) {
            error = where + "invalid wind report";
            return false;
        }
        report.valid_epoch = static_cast<std::int64_t>(epoch);
        snapshot.reports.push_back(report);
    }
    out = std::move(snapshot);
    return true;
}

std::optional<RouteWind> route_wind(const WindSnapshot& snapshot,
                                    const std::vector<LatLon>& waypoints, double cruise_alt_ft,
                                    std::int64_t now_epoch, double airspeed_kt) {
    if (airspeed_kt <= 0.0 || waypoints.size() < 2) return std::nullopt;
    const double length = route_length_nm(waypoints);
    if (length <= 0.0) return std::nullopt;
    const std::vector<Site> sites = sites_of(snapshot);
    if (sites.empty()) return std::nullopt;

    const auto first = mean_tailwind(sites, waypoints, length, cruise_alt_ft, now_epoch, airspeed_kt);
    if (!first) return std::nullopt;
    const double guess = std::max(kMinGroundspeedKt, airspeed_kt + *first);
    const auto second = mean_tailwind(sites, waypoints, length, cruise_alt_ft, now_epoch, guess);
    const double tail = second.value_or(*first);
    const double ground = std::max(kMinGroundspeedKt, airspeed_kt + tail);

    RouteWind wind;
    wind.airspeed_kt = airspeed_kt;
    wind.groundspeed_kt = ground;
    wind.tailwind_kt = ground - airspeed_kt;
    return wind;
}

}  // namespace corridor
