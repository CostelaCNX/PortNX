#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <pu/Plutonium>
#include <switch.h>

#include <app/Config.hpp>
#include <download/DownloadManager.hpp>
#include <install/InstallManager.hpp>
#include <net/HttpClient.hpp>

namespace pinx::ui {

class BrowseTab {
    public:
        BrowseTab(pinx::app::Config *config,
                  pinx::download::DownloadManager *downloader,
                  pinx::install::InstallManager   *installer);
        ~BrowseTab();

        void AddElementsTo(pu::ui::Layout *layout);
        void Show();
        void Hide();
        void Poll();
        void reload();

        bool HandleInput(u64 kd);
        void HandleTouch(const pu::ui::TouchPoint &tp);
        void TouchCell(s32 slot);
        void RefreshStrings();

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

        // Grid entry — one item in the catalog
        struct GridEntry {
            std::string name, url, icon_url, filename, format, version, sha256;
            std::uint64_t size = 0, title_id = 0;
            bool size_authoritative = false;
            bool is_installable = false;
            bool is_directory   = false;
            bool is_back        = false;
            std::string meta_line;
            pu::sdl2::TextureHandle::Ref icon_tex; // null until loaded
        };

        enum class ViewMode { Grid, List };

    private:
        pinx::app::Config               *config;
        pinx::download::DownloadManager *downloader;
        pinx::install::InstallManager   *installer;
        std::vector<std::string>         nav_stack;
        bool                             visible_ = false;

        bool pending_initial_load_ = false;

        // Grid constants (3×2, 6 items/page)
        static constexpr s32 kGridCols  = 3;
        static constexpr s32 kGridRows  = 2;
        static constexpr s32 kGridPage  = kGridCols * kGridRows; // 6
        static constexpr s32 kCellW     = 560;
        static constexpr s32 kCellH     = 420;
        static constexpr s32 kIconSz    = 256;
        static constexpr s32 kGridX     = 120;
        static constexpr s32 kGridY     = 130;

        // List constants (8 items/page, full-width rows)
        static constexpr s32 kListRows   = 8;
        static constexpr s32 kListX      = 120;
        static constexpr s32 kListY      = 108;
        static constexpr s32 kListW      = 1680;
        static constexpr s32 kListH      = 96;
        static constexpr s32 kListIconSz = 72;

        static constexpr s32 kMaxCells  = kListRows; // larger of the two page sizes

        ViewMode view_mode_ = ViewMode::Grid;
        s32 PageSize() const {
            return view_mode_ == ViewMode::Grid ? kGridPage : kListRows;
        }

        std::vector<GridEntry>          entries_;
        s32                             grid_page_ = 0;
        s32                             grid_sel_  = 0;
        bool                            is_loading_ = false;
        std::unordered_set<std::string> queued_urls_;
        s32                             sync_frame_ = 0;
        void SyncQueuedUrls();

        // Per-cell UI elements (kMaxCells = 8)
        std::array<pu::ui::elm::Rectangle::Ref, kMaxCells> cell_bg_;
        std::array<pu::ui::elm::Image::Ref,     kMaxCells> cell_img_;
        std::array<pu::ui::elm::TextBlock::Ref, kMaxCells> cell_lbl_;
        std::array<pu::ui::elm::TextBlock::Ref, kMaxCells> cell_meta_;

        // Status / page info
        pu::ui::elm::TextBlock::Ref status_tb_;
        pu::ui::elm::TextBlock::Ref page_tb_;
        pu::ui::elm::TextBlock::Ref browse_hint_tb_;

        // Toast notification (green, expires after ~3s)
        pu::ui::elm::Rectangle::Ref toast_bg_;
        pu::ui::elm::TextBlock::Ref toast_tb_;
        s32                         toast_countdown_ = 0;
        void ShowToast(const std::string &name);

        void UpdateGridCells();
        void ActivateSelected();

        void startFetch(const std::string &url, bool push);
        void tickFetch();
        void showMessage(const std::string &msg);

        // --- Async catalog fetch ---
        std::shared_ptr<CatalogFetch>    pending_fetch_;
        static constexpr std::size_t     kFetchStackSize = 4 * 1024 * 1024;
        Thread                           fetch_thread_{};
        alignas(4096) uint8_t            fetch_stack_[kFetchStackSize]{};
        bool                             fetch_thread_running_ = false;

        // --- Async icon loading ---
        std::shared_ptr<IconState>                      icon_state_;
        std::unordered_map<std::string, std::size_t>   icon_items_; // url -> entry index
        pinx::net::HttpOptions                          icon_pending_opts_;
        bool                                            icon_pending_schedule_ = false;

        static constexpr std::size_t     kIconStackSize = 4 * 1024 * 1024;
        Thread                           icon_thread_{};
        alignas(4096) uint8_t            icon_stack_[kIconStackSize]{};
        bool                             icon_thread_running_ = false;

        void scheduleIcons();
        void tickIcons();
};

} // namespace pinx::ui
