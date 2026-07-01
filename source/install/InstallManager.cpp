#include <install/InstallManager.hpp>
#include <install/InstallEngine.hpp>
#include <install/StreamInstallEngine.hpp>

namespace pinx::install {

InstallManager::~InstallManager() {
    shutdown();
}

bool InstallManager::enqueueStream(const StreamRequest &req) {
    bool needs_start = false;
    StreamRequest first;

    {
        std::lock_guard<std::mutex> lk(str_mtx);
        job_queue_.push_back(req);
        if(!busy.load()) {
            busy.store(true);
            first = std::move(job_queue_.front());
            job_queue_.pop_front();
            display_name = first.display_name;
            error.clear();
            needs_start = true;
        }
    }

    if(needs_start) {
        cancel_flag.store(false);
        state.store(State::Running);
        bytes_done.store(0);
        bytes_total.store(0);
        decompressing.store(false);
        title_id.store(0);
        if(worker.joinable()) worker.join();
        worker = std::thread([this, first = std::move(first)]() mutable {
            runStream(std::move(first));
        });
    }
    return true;
}

bool InstallManager::startStream(const StreamRequest &req) {
    if(busy.exchange(true)) return false;

    cancel_flag.store(false);
    state.store(State::Running);
    bytes_done.store(0);
    bytes_total.store(0);
    decompressing.store(false);
    title_id.store(0);
    {
        std::lock_guard<std::mutex> lk(str_mtx);
        display_name = req.display_name;
        error.clear();
    }

    if(worker.joinable()) worker.join();
    worker = std::thread([this, req] { runStream(req); });
    return true;
}

bool InstallManager::start(const Request &req) {
    if(busy.exchange(true)) return false;

    cancel_flag.store(false);
    state.store(State::Running);
    bytes_done.store(0);
    bytes_total.store(0);
    decompressing.store(false);
    title_id.store(0);
    {
        std::lock_guard<std::mutex> lk(str_mtx);
        display_name.clear();
        error.clear();
    }

    if(worker.joinable()) worker.join();
    worker = std::thread([this, req] { run(req); });
    return true;
}

void InstallManager::cancel() {
    {
        std::lock_guard<std::mutex> lk(str_mtx);
        job_queue_.clear();
    }
    cancel_flag.store(true);
}

void InstallManager::shutdown() {
    {
        std::lock_guard<std::mutex> lk(str_mtx);
        job_queue_.clear();
    }
    cancel_flag.store(true);
    if(worker.joinable()) worker.join();
}

InstallManager::Snapshot InstallManager::snapshot() const {
    Snapshot s;
    s.state         = state.load();
    s.bytes_done    = bytes_done.load();
    s.bytes_total   = bytes_total.load();
    s.decompressing = decompressing.load();
    s.title_id      = title_id.load();
    s.completions   = completions.load();
    std::lock_guard<std::mutex> lk(str_mtx);
    s.display_name    = display_name;
    s.error           = error;
    s.completed_names = completed_names_;
    s.installed_urls  = installed_urls_;
    for(const auto &j : job_queue_)
        s.queue_names.push_back(j.display_name);
    return s;
}

void InstallManager::runStream(StreamRequest req) {
    while(true) {
        StreamInstallRequest sir;
        sir.url            = req.url;
        sir.http_opts      = req.http_opts;
        sir.install_config = req.install_config;

        const InstallResult result = StreamInstallFromUrl(sir,
            [this](const InstallProgress &p) {
                bytes_done.store(p.bytes_done);
                bytes_total.store(p.bytes_total);
                decompressing.store(p.decompressing);
            },
            [this] { return cancel_flag.load(); });

        if(result.success) {
            title_id.store(result.title_id);
            {
                std::lock_guard<std::mutex> lk(str_mtx);
                completed_names_.push_back(req.display_name);
                installed_urls_.push_back(req.url);
            }
            completions.fetch_add(1, std::memory_order_relaxed);
            state.store(State::Done);
        } else {
            {
                std::lock_guard<std::mutex> lk(str_mtx);
                error = result.error_message;
            }
            state.store(State::Failed);
        }

        StreamRequest next;
        bool has_next = false;
        {
            std::lock_guard<std::mutex> lk(str_mtx);
            if(!job_queue_.empty() && !cancel_flag.load()) {
                next = std::move(job_queue_.front());
                job_queue_.pop_front();
                display_name = next.display_name;
                error.clear();
                has_next = true;
            } else {
                busy.store(false);
            }
        }

        if(!has_next) break;

        state.store(State::Running);
        bytes_done.store(0);
        bytes_total.store(0);
        decompressing.store(false);
        title_id.store(0);
        req = std::move(next);
    }
}

void InstallManager::run(Request req) {
    InstallConfig config;
    config.dest_storage_id = NcmStorageId_SdCard;
    config.verify_nca_sigs = true;
    config.allow_unsigned  = true;
    config.ignore_req_fw   = true;
    config.reinstall_ncas  = false;

    InstallResult result = InstallFromLocalFile(req.file_path, config,
        [this](const InstallProgress &p) {
            bytes_done.store(p.bytes_done);
            bytes_total.store(p.bytes_total);
            decompressing.store(p.decompressing);
        });

    if(result.success) {
        title_id.store(result.title_id);
        {
            std::lock_guard<std::mutex> lk(str_mtx);
            completed_names_.push_back(req.file_path);
        }
        completions.fetch_add(1, std::memory_order_relaxed);
        state.store(State::Done);
    } else {
        std::lock_guard<std::mutex> lk(str_mtx);
        error = result.error_message;
        state.store(State::Failed);
    }
    busy.store(false);
}

}
