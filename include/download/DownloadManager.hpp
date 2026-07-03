#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace pinx::download {

// Runs one download at a time on a worker thread: free-space guard, streaming
// download to a .part file with progress/cancel, integrity check (sha256 or
// size), then atomic rename to the final path. The UI polls snapshot() from
// its frame loop — all cross-thread state is atomic or mutex-guarded.
//
// A single worker per download is fine for M3 (one at a time); a persistent
// worker + queue (reusing one curl handle) is the M5 refactor.
class DownloadManager {
    public:
        enum class State { Idle, Running, Verifying, Done, Failed, Canceled };

        struct Request {
            std::string   url;
            std::string   dest_path;        // final path; ".part" is appended while downloading
            std::string   name;             // display name
            std::string   expected_sha256;  // optional integrity hash (lowercase hex)
            std::uint64_t expected_size = 0;
            bool          verify_tls = false;
            std::string   username;
            std::string   password;
        };

        struct Snapshot {
            State         state = State::Idle;
            std::uint64_t done  = 0;
            std::uint64_t total = 0;
            std::string   name;
            std::string   error;
            std::string   result_path;
            std::string   active_url;
        };

        DownloadManager() = default;
        ~DownloadManager();

        bool start(const Request &req);  // false if a download is already running
        void cancel();
        void shutdown();                 // cancel + join (call before app exit)
        Snapshot snapshot() const;

    private:
        void run(Request req);

        std::thread                 worker;
        std::atomic<bool>           cancel_flag{false};
        std::atomic<bool>           busy{false};
        std::atomic<State>          state{State::Idle};
        std::atomic<std::uint64_t>  done{0};
        std::atomic<std::uint64_t>  total{0};

        mutable std::mutex str_mtx;
        std::string        name;
        std::string        error;
        std::string        result_path;
        std::string        active_url_;
};

}
