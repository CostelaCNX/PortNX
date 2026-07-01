#pragma once

#include <functional>

#include <borealis.hpp>

#include <app/Config.hpp>

namespace pinx::ui {

// Settings tab: edit the index server URL and install options.
// Notifies |on_changed| when the URL changes so the Browse tab can reload.
class SettingsTab : public brls::List {
    public:
        SettingsTab(pinx::app::Config *config, std::function<void()> on_changed);

        void frame(brls::FrameContext *ctx) override;

    private:
        pinx::app::Config    *config;
        std::function<void()> on_changed;
        bool                  pending_rebuild_ = false;

        void build();
};

}
