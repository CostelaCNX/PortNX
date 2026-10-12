#pragma once

#include <pu/Plutonium>

#include <ui/BrowseTab.hpp>

namespace pinx::ui {

class DetailsTab {
    public:
        void AddElementsTo(pu::ui::Layout *layout);
        void Show(const BrowseTab::GridEntry &entry, bool queued);
        void Hide();
        void RefreshStrings();

        bool IsVisible() const { return visible_; }
        bool CanQueue() const { return !queued_; }
        const BrowseTab::GridEntry &Entry() const { return entry_; }
        bool HitsAction(const pu::ui::TouchPoint &tp) const;

    private:
        static constexpr s32 kCoverX  = 120;
        static constexpr s32 kCoverY  = 150;
        static constexpr s32 kCoverSz = 480;
        static constexpr s32 kActionY = kCoverY + kCoverSz + 40;
        static constexpr s32 kActionH = 90;
        static constexpr s32 kTextX   = kCoverX + kCoverSz + 60;
        static constexpr s32 kTextW   = 1920 - kTextX - 120;

        BrowseTab::GridEntry entry_;
        bool visible_ = false;
        bool queued_  = false;

        pu::ui::elm::Rectangle::Ref cover_bg_;
        pu::ui::elm::Image::Ref     cover_img_;
        pu::ui::elm::TextBlock::Ref cover_fallback_;
        pu::ui::elm::Rectangle::Ref action_bg_;
        pu::ui::elm::TextBlock::Ref action_tb_;
        pu::ui::elm::TextBlock::Ref title_tb_;
        pu::ui::elm::TextBlock::Ref facts_tb_;
        pu::ui::elm::TextBlock::Ref desc_tb_;
        pu::ui::elm::TextBlock::Ref notes_tb_;
        pu::ui::elm::TextBlock::Ref hint_tb_;

        void Populate();
};

}
