#include <download/DownloadManager.hpp>

#include <cstdio>
#include <filesystem>

#include <download/FsGuard.hpp>
#include <download/Integrity.hpp>
#include <net/HttpClient.hpp>

namespace pinx::download {

DownloadManager::~DownloadManager() {
    shutdown();
}

bool DownloadManager::start(const Request &req) {
    bool expected = false;
    if(!busy.compare_exchange_strong(expected, true)) {
        return false;
    }

    if(worker.joinable()) {
        worker.join();
    }

    cancel_flag.store(false);
    state.store(State::Running);
    done.store(0);
    total.store(req.expected_size);
    {
        std::lock_guard<std::mutex> lock(str_mtx);
        name = req.name;
        error.clear();
        result_path.clear();
    }

    worker = std::thread(&DownloadManager::run, this, req);
    return true;
}

void DownloadManager::cancel() {
    cancel_flag.store(true);
}

void DownloadManager::shutdown() {
    cancel_flag.store(true);
    if(worker.joinable()) {
        worker.join();
    }
}

DownloadManager::Snapshot DownloadManager::snapshot() const {
    Snapshot s;
    s.state = state.load();
    s.done  = done.load();
    s.total = total.load();
    std::lock_guard<std::mutex> lock(str_mtx);
    s.name        = name;
    s.error       = error;
    s.result_path = result_path;
    return s;
}

void DownloadManager::run(Request req) {
    const std::string part = req.dest_path + ".part";

    auto finish_fail = [&](const std::string &msg, bool purge) {
        if(purge) {
            std::remove(part.c_str());
        }
        {
            std::lock_guard<std::mutex> lock(str_mtx);
            error = msg;
        }
        state.store(State::Failed);
        busy.store(false);
    };

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(req.dest_path).parent_path(), ec);

    if(req.expected_size > 0) {
        const std::uint64_t freeb = FreeBytes("sdmc:/");
        if(freeb > 0 && freeb < req.expected_size) {
            finish_fail("not enough free space on the SD card", true);
            return;
        }
    }

    net::HttpOptions opts;
    opts.verify_tls         = req.verify_tls;
    opts.username           = req.username;
    opts.password           = req.password;
    opts.connect_timeout_ms = 15000;

    const net::DownloadResult dr = net::Download(
        req.url, part, opts,
        [this](const net::DownloadProgress &p) {
            done.store(p.downloaded);
            if(p.total > 0) {
                total.store(p.total);
            }
        },
        [this]() { return cancel_flag.load(); });

    if(cancel_flag.load()) {
        state.store(State::Canceled);
        busy.store(false);
        return;
    }
    if(!dr.success) {
        finish_fail(dr.error.empty() ? "download failed" : dr.error, false);
        return;
    }

    state.store(State::Verifying);
    if(!req.expected_sha256.empty()) {
        const std::string got = Sha256File(part);
        if(got.empty() || got != req.expected_sha256) {
            finish_fail("integrity check failed (sha256 mismatch)", true);
            return;
        }
    }
    else if(req.expected_size > 0 && dr.bytes != req.expected_size) {
        finish_fail("size mismatch (expected " + std::to_string(req.expected_size) +
                    ", got " + std::to_string(dr.bytes) + ")", true);
        return;
    }

    std::remove(req.dest_path.c_str());
    std::filesystem::rename(part, req.dest_path, ec);
    if(ec) {
        finish_fail("could not finalize file: " + ec.message(), false);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(str_mtx);
        result_path = req.dest_path;
    }
    state.store(State::Done);
    busy.store(false);
}

}
