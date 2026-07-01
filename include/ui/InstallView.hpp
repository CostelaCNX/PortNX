#pragma once

#include <borealis.hpp>

#include <install/InstallManager.hpp>

namespace pinx::ui {

class InstallView : public brls::List {
    public:
        explicit InstallView(pinx::install::InstallManager *mgr);

        void frame(brls::FrameContext *ctx) override;

    private:
        pinx::install::InstallManager *mgr;
        brls::ProgressDisplay         *bar         = nullptr;
        brls::ListItem                *title_item = nullptr;
        brls::ListItem                *info_item  = nullptr;
        bool                           name_set_  = false;
};

}
