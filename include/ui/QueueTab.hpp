#pragma once

#include <string>
#include <vector>

#include <pu/Plutonium>

#include <download/DownloadManager.hpp>
#include <install/InstallManager.hpp>

namespace pinx::ui {

class QueueTab {
    public:
        QueueTab(pinx::download::DownloadManager *dl,
                 pinx::install::InstallManager   *inst);

        void AddElementsTo(pu::ui::Layout *layout);
        void Show();
        void Hide();
        void Poll();
        void RefreshStrings();
        void CancelCurrent();
        bool IsActive() const;

        static constexpr s32 kCancelAbsX = 90;
        static constexpr s32 kCancelAbsY = 660;   // kCY(100) + kCancelY(560)
        static constexpr s32 kCancelH    = 60;

    private:
        pinx::download::DownloadManager *downloader_;
        pinx::install::InstallManager   *installer_;
        bool                             visible_ = false;

        enum class Phase { Idle, Downloading, Installing, Done, Failed };
        Phase phase_ = Phase::Idle;

        std::string              display_name_;
        float                    progress_     = 0.0f;
        std::string              status_text_;
        std::vector<std::string> queue_names_;
        std::vector<std::string> completed_names_;

        static constexpr s32 kPadX = 40;
        static constexpr s32 kPadY = 32;

        pu::ui::elm::TextBlock::Ref   completed_text_;
        pu::ui::elm::TextBlock::Ref   display_name_text_;
        pu::ui::elm::ProgressBar::Ref progress_bar_;
        pu::ui::elm::TextBlock::Ref   status_text_elm_;
        pu::ui::elm::TextBlock::Ref   queue_text_;
        pu::ui::elm::TextBlock::Ref   cancel_hint_;

        void UpdateElements();
        static std::string FormatSize(std::uint64_t bytes);
        void SetText(pu::ui::elm::TextBlock::Ref &tb, const std::string &text);
};

} // namespace pinx::ui
