// Pilot physiology model: altitude -> ambient pressure -> alveolar oxygen -> estimated SpO2,
// plus FAA supplemental-oxygen rule checks and a simple fatigue heuristic.
//
// Educational estimate only. Not medical advice and not for operational flight planning.
#pragma once

#include <string>
#include <vector>

namespace corridor {

// ---------- Atmosphere ----------

// International Standard Atmosphere pressure (mmHg) at a pressure altitude in feet.
double isa_pressure_mmhg(double pressure_alt_ft);

// Pressure altitude from indicated altitude and the altimeter setting (inHg).
// Uses the standard pilot approximation of 1000 ft per inHg.
double pressure_altitude_ft(double indicated_alt_ft, double altimeter_inhg);

// Altitude the occupants actually experience. Pressurized cabins are capped.
double cabin_altitude_ft(double aircraft_alt_ft, bool pressurized,
                         double max_cabin_alt_ft = 8000.0);

// ---------- Oxygen ----------

// Alveolar CO2 (mmHg) for an unacclimatized person. Falls with altitude because
// hypoxia drives hyperventilation. Interpolated from textbook values.
double alveolar_pco2_mmhg(double pressure_alt_ft);

// Alveolar gas equation: PAO2 = FiO2 * (PB - 47) - PACO2 / 0.8. Clamped at zero.
double alveolar_po2_mmhg(double pressure_alt_ft, double fio2 = 0.2095);

// Severinghaus (1979) oxygen-hemoglobin dissociation curve. Returns a 0..1 fraction.
double spo2_from_po2(double po2_mmhg);

// Convenience: estimated SpO2 (0..1) breathing ambient air at a pressure altitude.
double estimated_spo2(double pressure_alt_ft);

enum class HypoxiaRisk { None, Mild, Moderate, Severe };
HypoxiaRisk classify_spo2(double spo2);
const char* to_string(HypoxiaRisk risk);

// ---------- 14 CFR 91.211 supplemental oxygen ----------

struct AltitudeSample {
    double minute;                 // minutes since departure
    double cabin_pressure_alt_ft;  // cabin pressure altitude at that time
};

struct OxygenRuleReport {
    double minutes_12500_to_14000 = 0.0;
    double minutes_above_14000 = 0.0;
    double minutes_above_15000 = 0.0;
    bool crew_oxygen_required = false;       // 91.211(a)(1) or (a)(2)
    bool passenger_oxygen_required = false;  // 91.211(a)(3)
    std::vector<std::string> findings;
};

// Samples must be in time order. Each interval is assigned to the band of its
// midpoint altitude.
OxygenRuleReport check_far_91_211(const std::vector<AltitudeSample>& samples);

// ---------- Fatigue ----------

// Heuristic 0..100 score from flight duration, overlap with the 02:00-06:00
// circadian low, and average oxygen saturation. A relative indicator, not a
// validated fatigue model.
double fatigue_score(double flight_hours, double departure_local_hour, double mean_spo2);

}  // namespace corridor
