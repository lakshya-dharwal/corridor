// Strict field parsing shared by the snapshot readers. Internal to the library.
#pragma once

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

namespace corridor::detail {

inline std::vector<std::string> split(const std::string& text, char separator) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (;;) {
        const std::size_t at = text.find(separator, start);
        fields.push_back(text.substr(start, at - start));
        if (at == std::string::npos) break;
        start = at + 1;
    }
    return fields;
}

// Whole-string parse: rejects "", "12abc", "nan" and "inf".
inline bool to_double(const std::string& text, double& out) {
    if (text.empty()) return false;
    errno = 0;
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return end != text.c_str() && *end == '\0' && errno == 0 && std::isfinite(out);
}

// Empty means unknown; anything else must be a number.
inline bool to_optional(const std::string& text, std::optional<double>& out) {
    if (text.empty()) {
        out.reset();
        return true;
    }
    double value = 0.0;
    if (!to_double(text, value)) return false;
    out = value;
    return true;
}

inline bool valid_position(double lat_deg, double lon_deg) {
    return std::fabs(lat_deg) <= 90.0 && std::fabs(lon_deg) <= 180.0;
}

}  // namespace corridor::detail
