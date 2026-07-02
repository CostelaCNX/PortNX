#pragma once

#include <functional>

#include <pu/Plutonium>

#include <app/Config.hpp>

namespace pinx::ui {

class SettingsTab {
    public:
        SettingsTab(pinx::app::Config *config,
                    std::function<void()> on_url_changed,
                    std::function<void()> on_lang_changed);

        void AddElementsTo(pu::ui::Layout *layout);
        void Show();
        void Hide();
        void Poll();
        void RefreshStrings();

    private:
        pinx::app::Config    *config_;
        std::function<void()> on_url_changed_;
        std::function<void()> on_lang_changed_;
        bool                  pending_rebuild_      = false;
        bool                  pending_lang_refresh_ = false;
        bool                  visible_              = false;
        int                   input_guard_          = 0;

        pu::ui::elm::Menu::Ref menu_;

        pu::ui::elm::MenuItem::Ref lang_item_;
        pu::ui::elm::MenuItem::Ref url_item_;
        pu::ui::elm::MenuItem::Ref storage_item_;
        pu::ui::elm::MenuItem::Ref reinstall_item_;

        void UpdateItemLabels();

        static constexpr pu::ui::Color kItemClr   = {  28,  34,  44, 255 };
        static constexpr pu::ui::Color kFocusClr  = {  40,  56,  80, 255 };
        static constexpr pu::ui::Color kTextClr   = { 230, 237, 243, 255 };
        static constexpr s32           kItemH      = 88;
        static constexpr u32           kItemN      = 10;
};

} // namespace pinx::ui
