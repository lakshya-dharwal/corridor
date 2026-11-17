#include "corridor/physiology.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace corridor {

namespace {

constexpr double kFeetToMeters = 0.3048;
constexpr double kPascalPerMmHg = 133.322;
constexpr double kWaterVaporMmHg = 47.0;      // saturated at body temperature
constexpr double kRespiratoryQuotient = 0.8;

std::string format(const char* fmt, double value) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), fmt, value);
    return buf;
}

}  // namespace

double isa_pressure_mmhg(double pressure_alt_ft) {
    const double h = pressure_alt_ft * kFeetToMeters;
    double pascal;
    if (h <= 11000.0) {
        pascal = 101325.0 * std::pow(1.0 - 2.25577e-5 * h, 5.25588);
    } else {
        pascal = 22632.06 * std::exp(-(h - 11000.0) / 6341.62);
    }
    return pascal / kPascalPerMmHg;
}

double pressure_altitude_ft(double indicated_alt_ft, double altimeter_inhg) {
    return indicated_alt_ft + (29.92 - altimeter_inhg) * 1000.0;
}

double cabin_altitude_ft(double aircraft_alt_ft, bool pressurized, double max_cabin_alt_ft) {
    return pressurized ? std::min(aircraft_alt_ft, max_cabin_alt_ft) : aircraft_alt_ft;
}

double alveolar_pco2_mmhg(double pressure_alt_ft) {
    // Unacclimatized values, Guyton & Hall, Textbook of Medical Physiology
    // (effects of low oxygen pressure on the body).
    static constexpr double kAlt[] = {0.0, 10000.0, 20000.0, 30000.0};
    static constexpr double kPco2[] = {40.0, 36.0, 24.0, 24.0};
    constexpr int n = 4;
    if (pressure_alt_ft <= kAlt[0]) return kPco2[0];
    if (pressure_alt_ft >= kAlt[n - 1]) return kPco2[n - 1];
    for (int i = 0; i + 1 < n; ++i) {
        if (pressure_alt_ft <= kAlt[i + 1]) {
            const double f = (pressure_alt_ft - kAlt[i]) / (kAlt[i + 1] - kAlt[i]);
            return kPco2[i] + f * (kPco2[i + 1] - kPco2[i]);
        }
    }
    return kPco2[n - 1];
}

double alveolar_po2_mmhg(double pressure_alt_ft, double fio2) {
    const double pb = isa_pressure_mmhg(pressure_alt_ft);
    const double po2 = fio2 * (pb - kWaterVaporMmHg) -
                       alveolar_pco2_mmhg(pressure_alt_ft) / kRespiratoryQuotient;
    return std::max(0.0, po2);
}

double spo2_from_po2(double po2_mmhg) {
    if (po2_mmhg <= 0.0) return 0.0;
    const double p3 = po2_mmhg * po2_mmhg * po2_mmhg;
    return 1.0 / (1.0 + 23400.0 / (p3 + 150.0 * po2_mmhg));
}

double estimated_spo2(double pressure_alt_ft) {
    return spo2_from_po2(alveolar_po2_mmhg(pressure_alt_ft));
}

HypoxiaRisk classify_spo2(double spo2) {
    if (spo2 >= 0.93) return HypoxiaRisk::None;
    if (spo2 >= 0.90) return HypoxiaRisk::Mild;
    if (spo2 >= 0.80) return HypoxiaRisk::Moderate;
    return HypoxiaRisk::Severe;
}

const char* to_string(HypoxiaRisk risk) {
    switch (risk) {
        case HypoxiaRisk::None: return "none";
        case HypoxiaRisk::Mild: return "mild";
        case HypoxiaRisk::Moderate: return "moderate";
        case HypoxiaRisk::Severe: return "severe";
    }
    return "unknown";
}

OxygenRuleReport check_far_91_211(const std::vector<AltitudeSample>& samples) {
    OxygenRuleReport report;
    for (std::size_t i = 0; i + 1 < samples.size(); ++i) {
        const double dt = samples[i + 1].minute - samples[i].minute;
        if (dt <= 0.0) continue;
        const double mid = 0.5 * (samples[i].cabin_pressure_alt_ft +
                                  samples[i + 1].cabin_pressure_alt_ft);
        if (mid > 15000.0) report.minutes_above_15000 += dt;
        if (mid > 14000.0) {
            report.minutes_above_14000 += dt;
        } else if (mid > 12500.0) {
            report.minutes_12500_to_14000 += dt;
        }
    }

    if (report.minutes_12500_to_14000 > 30.0) {
        report.crew_oxygen_required = true;
        report.findings.push_back(format(
            "91.211(a)(1): %.0f min between 12,500 and 14,000 ft cabin altitude; crew "
            "oxygen required for the time beyond 30 min.",
            report.minutes_12500_to_14000));
    }
    if (report.minutes_above_14000 > 0.0) {
        report.crew_oxygen_required = true;
        report.findings.push_back(format(
            "91.211(a)(2): %.0f min above 14,000 ft cabin altitude; crew oxygen required "
            "for that entire time.",
            report.minutes_above_14000));
    }
    if (report.minutes_above_15000 > 0.0) {
        report.passenger_oxygen_required = true;
        report.findings.push_back(format(
            "91.211(a)(3): %.0f min above 15,000 ft cabin altitude; oxygen must be "
            "provided to every occupant.",
            report.minutes_above_15000));
    }
    return report;
}

double fatigue_score(double flight_hours, double departure_local_hour, double mean_spo2) {
    if (flight_hours <= 0.0) return 0.0;

    const double duration_part = std::min(40.0, flight_hours * 8.0);

    // Fraction of the flight inside the 02:00-06:00 local circadian low.
    int low = 0;
    int total = 0;
    for (double t = 0.0; t <= flight_hours; t += 1.0 / 12.0) {
        const double hour = std::fmod(std::fmod(departure_local_hour + t, 24.0) + 24.0, 24.0);
        if (hour >= 2.0 && hour < 6.0) ++low;
        ++total;
    }
    const double circadian_part = 30.0 * (total > 0 ? static_cast<double>(low) / total : 0.0);

    const double hypoxia_part = std::clamp((0.97 - mean_spo2) * 200.0, 0.0, 30.0);

    return std::clamp(duration_part + circadian_part + hypoxia_part, 0.0, 100.0);
}

}  // namespace corridor
