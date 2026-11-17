// Dependency-free test runner so the engine can be verified with nothing but a
// C++20 compiler. Swap for GoogleTest once CI is set up.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/time.h>

#include <atomic>
#include <mutex>

#include "corridor/alerts.hpp"
#include "corridor/api.hpp"
#include "corridor/briefing.hpp"
#include "corridor/websocket.hpp"
#include "corridor/geo.hpp"
#include "corridor/http.hpp"
#include "corridor/physiology.hpp"

using namespace corridor;

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(actual, expected, tol) CHECK(std::fabs((actual) - (expected)) <= (tol))

static const Airport kPhx{"KPHX", {33.4343, -112.0116}, 1135.0};
static const Airport kIxd{"KIXD", {38.8309, -94.8903}, 1087.0};
static const Airport kOjc{"KOJC", {38.8476, -94.7376}, 1096.0};

static void test_atmosphere() {
    CHECK_NEAR(isa_pressure_mmhg(0.0), 760.0, 0.5);
    CHECK_NEAR(isa_pressure_mmhg(10000.0), 522.7, 1.5);
    CHECK_NEAR(isa_pressure_mmhg(18000.0), 379.5, 2.0);  // about half an atmosphere
    CHECK(isa_pressure_mmhg(40000.0) < isa_pressure_mmhg(36000.0));

    CHECK_NEAR(pressure_altitude_ft(5000.0, 29.92), 5000.0, 1e-6);
    CHECK_NEAR(pressure_altitude_ft(5000.0, 28.92), 6000.0, 1e-6);

    CHECK_NEAR(cabin_altitude_ft(35000.0, true), 8000.0, 1e-6);
    CHECK_NEAR(cabin_altitude_ft(35000.0, false), 35000.0, 1e-6);
    CHECK_NEAR(cabin_altitude_ft(5000.0, true), 5000.0, 1e-6);
}

static void test_oxygen() {
    // Published ranges for healthy, unacclimatized adults breathing ambient air.
    const double sea = estimated_spo2(0.0);
    const double ten = estimated_spo2(10000.0);
    const double eighteen = estimated_spo2(18000.0);
    CHECK(sea >= 0.96 && sea <= 0.99);
    CHECK(ten >= 0.85 && ten <= 0.91);
    CHECK(eighteen >= 0.65 && eighteen <= 0.76);

    double previous = 2.0;
    for (double alt = 0.0; alt <= 25000.0; alt += 500.0) {
        const double s = estimated_spo2(alt);
        CHECK(s < previous);
        CHECK(s >= 0.0 && s <= 1.0);
        previous = s;
    }

    CHECK(spo2_from_po2(0.0) == 0.0);
    CHECK(alveolar_po2_mmhg(60000.0) == 0.0);  // clamped, never negative
    CHECK_NEAR(alveolar_pco2_mmhg(15000.0), 30.0, 1e-9);

    CHECK(classify_spo2(0.97) == HypoxiaRisk::None);
    CHECK(classify_spo2(0.91) == HypoxiaRisk::Mild);
    CHECK(classify_spo2(0.85) == HypoxiaRisk::Moderate);
    CHECK(classify_spo2(0.70) == HypoxiaRisk::Severe);
}

static std::vector<AltitudeSample> level_flight(double minutes, double alt_ft) {
    std::vector<AltitudeSample> samples;
    for (double m = 0.0; m <= minutes; m += 1.0) samples.push_back({m, alt_ft});
    return samples;
}

static void test_oxygen_rules() {
    const auto long_13k = check_far_91_211(level_flight(45.0, 13000.0));
    CHECK(long_13k.crew_oxygen_required);
    CHECK(!long_13k.passenger_oxygen_required);
    CHECK_NEAR(long_13k.minutes_12500_to_14000, 45.0, 1e-9);
    CHECK(long_13k.findings.size() == 1);

    const auto short_13k = check_far_91_211(level_flight(20.0, 13000.0));
    CHECK(!short_13k.crew_oxygen_required);
    CHECK(short_13k.findings.empty());

    const auto brief_14500 = check_far_91_211(level_flight(5.0, 14500.0));
    CHECK(brief_14500.crew_oxygen_required);
    CHECK(!brief_14500.passenger_oxygen_required);

    const auto at_16k = check_far_91_211(level_flight(10.0, 16000.0));
    CHECK(at_16k.crew_oxygen_required);
    CHECK(at_16k.passenger_oxygen_required);

    const auto low = check_far_91_211(level_flight(300.0, 9500.0));
    CHECK(!low.crew_oxygen_required && !low.passenger_oxygen_required);

    CHECK(check_far_91_211({}).findings.empty());
}

static void test_fatigue() {
    CHECK(fatigue_score(0.0, 9.0, 0.97) == 0.0);
    CHECK(fatigue_score(6.0, 9.0, 0.97) > fatigue_score(2.0, 9.0, 0.97));
    CHECK(fatigue_score(3.0, 2.0, 0.97) > fatigue_score(3.0, 10.0, 0.97));  // circadian low
    CHECK(fatigue_score(3.0, 10.0, 0.85) > fatigue_score(3.0, 10.0, 0.97)); // hypoxia
    CHECK(fatigue_score(40.0, 2.0, 0.60) <= 100.0);
}

static void test_geo() {
    CHECK_NEAR(distance_nm({0.0, 0.0}, {0.0, 1.0}), 60.04, 0.05);
    const double phx_ixd = distance_nm(kPhx.pos, kIxd.pos);
    CHECK(phx_ixd > 880.0 && phx_ixd < 930.0);
    CHECK_NEAR(initial_bearing_deg({0.0, 0.0}, {0.0, 1.0}), 90.0, 1e-6);
    CHECK_NEAR(initial_bearing_deg({0.0, 0.0}, {1.0, 0.0}), 0.0, 1e-6);

    const LatLon mid = intermediate_point(kPhx.pos, kIxd.pos, 0.5);
    CHECK_NEAR(distance_nm(kPhx.pos, mid), distance_nm(mid, kIxd.pos), 0.01);
    CHECK_NEAR(distance_nm(kPhx.pos, mid), phx_ixd / 2.0, 0.01);

    const LatLon a{0.0, 0.0}, b{0.0, 10.0};
    CHECK_NEAR(distance_to_segment_nm({1.0, 5.0}, a, b), 60.04, 0.1);   // beside the leg
    CHECK_NEAR(distance_to_segment_nm({0.0, -1.0}, a, b), 60.04, 0.1);  // behind the start
    CHECK_NEAR(distance_to_segment_nm({0.0, 11.0}, a, b), 60.04, 0.1);  // past the end
    CHECK_NEAR(distance_to_segment_nm({0.0, 5.0}, a, b), 0.0, 1e-6);    // on the leg

    const std::vector<LatLon> route{{0.0, 0.0}, {0.0, 1.0}, {1.0, 1.0}};
    CHECK_NEAR(route_length_nm(route), 120.08, 0.1);
    const LatLon corner = point_along_route(route, 60.04);
    CHECK_NEAR(corner.lat_deg, 0.0, 0.01);
    CHECK_NEAR(corner.lon_deg, 1.0, 0.01);
    const LatLon beyond = point_along_route(route, 9999.0);
    CHECK_NEAR(beyond.lat_deg, 1.0, 1e-6);
    CHECK(sample_route(route, 10.0).size() == 14);  // 13 steps plus the end point

    const std::vector<LatLon> square{{0.0, 0.0}, {0.0, 2.0}, {2.0, 2.0}, {2.0, 0.0}};
    CHECK(point_in_polygon({1.0, 1.0}, square));
    CHECK(!point_in_polygon({3.0, 1.0}, square));
    CHECK(!point_in_polygon({1.0, -0.5}, square));

    // Route along the equator; hazard box starts 12 nm north of it.
    const std::vector<LatLon> equator{{0.0, -5.0}, {0.0, 5.0}};
    const std::vector<LatLon> north_box{{0.2, -1.0}, {0.2, 1.0}, {1.0, 1.0}, {1.0, -1.0}};
    CHECK(corridor_intersects_polygon(equator, north_box, 25.0));
    CHECK(!corridor_intersects_polygon(equator, north_box, 5.0));

    // Route flies straight through a large box whose corners are all far away.
    const std::vector<LatLon> big_box{{-3.0, -2.0}, {-3.0, 2.0}, {3.0, 2.0}, {3.0, -2.0}};
    CHECK(corridor_intersects_polygon(equator, big_box, 1.0));
}

