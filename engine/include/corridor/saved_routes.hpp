// Remembers the last status told to a watcher, so a later connection for the
// same route hears what changed while nobody was listening. A refresh, or a
// restarted server, resumes instead of starting from a blank picture.
//
// The journal is a file the service owns. Briefings never read it, so a missing
// or unwritable file only means the next connection starts fresh.
#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <string>

#include "corridor/api.hpp"

namespace corridor {

// from|to|alt|gs|pressurized, with the same defaults /briefing uses when a
// parameter is omitted. Two connections that describe the same flight share a key.
std::string route_key(const Request& request);

class SavedRoutes {
public:
    // Empty path keeps the journal in memory only.
    explicit SavedRoutes(std::string path = {});

    std::optional<RouteStatus> find(const std::string& key) const;
    void remember(const std::string& key, const RouteStatus& status);
    std::size_t size() const;

private:
    void load();
    void save() const;

    std::string path_;
    std::map<std::string, RouteStatus> routes_;
    mutable std::mutex mutex_;
    mutable bool warned_ = false;
};

}  // namespace corridor
