#include "corridor/http.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <sstream>

namespace corridor {

namespace {

constexpr std::size_t kMaxHeadBytes = 8192;
constexpr int kReadTimeoutSeconds = 5;

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;  // Linux: no SIGPIPE on a closed peer
#else
constexpr int kSendFlags = 0;             // macOS uses SO_NOSIGPIPE instead
#endif

bool log_field(const std::string& text) {
    if (text.empty() || text.size() > 256) return false;
    for (unsigned char c : text) {
        if (c <= 32 || c >= 127) return false;
    }
    return true;
}

void log_request(const std::string& id, const std::string& method, const std::string& path,
                 int status, double seconds) {
    const auto milliseconds = static_cast<long long>(seconds * 1000.0);
    std::fprintf(stderr, "request id=%s method=%s path=%s status=%d duration_ms=%lld\n", id.c_str(),
                 log_field(method) ? method.c_str() : "-", log_field(path) ? path.c_str() : "-",
                 status, milliseconds);
}

const char* reason_phrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 426: return "Upgrade Required";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: return "Unknown";
    }
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

Response error_response(int status, const char* message) {
    Response response;
    response.status = status;
    response.body = std::string("{\"error\": \"") + message + "\"}\n";
    return response;
}

void send_all(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, kSendFlags);
        if (n <= 0) return;  // peer went away; nothing useful to do
        sent += static_cast<std::size_t>(n);
    }
}

}  // namespace

std::string url_decode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '+') {
            out += ' ';
        } else if (text[i] == '%' && i + 2 < text.size() &&
                   hex_value(text[i + 1]) >= 0 && hex_value(text[i + 2]) >= 0) {
            out += static_cast<char>(hex_value(text[i + 1]) * 16 + hex_value(text[i + 2]));
            i += 2;
        } else {
            out += text[i];
        }
    }
    return out;
}

std::map<std::string, std::string> parse_query(std::string_view query) {
    std::map<std::string, std::string> params;
    while (!query.empty()) {
        const std::size_t amp = query.find('&');
        const std::string_view pair = query.substr(0, amp);
        if (!pair.empty()) {
            const std::size_t eq = pair.find('=');
            const std::string key = url_decode(pair.substr(0, eq));
            const std::string value =
                eq == std::string_view::npos ? "" : url_decode(pair.substr(eq + 1));
            if (!key.empty()) params[key] = value;
        }
        if (amp == std::string_view::npos) break;
        query.remove_prefix(amp + 1);
    }
    return params;
}

bool parse_request_line(std::string_view head, Request& out) {
    const std::size_t line_end = head.find("\r\n");
    const std::string_view line = head.substr(0, line_end);
    const std::size_t sp1 = line.find(' ');
    if (sp1 == std::string_view::npos || sp1 == 0) return false;
    const std::size_t sp2 = line.find(' ', sp1 + 1);
    if (sp2 == std::string_view::npos || sp2 == sp1 + 1) return false;
    if (line.substr(sp2 + 1).rfind("HTTP/1.", 0) != 0) return false;

    const std::string_view target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    if (target.empty() || target[0] != '/') return false;
    const std::size_t question = target.find('?');

    out.method = std::string(line.substr(0, sp1));
    out.path = url_decode(target.substr(0, question));
    out.query = question == std::string_view::npos
                    ? std::map<std::string, std::string>{}
                    : parse_query(target.substr(question + 1));
    return true;
}

std::map<std::string, std::string> parse_headers(std::string_view head) {
    std::map<std::string, std::string> headers;
    std::size_t start = head.find("\r\n");  // skip the request line
    while (start != std::string_view::npos) {
        start += 2;
        const std::size_t end = head.find("\r\n", start);
        const std::string_view line = head.substr(start, end - start);
        if (line.empty()) break;  // blank line ends the head
        const std::size_t colon = line.find(':');
        if (colon != std::string_view::npos && colon > 0) {
            std::string name(line.substr(0, colon));
            for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            std::string_view value = line.substr(colon + 1);
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
                value.remove_prefix(1);
            }
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
                value.remove_suffix(1);
            }
            headers[name] = std::string(value);
        }
        start = end;
    }
    return headers;
}

std::string request_id_for(const std::string* header) {
    auto acceptable = [](const std::string& text) {
        if (text.empty() || text.size() > 64) return false;
        for (unsigned char c : text) {
            if (!(std::isalnum(c) || c == '-' || c == '_' || c == '.')) return false;
        }
        return true;
    };
    if (header && acceptable(*header)) return *header;
    static std::atomic<std::uint64_t> sequence{0};
    const auto ticks = static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
    std::ostringstream out;
    out << std::hex << ticks << "-" << sequence.fetch_add(1);
    return out.str();
}

bool wants_websocket(const Request& request) {
    const auto it = request.headers.find("upgrade");
    if (it == request.headers.end()) return false;
    std::string value = it->second;
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value == "websocket";
}

void HttpServer::get(std::string path, Handler handler) {
    routes_[std::move(path)] = std::move(handler);
}

