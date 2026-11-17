#include "corridor/geo.hpp"

#include <algorithm>
#include <cmath>

namespace corridor {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusNm = 3440.065;

double rad(double deg) { return deg * kPi / 180.0; }
double deg(double radians) { return radians * 180.0 / kPi; }

double angular_distance(LatLon a, LatLon b) {
    const double p1 = rad(a.lat_deg), p2 = rad(b.lat_deg);
    const double dp = p2 - p1;
    const double dl = rad(b.lon_deg - a.lon_deg);
    const double h = std::sin(dp / 2) * std::sin(dp / 2) +
                     std::cos(p1) * std::cos(p2) * std::sin(dl / 2) * std::sin(dl / 2);
    return 2.0 * std::asin(std::min(1.0, std::sqrt(h)));
}

double bearing_rad(LatLon from, LatLon to) {
    const double p1 = rad(from.lat_deg), p2 = rad(to.lat_deg);
    const double dl = rad(to.lon_deg - from.lon_deg);
    const double y = std::sin(dl) * std::cos(p2);
    const double x = std::cos(p1) * std::sin(p2) - std::sin(p1) * std::cos(p2) * std::cos(dl);
    return std::atan2(y, x);
}

}  // namespace

double distance_nm(LatLon a, LatLon b) { return angular_distance(a, b) * kEarthRadiusNm; }

double initial_bearing_deg(LatLon from, LatLon to) {
    return std::fmod(deg(bearing_rad(from, to)) + 360.0, 360.0);
}

LatLon intermediate_point(LatLon a, LatLon b, double fraction) {
    const double d = angular_distance(a, b);
    if (d < 1e-12) return a;
    const double p1 = rad(a.lat_deg), l1 = rad(a.lon_deg);
    const double p2 = rad(b.lat_deg), l2 = rad(b.lon_deg);
    const double wa = std::sin((1.0 - fraction) * d) / std::sin(d);
    const double wb = std::sin(fraction * d) / std::sin(d);
    const double x = wa * std::cos(p1) * std::cos(l1) + wb * std::cos(p2) * std::cos(l2);
    const double y = wa * std::cos(p1) * std::sin(l1) + wb * std::cos(p2) * std::sin(l2);
    const double z = wa * std::sin(p1) + wb * std::sin(p2);
    return {deg(std::atan2(z, std::sqrt(x * x + y * y))), deg(std::atan2(y, x))};
}

double distance_to_segment_nm(LatLon p, LatLon a, LatLon b) {
    const double d12 = angular_distance(a, b);
    const double d13 = angular_distance(a, p);
    if (d12 < 1e-12 || d13 < 1e-12) return d13 * kEarthRadiusNm;

    const double delta = bearing_rad(a, p) - bearing_rad(a, b);
    if (std::cos(delta) < 0.0) return d13 * kEarthRadiusNm;  // p is behind a

    const double cross = std::asin(std::clamp(std::sin(d13) * std::sin(delta), -1.0, 1.0));
    const double along =
        std::acos(std::clamp(std::cos(d13) / std::cos(cross), -1.0, 1.0));
    if (along > d12) return distance_nm(p, b);  // p is past b
    return std::fabs(cross) * kEarthRadiusNm;
}

double route_length_nm(const std::vector<LatLon>& waypoints) {
    double total = 0.0;
    for (std::size_t i = 0; i + 1 < waypoints.size(); ++i) {
        total += distance_nm(waypoints[i], waypoints[i + 1]);
    }
    return total;
}

LatLon point_along_route(const std::vector<LatLon>& waypoints, double dist_nm) {
    if (waypoints.empty()) return {};
    for (std::size_t i = 0; i + 1 < waypoints.size(); ++i) {
        const double leg = distance_nm(waypoints[i], waypoints[i + 1]);
        const bool last_leg = (i + 2 == waypoints.size());
        if (dist_nm <= leg || last_leg) {
            const double f = leg > 0.0 ? std::clamp(dist_nm / leg, 0.0, 1.0) : 0.0;
            return intermediate_point(waypoints[i], waypoints[i + 1], f);
        }
        dist_nm -= leg;
    }
    return waypoints.back();
}

