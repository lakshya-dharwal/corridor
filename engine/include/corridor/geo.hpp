// Great-circle route geometry on a spherical Earth. Distances in nautical miles.
#pragma once

#include <vector>

namespace corridor {

struct LatLon {
    double lat_deg = 0.0;
    double lon_deg = 0.0;
};

double distance_nm(LatLon a, LatLon b);
double initial_bearing_deg(LatLon from, LatLon to);

// Point a fraction (0..1) of the way along the great circle from a to b.
LatLon intermediate_point(LatLon a, LatLon b, double fraction);

// Shortest distance from p to the great-circle segment a-b.
double distance_to_segment_nm(LatLon p, LatLon a, LatLon b);

// Closest point of p on the route. along_nm is distance from the first waypoint.
struct RouteProjection {
    double along_nm = 0.0;
    double cross_nm = 0.0;
};
RouteProjection project_onto_route(LatLon p, const std::vector<LatLon>& waypoints);

// Total length of a route through the waypoints.
double route_length_nm(const std::vector<LatLon>& waypoints);

// Position after flying dist_nm along the route. Clamped to the last waypoint.
LatLon point_along_route(const std::vector<LatLon>& waypoints, double dist_nm);

// Points every spacing_nm along the route, always including both ends.
std::vector<LatLon> sample_route(const std::vector<LatLon>& waypoints, double spacing_nm);

// Ray casting in lat/lon space. Good for hazard areas that do not span the
// antimeridian or a pole.
bool point_in_polygon(LatLon p, const std::vector<LatLon>& polygon);

// True if p is inside the polygon or within half_width_nm of its boundary.
bool point_near_polygon(LatLon p, const std::vector<LatLon>& polygon, double half_width_nm);

// True if any part of the polygon lies within half_width_nm of the route.
bool corridor_intersects_polygon(const std::vector<LatLon>& waypoints,
                                 const std::vector<LatLon>& polygon,
                                 double half_width_nm, double spacing_nm = 5.0);

}  // namespace corridor