static void test_briefing() {
    FlightPlan plan;
    plan.departure = kPhx;
    plan.destination = kIxd;
    plan.cruise_alt_ft = 13500.0;
    plan.groundspeed_kt = 140.0;

    const Briefing b = build_briefing(plan);
    CHECK_NEAR(b.duration_min, b.distance_nm / 140.0 * 60.0, 1e-6);
    CHECK_NEAR(b.profile.front().altitude_ft, kPhx.elevation_ft, 1e-6);
    CHECK_NEAR(b.profile.back().altitude_ft, kIxd.elevation_ft, 1e-6);
    CHECK_NEAR(b.profile.back().minute, b.duration_min, 1e-9);

    double peak = 0.0;
    for (const ProfilePoint& p : b.profile) peak = std::fmax(peak, p.altitude_ft);
    CHECK_NEAR(peak, 13500.0, 1e-6);

    CHECK(b.oxygen.crew_oxygen_required);         // hours at 13,500 ft
    CHECK(!b.oxygen.passenger_oxygen_required);
    CHECK(b.min_spo2 < 0.85);
    CHECK(b.minutes_below_90 > 60.0);

    // Same flight, pressurized: cabin never exceeds 8,000 ft.
    FlightPlan pressurized = plan;
    pressurized.pressurized = true;
    const Briefing pb = build_briefing(pressurized);
    CHECK(!pb.oxygen.crew_oxygen_required);
    CHECK(pb.min_spo2 > b.min_spo2);
    CHECK(pb.fatigue < b.fatigue);

    // Low altimeter setting raises pressure altitude and lowers SpO2.
    FlightPlan low_pressure = plan;
    low_pressure.altimeter_inhg = 29.42;
    CHECK(build_briefing(low_pressure).min_spo2 < b.min_spo2);

    // A seven-mile hop never reaches its filed cruise altitude.
    FlightPlan hop;
    hop.departure = kIxd;
    hop.destination = kOjc;
    hop.cruise_alt_ft = 9000.0;
    const Briefing hb = build_briefing(hop);
    double hop_peak = 0.0;
    for (const ProfilePoint& p : hb.profile) hop_peak = std::fmax(hop_peak, p.altitude_ft);
    CHECK(hop_peak < 9000.0);
    CHECK_NEAR(hb.profile.back().altitude_ft, kOjc.elevation_ft, 1e-6);

    // Hazards: one box sitting on the route midpoint, one far away.
    const LatLon mid = intermediate_point(kPhx.pos, kIxd.pos, 0.5);
    const Hazard on_route{"W1", "SIGMET",
                          {{mid.lat_deg - 0.5, mid.lon_deg - 0.5},
                           {mid.lat_deg - 0.5, mid.lon_deg + 0.5},
                           {mid.lat_deg + 0.5, mid.lon_deg + 0.5},
                           {mid.lat_deg + 0.5, mid.lon_deg - 0.5}},
                          "TURB", 0, 0, {}, {}};
    const Hazard far_away{"W2", "AIRMET", {{47.0, -70.0}, {47.0, -69.0}, {48.0, -69.0}},
                          "IFR", 0, 0, {}, {}};
    const Briefing with_hazards = build_briefing(plan, {on_route, far_away});
    CHECK(with_hazards.hazards_on_route.size() == 1);
    CHECK(with_hazards.hazards_on_route.front() == "SIGMET W1");

    bool threw = false;
    try {
        FlightPlan bad = plan;
        bad.cruise_alt_ft = 500.0;
        build_briefing(bad);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);

    const std::string json = to_json(b, plan);
    CHECK(json.find("\"from\": \"KPHX\"") != std::string::npos);
    CHECK(json.find("91.211(a)(1)") != std::string::npos);
    CHECK(json.front() == '{');
}

static Request get_request(const std::string& target) {
    Request request;
    CHECK(parse_request_line("GET " + target + " HTTP/1.1\r\nHost: x\r\n\r\n", request));
    return request;
}

static void test_http_parsing() {
    CHECK(url_decode("a%20b+c") == "a b c");
    CHECK(url_decode("100%") == "100%");    // dangling percent is kept literally
    CHECK(url_decode("%zz") == "%zz");

    const auto query = parse_query("from=KPHX&to=KIXD&empty=&flag&&alt=13500");
    CHECK(query.at("from") == "KPHX");
    CHECK(query.at("alt") == "13500");
    CHECK(query.at("empty").empty());
    CHECK(query.count("flag") == 1);
    CHECK(query.size() == 5);

    Request request;
    CHECK(parse_request_line("GET /briefing?from=KPHX HTTP/1.1\r\n\r\n", request));
    CHECK(request.method == "GET");
    CHECK(request.path == "/briefing");
    CHECK(request.query.at("from") == "KPHX");

    CHECK(!parse_request_line("GARBAGE\r\n\r\n", request));
    CHECK(!parse_request_line("GET /x\r\n\r\n", request));             // no version
    CHECK(!parse_request_line("GET x HTTP/1.1\r\n\r\n", request));     // no leading slash
    CHECK(!parse_request_line("GET /x SPDY/3\r\n\r\n", request));
    CHECK(!parse_request_line("", request));
}

