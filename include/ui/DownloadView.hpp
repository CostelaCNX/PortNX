#pragma once

#include <chrono>
#include <cstdint>

#include <borealis.hpp>

#include <download/DownloadManager.hpp>
#include <install/InstallManager.hpp>

namespace pinx::ui {

class DownloadView : public brls::List {
    public:
        DownloadView(pinx::download::DownloadManager *dm,
                     pinx::install::InstallManager   *installer);

        void frame(brls::FrameContext *ctx) override;

    private:
        pinx::download::DownloadManager *dm;
        pinx::install::InstallManager   *installer;
        brls::ProgressDisplay           *bar         = nullptr;
        brls::ListItem                  *status      = nullptr;
        brls::ListItem                  *install_item = nullptr;

        std::chrono::steady_clock::time_point last_tick;
        std::uint64_t last_done  = 0;
        double        speed_bps  = 0.0;
        bool          install_enabled = false;
};

}
