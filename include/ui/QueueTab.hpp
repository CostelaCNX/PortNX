#pragma once

#include <borealis.hpp>

#include <download/DownloadManager.hpp>
#include <install/InstallManager.hpp>

#include <string>
#include <vector>

namespace pinx::ui {

// Persistent tab that shows session install history, the active job, and
// the pending queue. Custom NVG draw — no list items, no separator lines.
class QueueTab : public brls::View {
    public:
        QueueTab(pinx::download::DownloadManager *dl,
                 pinx::install::InstallManager   *inst);

        void draw(NVGcontext *vg, int x, int y, unsigned w, unsigned h,
                  brls::Style *style, brls::FrameContext *ctx) override;

        void layout(NVGcontext *vg, brls::Style *style,
                    brls::FontStash *stash) override {}

        void frame(brls::FrameContext *ctx) override;

    private:
        pinx::download::DownloadManager *downloader_;
        pinx::install::InstallManager   *installer_;

        enum class Phase { Idle, Downloading, Installing, Done, Failed };
        Phase phase_ = Phase::Idle;

        std::string              display_name_;
        float                    progress_     = 0.0f;
        std::string              status_text_;
        std::vector<std::string> queue_names_;      // pending jobs
        std::vector<std::string> completed_names_;  // installed this session
};

} // namespace pinx::ui