static void test_api() {
    const AirportTable& airports = demo_airports();

    const Response ok =
        briefing_endpoint(get_request("/briefing?from=KPHX&to=KIXD&alt=13500&gs=140"), airports);
    CHECK(ok.status == 200);
    CHECK(ok.body.find("\"crew_oxygen_required\": true") != std::string::npos);
    CHECK(ok.body.find("\"from\": \"KPHX\"") != std::string::npos);

    const Response pressurized = briefing_endpoint(
        get_request("/briefing?from=KPHX&to=KIXD&alt=25000&gs=280&pressurized=true"), airports);
    CHECK(pressurized.status == 200);
    CHECK(pressurized.body.find("\"crew_oxygen_required\": false") != std::string::npos);

    auto status_of = [&](const std::string& target) {
        return briefing_endpoint(get_request(target), airports).status;
    };
    CHECK(status_of("/briefing") == 400);                                    // nothing given
    CHECK(status_of("/briefing?from=KPHX") == 400);                          // no destination
    CHECK(status_of("/briefing?from=KPHX&to=ZZZZ") == 404);                  // unknown airport
    CHECK(status_of("/briefing?from=KPHX&to=KPHX") == 400);                  // same airport
    CHECK(status_of("/briefing?from=KPHX&to=KIXD&alt=abc") == 400);
    CHECK(status_of("/briefing?from=KPHX&to=KIXD&alt=12000ft") == 400);      // trailing junk
    CHECK(status_of("/briefing?from=KPHX&to=KIXD&alt=nan") == 400);
    CHECK(status_of("/briefing?from=KPHX&to=KIXD&alt=999999") == 400);       // out of range
    CHECK(status_of("/briefing?from=KPHX&to=KIXD&alt=500") == 400);          // below the field
    CHECK(status_of("/briefing?from=KPHX&to=KIXD&gs=0") == 400);
    CHECK(status_of("/briefing?from=KPHX&to=KIXD&pressurized=maybe") == 400);

    // Hostile input must not be reflected into the JSON error body.
    const Response hostile =
        briefing_endpoint(get_request("/briefing?from=%22%7D%3Cscript%3E&to=KIXD"), airports);
    CHECK(hostile.status == 404);
    CHECK(hostile.body.find("script") == std::string::npos);

    CHECK(health_endpoint().status == 200);
    CHECK(airports_endpoint(airports).body.find("KIXD") != std::string::npos);

    // Routing: unknown path, wrong method, handler that throws.
    HttpServer server;
    server.get("/health", [](const Request&) { return health_endpoint(); });
    server.get("/boom", [](const Request&) -> Response { throw std::runtime_error("x"); });
    CHECK(server.dispatch(get_request("/health")).status == 200);
    CHECK(server.dispatch(get_request("/nope")).status == 404);
    CHECK(server.dispatch(get_request("/boom")).status == 500);
    Request post = get_request("/health");
    post.method = "POST";
    CHECK(server.dispatch(post).status == 405);
}

// Two stations observed at epoch 1,000,000. KSFO is not in the fallback table.
static const char* kSnapshotText =
    "#corridor-weather v1 generated=1000100\n"
    "KPHX\t33.4343\t-112.0116\t1135\t1000000\tVFR\t\t10\t230\t4\t22.2\t29.42\tMETAR KPHX A2942\n"
    "KSFO\t37.6188\t-122.3754\t13\t1000000\tIFR\t800\t2.5\t\t0\t12\t30.10\tMETAR KSFO \"q\"\n";

static void test_weather() {
    WeatherSnapshot snapshot;
    std::string error;
    {
        std::istringstream in(kSnapshotText);
        CHECK(parse_snapshot(in, snapshot, error));
    }
    CHECK(snapshot.generated_epoch == 1000100);
    CHECK(snapshot.stations.size() == 2);
    const StationWeather& phx = snapshot.stations.at("KPHX");
    CHECK(!phx.ceiling_ft.has_value());            // empty field means unknown
    CHECK_NEAR(*phx.altimeter_inhg, 29.42, 1e-9);
    CHECK(phx.observed_epoch == 1000000);
    CHECK_NEAR(*snapshot.stations.at("KSFO").ceiling_ft, 800.0, 1e-9);
    CHECK(!snapshot.stations.at("KSFO").wind_dir_deg.has_value());

    auto rejects = [](const std::string& text) {
        std::istringstream in(text);
        WeatherSnapshot out;
        std::string why;
        return !parse_snapshot(in, out, why) && !why.empty() && out.stations.empty();
    };
    const std::string header = "#corridor-weather v1 generated=1\n";
    CHECK(rejects(""));
    CHECK(rejects("KPHX\t33\t-112\n"));                                        // no header
    CHECK(rejects(header + "KPHX\t33.4\t-112.0\t1135\n"));                     // too few fields
    CHECK(rejects(header + "KPHX\tabc\t-112\t1135\t1\tVFR\t\t\t\t\t\t\t\n"));  // bad latitude
    CHECK(rejects(header + "KPHX\t95\t-112\t1135\t1\tVFR\t\t\t\t\t\t\t\n"));   // out of range
    CHECK(rejects(header + "K<X>\t33\t-112\t1135\t1\tVFR\t\t\t\t\t\t\t\n"));   // bad station
    CHECK(rejects(header + "KPHX\t33\t-112\t1135\t1\tVFR\t\t\t\t\t\t29.9x\t\n"));

    const AirportTable& airports = demo_airports();
    const std::int64_t fresh_now = 1000000 + 20 * 60;       // 20 minutes after observation
    const std::int64_t stale_now = 1000000 + 5 * 60 * 60;   // 5 hours after

    // Airport known only from the weather feed resolves.
    const Response sfo = briefing_endpoint(get_request("/briefing?from=KSFO&to=KPHX&alt=9000"),
                                           airports, &snapshot, fresh_now);
    CHECK(sfo.status == 200);
    CHECK(sfo.body.find("\"flight_category\": \"IFR\"") != std::string::npos);
    CHECK(sfo.body.find("METAR KSFO \\\"q\\\"") != std::string::npos);  // raw text escaped
    CHECK(briefing_endpoint(get_request("/briefing?from=KSFO&to=KPHX"), airports).status == 404);

    // A fresh METAR altimeter is used; 29.42 is lower pressure, so lower SpO2.
    const std::string target = "/briefing?from=KPHX&to=KIXD&alt=13500&gs=140";
    const Response live = briefing_endpoint(get_request(target), airports, &snapshot, fresh_now);
    const Response standard = briefing_endpoint(get_request(target), airports);
    CHECK(live.body.find("\"altimeter_source\": \"METAR KPHX\"") != std::string::npos);
    CHECK(live.body.find("\"stale\": false") != std::string::npos);
    CHECK(live.body.find("\"destination\": null") != std::string::npos);  // no KIXD METAR
    CHECK(live.body.find("\"min_spo2_pct\": 81.5") == std::string::npos);
    CHECK(standard.body.find("\"min_spo2_pct\": 81.5") != std::string::npos);
    CHECK(standard.body.find("\"weather\"") == std::string::npos);

    // A stale METAR is shown and flagged but not used in the calculation.
    const Response stale = briefing_endpoint(get_request(target), airports, &snapshot, stale_now);
    CHECK(stale.body.find("\"stale\": true") != std::string::npos);
    CHECK(stale.body.find("\"altimeter_source\": \"standard 29.92\"") != std::string::npos);
    CHECK(stale.body.find("\"min_spo2_pct\": 81.5") != std::string::npos);

    // An explicit altimeter always wins.
    const Response manual = briefing_endpoint(get_request(target + "&altimeter=30.10"), airports,
                                              &snapshot, fresh_now);
    CHECK(manual.body.find("\"altimeter_source\": \"request\"") != std::string::npos);

    const Response listed = airports_endpoint(airports, &snapshot);
    CHECK(listed.body.find("KSFO") != std::string::npos);
    CHECK(listed.body.find("KDEN") != std::string::npos);

    // Store: load, survive a corrupt file, pick up a repaired one.
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                       ("corridor_test_" + std::to_string(::getpid()) + ".tsv");
    auto write_file = [&](const std::string& text) { std::ofstream(path) << text; };

    WeatherStore missing((path.string() + ".absent"));
    CHECK(missing.current() == nullptr);
    CHECK(health_endpoint(&missing, fresh_now).body.find("degraded") != std::string::npos);

    write_file(kSnapshotText);
    WeatherStore store(path.string());
    CHECK(store.reload());
    CHECK(store.current() && store.current()->stations.size() == 2);
    CHECK(health_endpoint(&store, fresh_now).body.find("\"status\": \"ok\"") != std::string::npos);
    CHECK(health_endpoint(&store, stale_now).body.find("degraded") != std::string::npos);

    write_file("garbage that is not a snapshot\n");
    std::string reload_error;
    CHECK(!store.reload(&reload_error));
    CHECK(!reload_error.empty());
    CHECK(store.current() && store.current()->stations.size() == 2);  // last good one kept
    CHECK(store.reload_failures() == 1);

    write_file(std::string(kSnapshotText) +
               "KIXD\t38.8309\t-94.8903\t1087\t1000000\tVFR\t\t10\t150\t4\t5\t30.40\tMETAR KIXD\n");
    CHECK(store.reload());
    CHECK(store.current()->stations.size() == 3);
    CHECK(store.reloads() == 2);
    CHECK(weather_metrics_text(store, fresh_now).find("corridor_weather_stations 3") !=
          std::string::npos);
    std::filesystem::remove(path);
}

