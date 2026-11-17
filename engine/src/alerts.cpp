#include "corridor/alerts.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <ctime>
#include <sstream>

#include "corridor/briefing.hpp"
#include "corridor/websocket.hpp"

namespace corridor {

namespace {

constexpr int kTickMs = 250;
constexpr auto kFullRecheck = std::chrono::seconds(60);  // staleness is time-driven
constexpr auto kPingInterval = std::chrono::seconds(30);
constexpr std::size_t kMaxInboundBytes = 2 * kMaxWsPayload;

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;  // macOS: SO_NOSIGPIPE was set when the socket was accepted
#endif

bool send_all(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, kSendFlags);
        if (n <= 0) return false;  // closed, or blocked past the send timeout
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

std::string message_json(const char* type, const RouteStatus& status,
                         const std::vector<std::string>& changes) {
    std::ostringstream out;
    out << "{\"type\": \"" << type << "\", \"at\": " << static_cast<std::int64_t>(std::time(nullptr));
    if (!changes.empty()) {
        out << ", \"changes\": [";
        for (std::size_t i = 0; i < changes.size(); ++i) {
            out << (i ? ", " : "") << '"' << json_escape(changes[i]) << '"';
        }
        out << "]";
    }
    out << ", \"status\": " << route_status_json(status) << "}";
    return out.str();
}

}  // namespace

AlertHub::AlertHub(StatusFn status, VersionFn version, std::size_t max_connections)
    : status_(std::move(status)), version_(std::move(version)),
      max_connections_(max_connections) {}

AlertHub::~AlertHub() { stop(); }

void AlertHub::start() {
    if (thread_.joinable()) return;
    stopping_.store(false);
    thread_ = std::thread([this] { run(); });
}

void AlertHub::stop() {
    stopping_.store(true);
    if (thread_.joinable()) thread_.join();
}

bool AlertHub::add(int fd, Request request, RouteStatus initial, std::string handshake) {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    if (stopping_.load() || connection_count_.load() + pending_.size() >= max_connections_) {
        return false;
    }
    Connection connection;
    connection.fd = fd;
    connection.request = std::move(request);
    connection.last = std::move(initial);
    connection.handshake = std::move(handshake);
    pending_.push_back(std::move(connection));
    return true;
}

std::string AlertHub::metrics_text() const {
    std::ostringstream out;
    out << "# TYPE corridor_alert_connections gauge\n"
        << "corridor_alert_connections " << connections() << "\n"
        << "# TYPE corridor_alerts_sent_total counter\n"
        << "corridor_alerts_sent_total " << alerts_sent() << "\n";
    return out.str();
}

void AlertHub::drop(std::size_t index) {
    ::close(connections_[index].fd);
    connections_.erase(connections_.begin() + static_cast<std::ptrdiff_t>(index));
    connection_count_.store(connections_.size());
}

void AlertHub::adopt_pending() {
    std::vector<Connection> adopted;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        adopted.swap(pending_);
    }
    for (Connection& connection : adopted) {
        // A client that stops reading must not stall alerts for everyone else.
        timeval timeout{};
        timeout.tv_sec = 2;
        ::setsockopt(connection.fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        const std::string hello =
            connection.handshake +
            encode_ws_frame(WsOpcode::Text, message_json("status", connection.last, {}));
        if (!send_all(connection.fd, hello)) {
            ::close(connection.fd);
            continue;
        }
        connection.handshake.clear();
        connections_.push_back(std::move(connection));
    }
    connection_count_.store(connections_.size());
}

void AlertHub::read_inbound(int timeout_ms) {
    if (connections_.empty()) {
        ::poll(nullptr, 0, timeout_ms);  // nothing to watch; just wait out the tick
        return;
    }
    std::vector<pollfd> watched;
    watched.reserve(connections_.size());
    for (const Connection& connection : connections_) {
        watched.push_back({connection.fd, POLLIN, 0});
    }
    if (::poll(watched.data(), static_cast<nfds_t>(watched.size()), timeout_ms) <= 0) return;

    // Walk backwards so dropping a connection does not shift the ones still to visit.
    for (std::size_t i = connections_.size(); i-- > 0;) {
        if (!(watched[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
        Connection& connection = connections_[i];

        char buffer[4096];
        const ssize_t n = ::recv(connection.fd, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
        if (n <= 0) {  // peer closed or errored
            drop(i);
            continue;
        }
        connection.inbound.append(buffer, static_cast<std::size_t>(n));

        bool alive = connection.inbound.size() <= kMaxInboundBytes;
        while (alive) {
            WsFrame frame;
            const WsDecode result = decode_ws_frame(connection.inbound, frame);
            if (result == WsDecode::Incomplete) break;
            if (result == WsDecode::Error) {
                send_all(connection.fd, encode_ws_frame(WsOpcode::Close, "\x03\xEA"));  // 1002
                alive = false;
            } else if (frame.opcode == WsOpcode::Close) {
                send_all(connection.fd, encode_ws_frame(WsOpcode::Close, frame.payload));
                alive = false;
            } else if (frame.opcode == WsOpcode::Ping) {
                alive = send_all(connection.fd, encode_ws_frame(WsOpcode::Pong, frame.payload));
            }
            // Text, binary and pong frames from the client carry nothing we act on.
        }
        if (!alive) drop(i);
    }
}

void AlertHub::reevaluate_all() {
    for (std::size_t i = connections_.size(); i-- > 0;) {
        Connection& connection = connections_[i];
        const std::optional<RouteStatus> status = status_(connection.request);
        if (!status) continue;  // e.g. an airport left the feed; keep the last known status
        const std::vector<std::string> changes = diff_route_status(connection.last, *status);
        connection.last = *status;
        if (changes.empty()) continue;
        if (send_all(connection.fd,
                     encode_ws_frame(WsOpcode::Text, message_json("alert", *status, changes)))) {
            alerts_sent_.fetch_add(1);
        } else {
            drop(i);
        }
    }
}

void AlertHub::ping_all() {
    const std::string ping = encode_ws_frame(WsOpcode::Ping, "");
    for (std::size_t i = connections_.size(); i-- > 0;) {
        if (!send_all(connections_[i].fd, ping)) drop(i);  // finds clients that vanished
    }
}

void AlertHub::run() {
    std::uint64_t seen_version = version_();
    auto last_full = std::chrono::steady_clock::now();
    auto last_ping = last_full;

    while (!stopping_.load()) {
        adopt_pending();
        read_inbound(kTickMs);

        const auto now = std::chrono::steady_clock::now();
        const std::uint64_t version = version_();
        if (version != seen_version || now - last_full >= kFullRecheck) {
            seen_version = version;
            last_full = now;
            reevaluate_all();
        }
        if (now - last_ping >= kPingInterval) {
            last_ping = now;
            ping_all();
        }
    }

    adopt_pending();  // do not leak sockets queued during shutdown
    const std::string going_away = encode_ws_frame(WsOpcode::Close, "\x03\xE9");  // 1001
    for (const Connection& connection : connections_) {
        send_all(connection.fd, going_away);
        ::close(connection.fd);
    }
    connections_.clear();
    connection_count_.store(0);
}

}  // namespace corridor