void HttpServer::upgrade(std::string path, UpgradeHandler handler) {
    upgrades_[std::move(path)] = std::move(handler);
}

Response HttpServer::dispatch(const Request& request) const {
    const auto route = routes_.find(request.path);
    if (route == routes_.end()) return error_response(404, "no such route");
    if (request.method != "GET") return error_response(405, "only GET is supported");
    try {
        return route->second(request);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "handler error on %s: %s\n", request.path.c_str(), error.what());
        return error_response(500, "internal error");
    }
}

void HttpServer::record(int status, double seconds) {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    ++status_counts_[status];
    duration_sum_ += seconds;
    ++duration_count_;
}

std::string HttpServer::metrics_text() const {
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    std::ostringstream out;
    out << "# TYPE corridor_http_requests_total counter\n";
    for (const auto& [status, count] : status_counts_) {
        out << "corridor_http_requests_total{status=\"" << status << "\"} " << count << "\n";
    }
    out << "# TYPE corridor_http_request_duration_seconds summary\n";
    out << "corridor_http_request_duration_seconds_sum " << duration_sum_ << "\n";
    out << "corridor_http_request_duration_seconds_count " << duration_count_ << "\n";
    return out.str();
}

void HttpServer::handle_connection(int fd) {
    const auto started = std::chrono::steady_clock::now();

    timeval timeout{};
    timeout.tv_sec = kReadTimeoutSeconds;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
    const int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif

    std::string head;
    Response response;
    bool complete = false;
    char buffer[2048];
    while (head.size() < kMaxHeadBytes) {
        const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        head.append(buffer, static_cast<std::size_t>(n));
        if (head.find("\r\n\r\n") != std::string::npos) {
            complete = true;
            break;
        }
    }

    if (head.empty()) {  // client connected and sent nothing
        ::close(fd);
        return;
    }
    Request request;
    std::string id;
    if (!complete) {
        response = head.size() >= kMaxHeadBytes ? error_response(431, "request too large")
                                                : error_response(408, "request timed out");
        id = request_id_for(nullptr);
    } else if (!parse_request_line(head, request)) {
        response = error_response(400, "malformed request line");
        id = request_id_for(nullptr);
    } else {
        request.headers = parse_headers(head);
        const auto supplied = request.headers.find("x-request-id");
        id = request_id_for(supplied == request.headers.end() ? nullptr : &supplied->second);
        const auto upgrade = upgrades_.find(request.path);
        if (upgrade == upgrades_.end()) {
            response = dispatch(request);
        } else if (!wants_websocket(request)) {
            response = error_response(426, "this route requires a websocket upgrade");
        } else {
            std::optional<Response> refusal;
            try {
                refusal = upgrade->second(request, fd);
            } catch (const std::exception& error) {
                std::fprintf(stderr, "upgrade error on %s: %s\n", request.path.c_str(),
                             error.what());
                refusal = error_response(500, "internal error");
            }
            if (!refusal) {  // the handler owns the socket now; do not write or close
                const std::chrono::duration<double> elapsed =
                    std::chrono::steady_clock::now() - started;
                record(101, elapsed.count());
                log_request(id, request.method, request.path, 101, elapsed.count());
                return;
            }
            response = *refusal;
        }
    }

    std::string wire = "HTTP/1.1 " + std::to_string(response.status) + " " +
                       reason_phrase(response.status) + "\r\nContent-Type: " +
                       response.content_type + "\r\nContent-Length: " +
                       std::to_string(response.body.size()) + "\r\nX-Request-Id: " + id +
                       "\r\nConnection: close\r\n\r\n";
    wire += response.body;
    send_all(fd, wire);
    ::close(fd);

    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started;
    record(response.status, elapsed.count());
    log_request(id, request.method, request.path, response.status, elapsed.count());
}

void HttpServer::worker_loop() {
    for (;;) {
        int fd = -1;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [&] { return !pending_.empty() || stopping_.load(); });
            if (pending_.empty()) return;  // stopping and drained
            fd = pending_.front();
            pending_.pop();
        }
        handle_connection(fd);
    }
}

bool HttpServer::listen(const std::string& host, std::uint16_t port, int workers) {
    const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) return false;

    const int on = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1 ||
        ::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        ::listen(listener, 128) != 0) {
        ::close(listener);
        return false;
    }

    std::vector<std::thread> pool;
    for (int i = 0; i < workers; ++i) pool.emplace_back([this] { worker_loop(); });

    // Poll with a short timeout so a stop() from a signal handler is noticed.
    while (!stopping_.load()) {
        pollfd waiting{listener, POLLIN, 0};
        if (::poll(&waiting, 1, 200) <= 0) continue;
        const int fd = ::accept(listener, nullptr, nullptr);
        if (fd < 0) continue;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            pending_.push(fd);
        }
        queue_cv_.notify_one();
    }

    ::close(listener);
    queue_cv_.notify_all();
    for (std::thread& thread : pool) thread.join();  // workers finish queued requests first
    return true;
}

}  // namespace corridor