std::vector<LatLon> sample_route(const std::vector<LatLon>& waypoints, double spacing_nm) {
    std::vector<LatLon> points;
    if (waypoints.empty()) return points;
    const double total = route_length_nm(waypoints);
    if (spacing_nm <= 0.0 || total <= 0.0) {
        points.push_back(waypoints.front());
        points.push_back(waypoints.back());
        return points;
    }
    for (double d = 0.0; d < total; d += spacing_nm) {
        points.push_back(point_along_route(waypoints, d));
    }
    points.push_back(waypoints.back());
    return points;
}

bool point_in_polygon(LatLon p, const std::vector<LatLon>& polygon) {
    bool inside = false;
    const std::size_t n = polygon.size();
    if (n < 3) return false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const LatLon& a = polygon[i];
        const LatLon& b = polygon[j];
        const bool straddles = (a.lat_deg > p.lat_deg) != (b.lat_deg > p.lat_deg);
        if (straddles) {
            const double x = a.lon_deg + (p.lat_deg - a.lat_deg) / (b.lat_deg - a.lat_deg) *
                                             (b.lon_deg - a.lon_deg);
            if (p.lon_deg < x) inside = !inside;
        }
    }
    return inside;
}

bool corridor_intersects_polygon(const std::vector<LatLon>& waypoints,
                                 const std::vector<LatLon>& polygon,
                                 double half_width_nm, double spacing_nm) {
    if (waypoints.size() < 2 || polygon.size() < 3) return false;

    // Polygon corner inside the corridor.
    for (const LatLon& vertex : polygon) {
        for (std::size_t i = 0; i + 1 < waypoints.size(); ++i) {
            if (distance_to_segment_nm(vertex, waypoints[i], waypoints[i + 1]) <= half_width_nm) {
                return true;
            }
        }
    }

    // Route passes through the polygon, or close to one of its edges.
    for (const LatLon& p : sample_route(waypoints, spacing_nm)) {
        if (point_near_polygon(p, polygon, half_width_nm)) return true;
    }
    return false;
}

bool point_near_polygon(LatLon p, const std::vector<LatLon>& polygon, double half_width_nm) {
    const std::size_t n = polygon.size();
    if (n < 3) return false;

    // Cheap reject first: most points are nowhere near most hazard areas, and the
    // exact test below costs a dozen trig calls per polygon edge.
    double min_lat = 90.0, max_lat = -90.0, min_lon = 180.0, max_lon = -180.0;
    for (const LatLon& v : polygon) {
        min_lat = std::min(min_lat, v.lat_deg);
        max_lat = std::max(max_lat, v.lat_deg);
        min_lon = std::min(min_lon, v.lon_deg);
        max_lon = std::max(max_lon, v.lon_deg);
    }
    const double lat_margin = 1.05 * half_width_nm / 60.0;  // 1 degree of latitude = 60 nm
    const double widest_lat =
        std::min(89.0, std::max(std::fabs(min_lat), std::fabs(max_lat)) + lat_margin);
    const double lon_margin = lat_margin / std::cos(rad(widest_lat));
    if (p.lat_deg < min_lat - lat_margin || p.lat_deg > max_lat + lat_margin ||
        p.lon_deg < min_lon - lon_margin || p.lon_deg > max_lon + lon_margin) {
        return false;
    }

    if (point_in_polygon(p, polygon)) return true;
    for (std::size_t i = 0; i < n; ++i) {
        if (distance_to_segment_nm(p, polygon[i], polygon[(i + 1) % n]) <= half_width_nm) {
            return true;
        }
    }
    return false;
}

}  // namespace corridor
