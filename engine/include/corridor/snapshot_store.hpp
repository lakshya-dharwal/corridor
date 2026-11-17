// Hot-reloading holder for a data snapshot published as a file by an ingestion
// worker. Readers always get a complete, immutable snapshot; if a new file is
// missing or malformed, the last good one keeps being served.
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace corridor {

// Reads one archived generation. A missing file is an error: callers must not
// quietly fall back to the live snapshot, or a replay would not be reproducible.
template <typename Snapshot, typename Parser>
bool load_generation(const std::filesystem::path& file, Parser parser, Snapshot& out,
                     std::string& error) {
    std::ifstream in(file);
    if (!in) {
        error = "no snapshot " + file.string();
        return false;
    }
    return parser(in, out, error);
}

template <typename Snapshot>
class SnapshotStore {
public:
    // Fills `out` and returns true, or returns false with a message in `error`.
    using Parser = bool (*)(std::istream& in, Snapshot& out, std::string& error);

    SnapshotStore(std::string path, Parser parser) : path_(std::move(path)), parser_(parser) {}

    // Latest good snapshot, or null if none has ever loaded. Checks the file's
    // modification time at most once per second and reloads when it changes.
    std::shared_ptr<const Snapshot> current() {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        if (!checked_ || now - last_check_ >= std::chrono::seconds(1)) {
            checked_ = true;
            last_check_ = now;
            std::error_code ec;
            const auto mtime = std::filesystem::last_write_time(path_, ec);
            if (ec) {
                if (!snapshot_) last_error_ = "snapshot file not found";
            } else if (!have_mtime_ || mtime != last_mtime_) {
                last_mtime_ = mtime;
                have_mtime_ = true;
                load_locked();
            }
        }
        return snapshot_;
    }

    // Forced reload. On failure the previous snapshot stays in place.
    bool reload(std::string* error = nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::error_code ec;
        const auto mtime = std::filesystem::last_write_time(path_, ec);
        if (!ec) {
            last_mtime_ = mtime;
            have_mtime_ = true;
        }
        const bool ok = load_locked();
        if (!ok && error) *error = last_error_;
        return ok;
    }

    std::uint64_t reloads() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return reloads_;
    }

    std::uint64_t reload_failures() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return failures_;
    }

    std::string last_error() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_error_;
    }

private:
    bool load_locked() {
        std::ifstream file(path_);
        std::string error;
        Snapshot snapshot;
        if (!file) {
            error = "cannot open snapshot file";
        } else if (parser_(file, snapshot, error)) {
            snapshot_ = std::make_shared<const Snapshot>(std::move(snapshot));
            ++reloads_;
            last_error_.clear();
            return true;
        }
        ++failures_;
        last_error_ = error;
        return false;
    }

    const std::string path_;
    const Parser parser_;
    mutable std::mutex mutex_;
    std::shared_ptr<const Snapshot> snapshot_;
    std::filesystem::file_time_type last_mtime_{};
    bool have_mtime_ = false;
    bool checked_ = false;
    std::chrono::steady_clock::time_point last_check_{};
    std::uint64_t reloads_ = 0;
    std::uint64_t failures_ = 0;
    std::string last_error_;
};

}  // namespace corridor
