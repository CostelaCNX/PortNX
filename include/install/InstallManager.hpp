#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <install/InstallEngine.hpp>
#include <net/HttpClient.hpp>

namespace pinx::install {

class InstallManager {
    public:
        enum class State { Idle, Running, Done, Failed };

        struct Request {
            std::string file_path;
        };

        struct StreamRequest {
            std::string      url;
            std::string      display_name;
            net::HttpOptions http_opts;
            InstallConfig    install_config;
        };

        struct Snapshot {
            State         state         = State::Idle;
            std::uint64_t bytes_done    = 0;
            std::uint64_t bytes_total   = 0;
            bool          decompressing = false;
            std::string   display_name;
            std::string   error;
            std::uint64_t title_id      = 0;
            std::uint32_t completions   = 0;   // increments after each successful install

            // URL of the job currently being processed (empty when idle).
            std::string              active_url;
            // Names and URLs of pending (not yet started) jobs.
            std::vector<std::string> queue_names;
            std::vector<std::string> queue_urls;
            // Names of successfully installed ports (this session).
            std::vector<std::string> completed_names;
            // URLs of successfully installed ports (this session).
            std::vector<std::string> installed_urls;
        };

        InstallManager() = default;
        ~InstallManager();

        bool start(const Request &req);
        bool startStream(const StreamRequest &req);
        bool enqueueStream(const StreamRequest &req);
        void cancel();
        void shutdown();
        Snapshot snapshot() const;

    private:
        void run(Request req);
        void runStream(StreamRequest req);

        std::thread                worker;
        std::atomic<bool>          cancel_flag{false};
        std::atomic<bool>          busy{false};
        std::atomic<State>         state{State::Idle};
        std::atomic<std::uint64_t> bytes_done{0};
        std::atomic<std::uint64_t> bytes_total{0};
        std::atomic<bool>          decompressing{false};
        std::atomic<std::uint64_t> title_id{0};
        std::atomic<std::uint32_t> completions{0};

        mutable std::mutex              str_mtx;
        std::string                     display_name;
        std::string                     active_url_;
        std::string                     error;
        std::deque<StreamRequest>       job_queue_;
        std::vector<std::string>        completed_names_;
        std::vector<std::string>        installed_urls_;
};

}