// A box of +/- half_deg around a centre point.
static std::vector<LatLon> box_around(LatLon c, double half_deg) {
    return {{c.lat_deg - half_deg, c.lon_deg - half_deg},
            {c.lat_deg - half_deg, c.lon_deg + half_deg},
            {c.lat_deg + half_deg, c.lon_deg + half_deg},
            {c.lat_deg + half_deg, c.lon_deg - half_deg}};
}

static std::string polygon_text(const std::vector<LatLon>& polygon) {
    std::ostringstream out;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        out << (i ? ";" : "") << polygon[i].lat_deg << "," << polygon[i].lon_deg;
    }
    return out.str();
}

static void test_hazards() {
    // Bounding-box shortcut must agree with the exact test.
    const std::vector<LatLon> square{{0.0, 0.0}, {0.0, 2.0}, {2.0, 2.0}, {2.0, 0.0}};
    CHECK(point_near_polygon({1.0, 1.0}, square, 5.0));     // inside
    CHECK(point_near_polygon({2.15, 1.0}, square, 25.0));   // 9 nm outside the north edge
    CHECK(!point_near_polygon({2.15, 1.0}, square, 5.0));
    CHECK(!point_near_polygon({10.0, 10.0}, square, 25.0)); // far away
    CHECK(point_near_polygon({1.0, -0.3}, square, 25.0));   // 18 nm west
    CHECK(!point_near_polygon({1.0, -0.6}, square, 25.0));  // 36 nm west

    FlightPlan plan;
    plan.departure = kPhx;
    plan.destination = kIxd;
    plan.cruise_alt_ft = 13500.0;
    const LatLon mid = intermediate_point(kPhx.pos, kIxd.pos, 0.5);
    const std::vector<LatLon> mid_box = box_around(mid, 0.5);

    const Hazard below{"LOW", "G-AIRMET", mid_box, "TURB-LO", 999000, 1010000, 0.0, 12000.0};
    const Hazard level{"MID", "G-AIRMET", mid_box, "ICE", 999000, 1010000, 10000.0, 18000.0};
    const Hazard no_band{"SFC", "G-AIRMET", mid_box, "IFR", 999000, 1010000, {}, {}};
    const Hazard far{"FAR", "SIGMET", box_around({47.0, -70.0}, 0.5), "TURB", 999000, 1010000,
                     0.0, 45000.0};
    const Hazard at_departure{"DEP", "G-AIRMET", box_around(kPhx.pos, 0.3), "LLWS", 999000,
                              1010000, 0.0, 3000.0};

    const Briefing b = build_briefing(plan, {below, level, no_band, far, at_departure});
    CHECK(b.hazard_hits.size() == 4);
    CHECK(b.hazards_on_route.size() == 4);

    const HazardHit& low_hit = b.hazard_hits[0];
    CHECK(low_hit.hazard.id == "LOW");
    CHECK(low_hit.at_altitude.has_value() && !*low_hit.at_altitude);  // cruising above it
    const double mid_minute = b.duration_min / 2.0;
    CHECK(low_hit.enter_min && low_hit.exit_min);
    CHECK(*low_hit.enter_min < mid_minute && *low_hit.exit_min > mid_minute);
    const double minutes_inside = *low_hit.exit_min - *low_hit.enter_min;
    CHECK(minutes_inside > 25.0 && minutes_inside < 70.0);  // ~110 nm at 140 kt

    CHECK(b.hazard_hits[1].at_altitude.has_value() && *b.hazard_hits[1].at_altitude);
    CHECK(!b.hazard_hits[2].at_altitude.has_value());  // no altitude band to compare

    const HazardHit& dep_hit = b.hazard_hits[3];
    CHECK(dep_hit.hazard.id == "DEP");
    CHECK_NEAR(*dep_hit.enter_min, 0.0, 1e-9);       // starts inside it
    CHECK(*dep_hit.at_altitude);                     // climbing through 0-3,000 ft
    CHECK(*dep_hit.exit_min < 30.0);

    const std::string json = to_json(b, plan);
    CHECK(json.find("\"id\": \"LOW\"") != std::string::npos);
    CHECK(json.find("\"at_your_altitude\": false") != std::string::npos);
    CHECK(json.find("\"at_your_altitude\": null") != std::string::npos);
    CHECK(json.find("\"id\": \"FAR\"") == std::string::npos);

    // Snapshot parsing.
    const std::string header = "#corridor-hazards v1 generated=1000100\n";
    const std::string poly = polygon_text(mid_box);
    const std::string good = header +
        "GA-1\tG-AIRMET\tTURB-LO\t999000\t1010000\t0\t12000\t" + poly + "\n" +
        "GA-2\tG-AIRMET\tIFR\t999000\t1010000\t\t\t" + poly + "\n" +
        "OLD-1\tSIGMET\tCONVECTIVE\t900000\t950000\t0\t45000\t" + poly + "\n";
    HazardSnapshot snapshot;
    std::string error;
    {
        std::istringstream in(good);
        CHECK(parse_hazard_snapshot(in, snapshot, error));
    }
    CHECK(snapshot.generated_epoch == 1000100);
    CHECK(snapshot.areas.size() == 3);
    CHECK(snapshot.areas[0].polygon.size() == 4);
    CHECK_NEAR(*snapshot.areas[0].high_ft, 12000.0, 1e-9);
    CHECK(!snapshot.areas[1].low_ft.has_value());

    {
        std::istringstream in(header);  // header only: quiet weather is valid
        HazardSnapshot empty;
        CHECK(parse_hazard_snapshot(in, empty, error) && empty.areas.empty());
    }
    auto rejects = [](const std::string& text) {
        std::istringstream in(text);
        HazardSnapshot out;
        std::string why;
        return !parse_hazard_snapshot(in, out, why) && !why.empty() && out.areas.empty();
    };
    CHECK(rejects("GA-1\tG-AIRMET\n"));                                              // no header
    CHECK(rejects(header + "GA-1\tG-AIRMET\tICE\t1\t2\t0\t100\n"));                  // 7 fields
    CHECK(rejects(header + "GA-1\tG-AIRMET\tICE\t1\t2\t0\t100\t1,1;2,2\n"));         // 2 points
    CHECK(rejects(header + "GA-1\tG-AIRMET\tICE\t1\t2\t0\t100\t1,1;2,2;95,3\n"));    // bad lat
    CHECK(rejects(header + "GA-1\tG-AIRMET\tICE\t1\t2\t0\t100\t1,1;2,2;x,3\n"));
    CHECK(rejects(header + "GA\"1\tG-AIRMET\tICE\t1\t2\t0\t100\t" + poly + "\n"));   // bad id
    CHECK(rejects(header + "GA-1\tG-AIRMET\tICE\t5\t2\t0\t100\t" + poly + "\n"));    // ends first
    CHECK(rejects(header + "GA-1\tG-AIRMET\tICE\t1\t2\t900\t100\t" + poly + "\n"));  // band flipped

    // Endpoint: expired areas are dropped, feed age is reported.
    const AirportTable& airports = demo_airports();
    const std::string target = "/briefing?from=KPHX&to=KIXD&alt=13500&gs=140";
    const std::int64_t fresh_now = 1000100 + 5 * 60;
    const Response fresh =
        briefing_endpoint(get_request(target), airports, nullptr, fresh_now, &snapshot);
    CHECK(fresh.status == 200);
    CHECK(fresh.body.find("\"id\": \"GA-1\"") != std::string::npos);
    CHECK(fresh.body.find("\"id\": \"GA-2\"") != std::string::npos);
    CHECK(fresh.body.find("OLD-1") == std::string::npos);  // expired at 950000
    CHECK(fresh.body.find("\"hazard_feed\": {\"status\": \"ok\"") != std::string::npos);
    CHECK(fresh.body.find("\"active_areas\": 2") != std::string::npos);

    // Feed went quiet an hour ago: flagged stale, unexpired areas still shown.
    const Response stale = briefing_endpoint(get_request(target), airports, nullptr,
                                             1000100 + 60 * 60, &snapshot);
    CHECK(stale.body.find("\"hazard_feed\": {\"status\": \"stale\"") != std::string::npos);
    CHECK(stale.body.find("\"id\": \"GA-1\"") != std::string::npos);

    // After every area has expired nothing is reported.
    const Response later = briefing_endpoint(get_request(target), airports, nullptr,
                                             1010000 + 1, &snapshot);
    CHECK(later.body.find("\"active_areas\": 0") != std::string::npos);
    CHECK(later.body.find("GA-1") == std::string::npos);

    const Response none = briefing_endpoint(get_request(target), airports);
    CHECK(none.body.find("hazard_feed") == std::string::npos);

    // /hazards lists unexpired areas with their polygons.
    const Response listing = hazards_endpoint(&snapshot, fresh_now);
    CHECK(listing.body.find("\"id\": \"GA-1\"") != std::string::npos);
    CHECK(listing.body.find("\"polygon\": [[") != std::string::npos);
    CHECK(listing.body.find("OLD-1") == std::string::npos);
    CHECK(hazards_endpoint().body == "{\"generated_epoch\": 0, \"areas\": []}\n");

    // Store and health.
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                       ("corridor_hz_" + std::to_string(::getpid()) + ".tsv");
    { std::ofstream(path) << good; }
    HazardStore store(path.string());
    CHECK(store.reload());
    CHECK(store.current()->areas.size() == 3);
    CHECK(health_endpoint(nullptr, fresh_now, &store).body.find("\"status\": \"ok\"") !=
          std::string::npos);
    CHECK(health_endpoint(nullptr, 1000100 + 60 * 60, &store).body.find("degraded") !=
          std::string::npos);
    { std::ofstream(path) << "junk\n"; }
    CHECK(!store.reload());
    CHECK(store.current()->areas.size() == 3);  // last good snapshot kept
    CHECK(hazard_metrics_text(store, fresh_now).find("corridor_hazard_areas 3") !=
          std::string::npos);
    std::filesystem::remove(path);

    HazardStore missing(path.string() + ".absent");
    CHECK(health_endpoint(nullptr, fresh_now, &missing).body.find("degraded") !=
          std::string::npos);
}

