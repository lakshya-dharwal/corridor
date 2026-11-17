// Offline demo: prints a JSON briefing for a route between built-in airports.
//
//   corridor_cli --from KPHX --to KIXD --alt 13500 --gs 140
//   corridor_cli --from KPHX --to KIXD --alt 25000 --gs 280 --pressurized
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

#include "corridor/api.hpp"

using namespace corridor;

namespace {

const AirportTable& airports() { return demo_airports(); }

int usage() {
    std::fprintf(stderr,
                 "usage: corridor_cli --from ICAO --to ICAO [--alt FT] [--gs KT]\n"
                 "                    [--altimeter INHG] [--dep-hour 0-24] [--pressurized]\n"
                 "airports:");
    for (const auto& entry : airports()) std::fprintf(stderr, " %s", entry.first.c_str());
    std::fprintf(stderr, "\n");
    return 2;
}

}  // namespace

int main(int argc, char** argv) {
    std::string from = "KPHX", to = "KIXD";
    FlightPlan plan;
    plan.cruise_alt_ft = 13500.0;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--pressurized") {
            plan.pressurized = true;
        } else if (arg == "--from" && has_value) {
            from = argv[++i];
        } else if (arg == "--to" && has_value) {
            to = argv[++i];
        } else if (arg == "--alt" && has_value) {
            plan.cruise_alt_ft = std::atof(argv[++i]);
        } else if (arg == "--gs" && has_value) {
            plan.groundspeed_kt = std::atof(argv[++i]);
        } else if (arg == "--altimeter" && has_value) {
            plan.altimeter_inhg = std::atof(argv[++i]);
        } else if (arg == "--dep-hour" && has_value) {
            plan.departure_local_hour = std::atof(argv[++i]);
        } else {
            return usage();
        }
    }

    const auto dep = airports().find(from);
    const auto dest = airports().find(to);
    if (dep == airports().end() || dest == airports().end()) return usage();
    plan.departure = dep->second;
    plan.destination = dest->second;

    try {
        const Briefing briefing = build_briefing(plan);
        std::fputs(to_json(briefing, plan, 30).c_str(), stdout);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "error: %s\n", error.what());
        return 1;
    }
    return 0;
}
