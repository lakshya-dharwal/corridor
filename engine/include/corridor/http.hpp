// Minimal HTTP/1.1 server on POSIX sockets: GET routes, a fixed worker pool,
// one request per connection, request metrics. No third-party dependencies.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace corridor {

struct Request {
    std::string method;
    std::string path;
    std::map<std::string, std::string> query;
    std::map<std::string, std::string> headers;  // names lowercased
};

struct Response {
    int status = 200;
    std::string content_type = "application/json";
    std::string body;
};

// Parsing helpers, exposed so they can be tested without a socket.
std::string url_decode(std::string_view text);
std::map<std::string, std::string> parse_query(std::string_view query);
// Parses the request line ("GET /path?x=1 HTTP/1.1"). False if malformed.
bool parse_request_line(std::string_view head, Request& out);
// Header lines after the request line, names lowercased, values trimmed.
std::map<std::string, std::string> parse_headers(std::string_view head);
// True if the request asks to upgrade the connection to WebSocket.
bool wants_websocket(const Request& request);

class HttpServer {
public:
    using Handler = std::function<Response(const Request&)>;

    // Takes over a connection that asked to upgrade (e.g. to WebSocket). Return
    // nullopt after taking ownership of `fd`: the handler, or whoever it hands
    // the socket to, then writes the upgrade response and closes it. Return a
    // Response to refuse; the server sends it and closes the socket as usual.
    using UpgradeHandler = std::function<std::optional<Response>(const Request&, int fd)>;

    void get(std::string path, Handler handler);
    void upgrade(std::string path, UpgradeHandler handler);

    // Blocks until stop() is called. Returns false if the socket could not be
    // opened or bound.
    bool listen(const std::string& host, std::uint16_t port, int workers = 4);

    // Safe to call from a signal handler.
    void stop() { stopping_.store(true); }

    // Request counters in Prometheus text format.
    std::string metrics_text() const;

    // Routes a parsed request. Public so tests can exercise routing directly.
    Response dispatch(const Request& request) const;

private:
    void worker_loop();
    void handle_connection(int fd);
    void record(int status, double seconds);

    std::map<std::string, Handler> routes_;
    std::map<std::string, UpgradeHandler> upgrades_;
    std::atomic<bool> stopping_{false};

    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<int> pending_;

    mutable std::mutex metrics_mutex_;
    std::map<int, std::uint64_t> status_counts_;
    double duration_sum_ = 0.0;
    std::uint64_t duration_count_ = 0;
};

}  // namespace corridor