static std::string to_hex(const std::string& bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (unsigned char c : bytes) {
        out += digits[c >> 4];
        out += digits[c & 0x0F];
    }
    return out;
}

// A masked frame as a client would send it.
static std::string client_frame(WsOpcode opcode, const std::string& payload, bool masked = true) {
    const unsigned char mask[4] = {0x11, 0x22, 0x33, 0x44};
    std::string frame;
    frame += static_cast<char>(0x80 | static_cast<int>(opcode));
    const int mask_bit = masked ? 0x80 : 0x00;
    if (payload.size() < 126) {
        frame += static_cast<char>(mask_bit | static_cast<int>(payload.size()));
    } else {
        frame += static_cast<char>(mask_bit | 126);
        frame += static_cast<char>((payload.size() >> 8) & 0xFF);
        frame += static_cast<char>(payload.size() & 0xFF);
    }
    if (!masked) return frame + payload;
    frame.append(reinterpret_cast<const char*>(mask), 4);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        frame += static_cast<char>(static_cast<unsigned char>(payload[i]) ^ mask[i % 4]);
    }
    return frame;
}

static void test_websocket() {
    // Published SHA-1 and base64 test vectors.
    CHECK(to_hex(sha1("")) == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    CHECK(to_hex(sha1("abc")) == "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK(to_hex(sha1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")) ==
          "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    CHECK(to_hex(sha1(std::string(1000, 'a'))) == "291e9a6c66994949b57ba5e650361e98fc36b1ba");
    CHECK(base64_encode("") == "");
    CHECK(base64_encode("f") == "Zg==");
    CHECK(base64_encode("fo") == "Zm8=");
    CHECK(base64_encode("foobar") == "Zm9vYmFy");
    // The worked example in RFC 6455 section 1.3.
    CHECK(websocket_accept_key("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

    const auto headers = parse_headers(
        "GET /alerts HTTP/1.1\r\nHost: x\r\nUPGRADE:  WebSocket \r\nSec-WebSocket-Key: abc\r\n\r\nbody: no\r\n");
    CHECK(headers.at("upgrade") == "WebSocket");   // name lowercased, value trimmed
    CHECK(headers.at("sec-websocket-key") == "abc");
    CHECK(headers.count("body") == 0);             // stops at the blank line

    Request upgrade;
    upgrade.method = "GET";
    upgrade.headers = {{"upgrade", "WebSocket"}, {"sec-websocket-version", "13"},
                       {"sec-websocket-key", "dGhlIHNhbXBsZSBub25jZQ=="}};
    std::string why;
    const auto handshake = websocket_handshake_response(upgrade, why);
    CHECK(handshake.has_value());
    CHECK(handshake->rfind("HTTP/1.1 101 ", 0) == 0);
    CHECK(handshake->find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n") !=
          std::string::npos);

    Request bad = upgrade;
    bad.headers["sec-websocket-version"] = "8";
    CHECK(!websocket_handshake_response(bad, why) && !why.empty());
    bad = upgrade;
    bad.headers.erase("sec-websocket-key");
    CHECK(!websocket_handshake_response(bad, why));
    bad = upgrade;
    bad.headers.erase("upgrade");
    CHECK(!websocket_handshake_response(bad, why));

    // Encoding: the three length forms.
    CHECK(encode_ws_frame(WsOpcode::Text, "hi") == std::string("\x81\x02hi", 4));
    const std::string medium = encode_ws_frame(WsOpcode::Text, std::string(300, 'x'));
    CHECK(medium.size() == 304 && static_cast<unsigned char>(medium[1]) == 126);
    const std::string large = encode_ws_frame(WsOpcode::Text, std::string(70000, 'x'));
    CHECK(large.size() == 70010 && static_cast<unsigned char>(large[1]) == 127);

    // Decoding: masked client frames, delivered whole, in pieces, and back to back.
    std::string buffer = client_frame(WsOpcode::Text, "hello") + client_frame(WsOpcode::Ping, "p");
    WsFrame frame;
    CHECK(decode_ws_frame(buffer, frame) == WsDecode::Complete);
    CHECK(frame.opcode == WsOpcode::Text && frame.payload == "hello");
    CHECK(decode_ws_frame(buffer, frame) == WsDecode::Complete);
    CHECK(frame.opcode == WsOpcode::Ping && frame.payload == "p");
    CHECK(buffer.empty());
    CHECK(decode_ws_frame(buffer, frame) == WsDecode::Incomplete);

    const std::string whole = client_frame(WsOpcode::Text, std::string(500, 'z'));
    buffer = whole.substr(0, 3);
    CHECK(decode_ws_frame(buffer, frame) == WsDecode::Incomplete);
    buffer += whole.substr(3);
    CHECK(decode_ws_frame(buffer, frame) == WsDecode::Complete);
    CHECK(frame.payload == std::string(500, 'z'));

    auto is_error = [](std::string bytes) {
        WsFrame ignored;
        return decode_ws_frame(bytes, ignored) == WsDecode::Error;
    };
    CHECK(is_error(client_frame(WsOpcode::Text, "x", false)));          // unmasked
    CHECK(is_error(std::string("\xF1\x80", 2) + "abcd"));               // reserved bits set
    CHECK(is_error(std::string("\x83\x80", 2) + "abcd"));               // unknown opcode 3
    CHECK(is_error(client_frame(WsOpcode::Ping, std::string(200, 'p')))); // oversized control
    std::string huge = "\x81\xFF";                                       // claims 2^40 bytes
    huge += std::string("\x00\x00\x01\x00\x00\x00\x00\x00", 8);
    CHECK(is_error(huge));
}

// Reads one unmasked server frame from a blocking socket. Empty opcode on timeout.
static std::optional<WsFrame> read_server_frame(int fd) {
    auto read_exact = [fd](std::size_t count, std::string& into) {
        into.clear();
        while (into.size() < count) {
            char chunk[4096];
            const ssize_t n = ::recv(fd, chunk, std::min(sizeof(chunk), count - into.size()), 0);
            if (n <= 0) return false;
            into.append(chunk, static_cast<std::size_t>(n));
        }
        return true;
    };
    std::string head, extra, payload;
    if (!read_exact(2, head)) return std::nullopt;
    std::size_t length = static_cast<unsigned char>(head[1]) & 0x7F;
    if (length == 126) {
        if (!read_exact(2, extra)) return std::nullopt;
        length = (static_cast<std::size_t>(static_cast<unsigned char>(extra[0])) << 8) |
                 static_cast<unsigned char>(extra[1]);
    }
    if (length && !read_exact(length, payload)) return std::nullopt;
    WsFrame frame;
    frame.opcode = static_cast<WsOpcode>(static_cast<unsigned char>(head[0]) & 0x0F);
    frame.payload = payload;
    return frame;
}

static void test_alerts() {
    RouteStatus before;
    before.departure = "KPHX";
    before.destination = "KIXD";
    before.departure_category = "VFR";
    before.destination_category = "VFR";
    before.hazard_feed = "ok";
    before.hazards.push_back({"H1", "G-AIRMET ICE", 0.0, 20000.0, 150.0, 295.0, true});

    CHECK(diff_route_status(before, before).empty());

    RouteStatus after = before;
    after.destination_category = "IFR";
    auto changes = diff_route_status(before, after);
    CHECK(changes.size() == 1);
    CHECK(changes[0] == "KIXD flight category changed from VFR to IFR");

    after = before;
    after.departure_category.clear();
    CHECK(diff_route_status(before, after)[0] == "KPHX weather is no longer current");
    CHECK(diff_route_status(after, before)[0] == "KPHX weather is current again: VFR");

    after = before;
    after.hazards.clear();
    after.hazards.push_back({"H2", "G-AIRMET TURB-HI", 18000.0, 35000.0, 10.0, 40.0, false});
    after.hazards.push_back({"H3", "G-AIRMET IFR", {}, {}, 5.0, 9.0, {}});
    changes = diff_route_status(before, after);
    CHECK(changes.size() == 3);
    CHECK(changes[0] ==
          "New hazard on route: G-AIRMET TURB-HI, 18000 to 35000 ft, minutes 10 to 40, "
          "not at your altitude");
    CHECK(changes[1] == "New hazard on route: G-AIRMET IFR, minutes 5 to 9");
    CHECK(changes[2] == "Hazard no longer on route: G-AIRMET ICE");

    after = before;
    after.crew_oxygen_required = true;
    after.hazard_feed = "stale";
    changes = diff_route_status(before, after);
    CHECK(changes.size() == 2);
    CHECK(changes[0] == "Crew oxygen is now required (14 CFR 91.211)");
    CHECK(changes[1] == "Hazard data is now stale");
    CHECK(diff_route_status(after, before)[1] == "Hazard data is current again");

    // route_status mirrors what /briefing would say, and reports bad requests.
    const AirportTable& airports = demo_airports();
    const auto status =
        route_status(get_request("/alerts?from=KPHX&to=KIXD&alt=13500&gs=140"), airports);
    CHECK(status.has_value());
    CHECK(status->crew_oxygen_required && status->hazard_feed == "not configured");
    CHECK(status->departure_category.empty());
    CHECK(route_status_json(*status).find("\"departure_category\": null") != std::string::npos);
    Response error;
    CHECK(!route_status(get_request("/alerts?from=KPHX&to=ZZZZ"), airports, nullptr, 0, nullptr,
                        &error));
    CHECK(error.status == 404);

    // Hub end to end over a socket pair: handshake, initial status, an alert
    // when the data changes, and a clean close.
    int sockets[2];
    CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    const int client = sockets[0];
    timeval timeout{};
    timeout.tv_sec = 5;
    ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    std::mutex mutex;
    RouteStatus live = before;
    std::atomic<std::uint64_t> version{1};
    AlertHub hub(
        [&](const Request&) -> std::optional<RouteStatus> {
            std::lock_guard<std::mutex> lock(mutex);
            return live;
        },
        [&] { return version.load(); }, 1);
    hub.start();

    const std::string handshake = "HTTP/1.1 101 Switching Protocols\r\n\r\n";
    CHECK(hub.add(sockets[1], get_request("/alerts?from=KPHX&to=KIXD"), before, handshake));
    CHECK(!hub.add(999, get_request("/alerts?from=KPHX&to=KIXD"), before, handshake));  // full

    std::string greeting(handshake.size(), '\0');
    CHECK(::recv(client, greeting.data(), greeting.size(), MSG_WAITALL) ==
          static_cast<ssize_t>(handshake.size()));
    CHECK(greeting == handshake);
    auto first = read_server_frame(client);
    CHECK(first && first->opcode == WsOpcode::Text);
    CHECK(first && first->payload.find("\"type\": \"status\"") != std::string::npos);
    CHECK(hub.connections() == 1);

    {
        std::lock_guard<std::mutex> lock(mutex);
        live.destination_category = "IFR";
    }
    version.fetch_add(1);
    auto alert = read_server_frame(client);
    CHECK(alert && alert->payload.find("\"type\": \"alert\"") != std::string::npos);
    CHECK(alert && alert->payload.find("KIXD flight category changed from VFR to IFR") !=
                       std::string::npos);
    CHECK(hub.alerts_sent() == 1);

    // A version bump with no real change must not produce a message; the ping
    // reply arriving next proves nothing was sent in between.
    version.fetch_add(1);
    const std::string ping = client_frame(WsOpcode::Ping, "are-you-there");
    CHECK(::send(client, ping.data(), ping.size(), 0) == static_cast<ssize_t>(ping.size()));
    auto pong = read_server_frame(client);
    CHECK(pong && pong->opcode == WsOpcode::Pong && pong->payload == "are-you-there");
    CHECK(hub.alerts_sent() == 1);

    const std::string close = client_frame(WsOpcode::Close, "\x03\xE8");
    CHECK(::send(client, close.data(), close.size(), 0) == static_cast<ssize_t>(close.size()));
    auto closed = read_server_frame(client);
    CHECK(closed && closed->opcode == WsOpcode::Close);
    char byte;
    CHECK(::recv(client, &byte, 1, 0) == 0);  // hub closed its end
    hub.stop();
    CHECK(hub.connections() == 0);
    ::close(client);
}

static void test_saved_routes() {
    const Request plan = get_request("/alerts?from=kphx&to=KIXD&alt=13500&gs=140&pressurized=false");
    CHECK(route_key(plan) == "KPHX|KIXD|13500|140|0");
    CHECK(route_key(get_request("/alerts?from=KPHX&to=KIXD")) == "KPHX|KIXD|8000|140|0");

    RouteStatus before;
    before.departure = "KPHX";
    before.destination = "KIXD";
    before.departure_category = "VFR";
    before.destination_category = "VFR";
    before.crew_oxygen_required = true;
    before.hazard_feed = "ok";
    before.min_spo2 = 0.82;
    before.hazards.push_back({"H1", "G-AIRMET ICE", 0.0, 20000.0, 150.0, 295.0, true});

    const auto path = std::filesystem::temp_directory_path() / "corridor-saved-routes-test.tsv";
    std::filesystem::remove(path);
    {
        SavedRoutes saved(path.string());
        saved.remember(route_key(plan), before);
        CHECK(saved.size() == 1);
    }
    SavedRoutes reloaded(path.string());
    const auto stored = reloaded.find(route_key(plan));
    CHECK(stored.has_value());
    if (stored) {
        CHECK(stored->departure_category == "VFR");
        CHECK(stored->crew_oxygen_required);
        CHECK(stored->hazards.size() == 1 && stored->hazards[0].id == "H1");
        CHECK(stored->hazards[0].at_altitude && *stored->hazards[0].at_altitude);
        CHECK_NEAR(stored->min_spo2, 0.82, 1e-6);
    }
    std::filesystem::remove(path);

    // A second connection hears what changed after the first one closed, including
    // across a new hub that only has the journal file.
    const auto watch = std::filesystem::temp_directory_path() / "corridor-saved-watch-test.tsv";
    std::filesystem::remove(watch);
    RouteStatus live = before;
    std::atomic<std::uint64_t> version{1};
    auto listen = [&](AlertHub& hub, const RouteStatus& initial) {
        int sockets[2];
        CHECK(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
        timeval timeout{};
        timeout.tv_sec = 5;
        ::setsockopt(sockets[0], SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        const std::string handshake = "HTTP/1.1 101 Switching Protocols\r\n\r\n";
        CHECK(hub.add(sockets[1], plan, initial, handshake));
        std::string greeting(handshake.size(), '\0');
        CHECK(::recv(sockets[0], greeting.data(), greeting.size(), MSG_WAITALL) ==
              static_cast<ssize_t>(handshake.size()));
        auto frame = read_server_frame(sockets[0]);
        const std::string close = client_frame(WsOpcode::Close, "\x03\xE8");
        CHECK(::send(sockets[0], close.data(), close.size(), 0) == static_cast<ssize_t>(close.size()));
        auto closed = read_server_frame(sockets[0]);
        CHECK(closed && closed->opcode == WsOpcode::Close);
        ::close(sockets[0]);
        return frame ? frame->payload : std::string();
    };

    {
        SavedRoutes fresh(watch.string());
        AlertHub hub([&](const Request&) { return live; }, [&] { return version.load(); }, 4, &fresh);
        hub.start();
        const std::string first = listen(hub, before);
        CHECK(first.find("\"type\": \"status\"") != std::string::npos);
        CHECK(first.find("resumed") == std::string::npos);
        hub.stop();
    }

    live.destination_category = "IFR";
    live.hazards.clear();
    SavedRoutes resumed(watch.string());
    CHECK(resumed.size() == 1);
    AlertHub hub([&](const Request&) { return live; }, [&] { return version.load(); }, 4, &resumed);
    hub.start();
    const std::string again = listen(hub, live);
    CHECK(again.find("\"type\": \"alert\"") != std::string::npos);
    CHECK(again.find("\"resumed\": true") != std::string::npos);
    CHECK(again.find("KIXD flight category changed from VFR to IFR") != std::string::npos);
    CHECK(again.find("Hazard no longer on route: G-AIRMET ICE") != std::string::npos);
    CHECK(hub.alerts_sent() == 1);
    hub.stop();
    std::filesystem::remove(watch);
}

static const char* kTafText =
    "#corridor-taf v1 generated=1001200\n"
    "KIXD\t1000000\t1000000\t9000000\t1\t1001260\t\t1\tVFR\t\t10\t160\t7\t\t\tTAF KIXD BASE\n"
    "KIXD\t1000000\t1000000\t9000000\t1001260\t9000000\tFM\t1\tIFR\t800\t2\t170\t12\t\t\tTAF KIXD FM\n"
    "KIXD\t1000000\t1000000\t9000000\t1001260\t9000000\tTEMPO\t0\tLIFR\t200\t0.5\t170\t12\t20\t\tTAF KIXD TEMPO\n"
    "KIXD\t1000000\t1000000\t9000000\t1001260\t9000000\tTEMPO\t0\tVFR\t\t10\t180\t10\t25\t\tgust only\n";

static void test_taf() {
    TafSnapshot snapshot;
    std::string error;
    {
        std::istringstream in(kTafText);
        CHECK(parse_taf_snapshot(in, snapshot, error));
    }
    CHECK(snapshot.generated_epoch == 1001200);
    CHECK(snapshot.stations.at("KIXD").size() == 4);
    CHECK(category_rank("LIFR") > category_rank("IFR"));

    // The first hour is the base line. A gust-only TEMPO is not a worse category.
    const auto early = forecast_at(snapshot, "KIXD", 1000, 1);
    CHECK(early && early->flight_category == "VFR");
    CHECK(early && early->tempo_category.empty());

    // Hours later the FM line prevails, and the worse TEMPO is the one reported.
    const auto later = forecast_at(snapshot, "KIXD", 1100000, 381);
    CHECK(later && later->flight_category == "IFR");
    CHECK(later && later->change == "FM");
    CHECK(later && later->tempo_category == "LIFR" && later->tempo_change == "TEMPO");
    CHECK(later && later->tempo_gust_kt && *later->tempo_gust_kt == 20.0);
    CHECK(!forecast_at(snapshot, "KIXD", 9000000, 1));
    CHECK(!forecast_at(snapshot, "KDEN", 1100000, 1));

    auto rejects = [](const std::string& text) {
        std::istringstream in(text);
        TafSnapshot out;
        std::string why;
        return !parse_taf_snapshot(in, out, why) && !why.empty();
    };
    CHECK(rejects("#corridor-taf v1 generated=1\nKIXD\t1\t1\t2\t1\t2\tFM\t1\tVFR\n"));
    CHECK(rejects("#corridor-taf v1 generated=1\nKIXD\t1\t1\t2\t1\t2\tFM\tmaybe\tVFR\t\t\t\t\t\t\t\n"));

    const std::int64_t now = 1001200;
    const std::string target = "/briefing?from=KPHX&to=KIXD&alt=13500&gs=140";
    const Response brief = briefing_endpoint(get_request(target), demo_airports(), nullptr, now,
                                             nullptr, &snapshot);
    CHECK(brief.status == 200);
    CHECK(brief.body.find("\"arrival\"") != std::string::npos);
    CHECK(brief.body.find("\"flight_category\": \"IFR\"") != std::string::npos);
    CHECK(brief.body.find("\"tempo_category\": \"LIFR\"") != std::string::npos);

    const auto status = route_status(get_request(target), demo_airports(), nullptr, now, nullptr,
                                     nullptr, &snapshot);
    CHECK(status && status->arrival_category == "IFR");
    CHECK(status && status->arrival_tempo == "TEMPO LIFR");
    CHECK(status && status->destination_category.empty());

    RouteStatus before = *status;
    before.arrival_category = "VFR";
    before.arrival_tempo.clear();
    const auto changes = diff_route_status(before, *status);
    CHECK(std::find(changes.begin(), changes.end(),
                    "KIXD at arrival changed from VFR to IFR") != changes.end());
    CHECK(std::find(changes.begin(), changes.end(),
                    "KIXD at arrival now includes TEMPO LIFR") != changes.end());

    // A forecast file older than 30 minutes is still shown and does not drive alerts.
    snapshot.generated_epoch = now - 40 * 60;
    const Response stale = briefing_endpoint(get_request(target), demo_airports(), nullptr, now,
                                             nullptr, &snapshot);
    CHECK(stale.body.find("\"status\": \"stale\"") != std::string::npos);
    CHECK(stale.body.find("\"flight_category\": \"IFR\"") != std::string::npos);
    const auto quiet = route_status(get_request(target), demo_airports(), nullptr, now, nullptr,
                                    nullptr, &snapshot);
    CHECK(quiet && quiet->arrival_category.empty());
    CHECK(quiet && quiet->arrival_tempo.empty());
}

static double duration_min_of(const std::string& body) {
    const auto at = body.find("\"duration_min\": ");
    if (at == std::string::npos) return 0.0;
    return std::strtod(body.c_str() + at + 16, nullptr);
}

static void test_wind() {
    CHECK_NEAR(along_track_kt(90, 20, 90), -20, 1e-6);   // wind on the nose
    CHECK_NEAR(along_track_kt(270, 20, 90), 20, 1e-6);   // wind on the tail
    CHECK_NEAR(along_track_kt(0, 20, 90), 0, 1e-6);      // pure crosswind

    const double course = initial_bearing_deg(kPhx.pos, kIxd.pos);
    const double from = std::fmod(course + 180.0, 360.0);
    const std::int64_t now = 1700000000;
    std::ostringstream snap;
    snap << std::fixed << "#corridor-wind v1 generated=" << now << "\n";
    auto row = [&](LatLon pos, std::int64_t epoch, double alt) {
        snap << pos.lat_deg << "\t" << pos.lon_deg << "\t" << epoch << "\t" << alt << "\t" << from
             << "\t40\n";
    };
    for (int hour = 0; hour <= 8; ++hour) {
        const std::int64_t epoch = now + static_cast<std::int64_t>(hour) * 3600;
        row(kPhx.pos, epoch, 9880);
        row(kPhx.pos, epoch, 18280);
        row(kIxd.pos, epoch, 9880);
        row(kIxd.pos, epoch, 18280);
    }

    WindSnapshot winds;
    std::string error;
    {
        std::istringstream in(snap.str());
        CHECK(parse_wind_snapshot(in, winds, error));
    }
    const auto flow = route_wind(winds, {kPhx.pos, kIxd.pos}, 13500, now, 140);
    CHECK(flow && flow->tailwind_kt > 25.0);
    CHECK(flow && flow->groundspeed_kt > 165.0);

    const std::string target = "/briefing?from=KPHX&to=KIXD&alt=13500&gs=140";
    const Response calm = briefing_endpoint(get_request(target), demo_airports(), nullptr, now);
    const Response blown = briefing_endpoint(get_request(target), demo_airports(), nullptr, now,
                                             nullptr, nullptr, &winds);
    CHECK(duration_min_of(blown.body) < duration_min_of(calm.body) - 30.0);
    CHECK(blown.body.find("\"tailwind_kt\":") != std::string::npos);

    const auto status = route_status(get_request(target), demo_airports(), nullptr, now, nullptr,
                                     nullptr, nullptr, &winds);
    CHECK(status && status->tailwind_kt && *status->tailwind_kt > 25);
    RouteStatus slower = *status;
    slower.tailwind_kt = *status->tailwind_kt - 20;
    const auto changes = diff_route_status(slower, *status);
    CHECK(std::find(changes.begin(), changes.end(),
                    "Wind along the route changed from " +
                        std::to_string(*slower.tailwind_kt) + " kt tailwind to " +
                        std::to_string(*status->tailwind_kt) + " kt tailwind") != changes.end());

    winds.generated_epoch = now - 40 * 60;
    const Response stale = briefing_endpoint(get_request(target), demo_airports(), nullptr, now,
                                             nullptr, nullptr, &winds);
    CHECK(std::fabs(duration_min_of(stale.body) - duration_min_of(calm.body)) < 0.05);
    CHECK(stale.body.find("\"wind\"") != std::string::npos);
    CHECK(stale.body.find("\"status\": \"stale\"") != std::string::npos);
    const auto quiet = route_status(get_request(target), demo_airports(), nullptr, now, nullptr,
                                    nullptr, nullptr, &winds);
    CHECK(quiet && !quiet->tailwind_kt);

    std::istringstream bad("#corridor-wind v1 generated=1\n10\t20\t1\n");
    WindSnapshot rejected;
    CHECK(!parse_wind_snapshot(bad, rejected, error) && !error.empty());
}

int main() {
    test_http_parsing();
    test_api();
    test_weather();
    test_hazards();
    test_websocket();
    test_alerts();
    test_saved_routes();
    test_taf();
    test_wind();
    test_atmosphere();
    test_oxygen();
    test_oxygen_rules();
    test_fatigue();
    test_geo();
    test_briefing();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
