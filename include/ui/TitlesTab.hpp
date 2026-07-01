#pragma once

#include <atomic>
#include <thread>
#include <vector>

#include <borealis.hpp>

#include <install/TitleManager.hpp>

namespace pinx::ui {

class TitlesTab : public brls::List {
    public:
        TitlesTab();
        ~TitlesTab();

        void refresh();
        void frame(brls::FrameContext *ctx) override;

    private:
        void populate(const std::vector<pinx::install::InstalledTitle> &titles);
        void showUninstallDialog(const pinx::install::InstalledTitle &title);

        bool pending_refresh_ = false;

        // Background uninstall thread.
        std::thread        uninstall_thread_;
        std::atomic<bool>  uninstalling_{false};
        std::atomic<bool>  uninstall_done_{false};
        std::atomic<bool>  uninstall_ok_{false};
};

}
