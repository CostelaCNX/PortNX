#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <borealis.hpp>
#include <switch.h>

#include <app/Config.hpp>
#include <download/DownloadManager.hpp>
#include <install/InstallManager.hpp>
#include <net/HttpClient.hpp>

namespace pinx::ui {

class BrowseTab : public brls::List {
    public:
        BrowseTab(pinx::app::Config *config,
                  pinx::download::DownloadManager *downloader,
                  pinx::install::InstallManager   *installer);
        ~BrowseTab() override;

        void reload();
        void frame(brls::FrameContext *ctx) override;

        // Public so free thread functions in the .cpp can reference them.
        struct IconResult {
            std::uint32_t        gen;
            std::string          url;
            std::vector<uint8_t> bytes;
        };
        struct IconState {
            std::atomic<std::uint32_t> gen{0};
            std::mutex                 mtx;
            std::vector<IconResult>    results;
        };
        struct CatalogFetch {
            std::string            url;
            bool                   push{true};
            pinx::net::HttpOptions opts;
            std::atomic<bool>      ready{false};
            bool                   success{false};
            std::string            body;
            std::string            error;
        };

    private:
        pinx::app::Config               *config;
        pinx::download::DownloadManager *downloader;
        pinx::install::InstallManager   *installer;
        std::vector<std::string>         nav_stack;

        // Deferred initial load — startFetch() is called on the first frame()
        // so threads start inside Application::mainLoop(), not the constructor.
        bool pending_initial_load_ = false;

        // Reload whenever a new install completes (tracks the completions counter).
        std::uint32_t            last_completions_ = 0;
        // URLs installed this session — used to badge ports without title_id.
        std::vector<std::string> session_installed_urls_;

        void startFetch(const std::string &url, bool push);
        void tickFetch();
        void showMessage(const std::string &title, const std::string &sub);

        // --- Async catalog fetch ---
        std::shared_ptr<CatalogFetch>    pending_fetch_;
        static constexpr std::size_t     kFetchStackSize = 4 * 1024 * 1024;
        Thread                           fetch_thread_{};
        alignas(4096) uint8_t            fetch_stack_[kFetchStackSize]{};
        bool                             fetch_thread_running_ = false;

        // --- Async icon loading ---
        std::shared_ptr<IconState>                        icon_state_;
        std::unordered_map<std::string, brls::ListItem *> icon_items_;
        pinx::net::HttpOptions                            icon_pending_opts_;
        bool                                              icon_pending_schedule_ = false;

        static constexpr std::size_t     kIconStackSize = 4 * 1024 * 1024;
        Thread                           icon_thread_{};
        alignas(4096) uint8_t            icon_stack_[kIconStackSize]{};
        bool                             icon_thread_running_ = false;

        void scheduleIcons();
        void tickIcons();
};

}
