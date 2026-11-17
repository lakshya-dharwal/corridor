// Pushes route alerts over WebSocket. Each connection watches one route; when
// the live data changes, the hub re-evaluates the route and sends the client a
// plain-English list of what changed.
//
// One background thread owns every socket: it sends the handshake and all
// frames, answers pings and closes, and re-evaluates routes. HTTP workers only
// hand sockets over, so a slow or silent client never ties up a worker.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "corridor/api.hpp"
#include "corridor/http.hpp"
#include "corridor/saved_routes.hpp"

namespace corridor {

class AlertHub {
public:
    // Current status of the route a request describes, or nullopt if it can no
    // longer be evaluated.
    using StatusFn = std::function<std::optional<RouteStatus>(const Request&)>;
    // A number that changes whenever the underlying data does.
    using VersionFn = std::function<std::uint64_t()>;

    // `saved` remembers each route's last status. Null keeps the old behaviour:
    // a connection only hears about changes that happen while it is open.
    AlertHub(StatusFn status, VersionFn version, std::size_t max_connections = 256,
             SavedRoutes* saved = nullptr);
    ~AlertHub();

    void start();
    // Sends a close frame to every client and joins the thread.
    void stop();

    // Queues a socket whose upgrade request has been validated. The hub sends
    // `handshake` (the 101 response) followed by the initial status. False if
    // the hub is full or stopped; the caller then still owns the socket.
    bool add(int fd, Request request, RouteStatus initial, std::string handshake);

    std::size_t connections() const { return connection_count_.load(); }
    std::uint64_t alerts_sent() const { return alerts_sent_.load(); }
    std::string metrics_text() const;

private:
    struct Connection {
        int fd = -1;
        Request request;
        RouteStatus last;
        std::string handshake;  // pending until the hub thread adopts the socket
        std::string inbound;
    };

    void run();
    void adopt_pending();
    void read_inbound(int timeout_ms);
    void reevaluate_all();
    void ping_all();
    void drop(std::size_t index);

    const StatusFn status_;
    const VersionFn version_;
    const std::size_t max_connections_;
    SavedRoutes* saved_;

    std::mutex pending_mutex_;
    std::vector<Connection> pending_;

    std::vector<Connection> connections_;  // touched only by the hub thread
    std::atomic<std::size_t> connection_count_{0};
    std::atomic<std::uint64_t> alerts_sent_{0};
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};

}  // namespace corridor
