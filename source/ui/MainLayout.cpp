#include <ui/MainLayout.hpp>
#include <ui/MainApplication.hpp>
#include <app/I18n.hpp>

namespace pinx::ui {

// Pure black + SNES Super Famicom button palette
static constexpr pu::ui::Color kBlack    = {   0,   0,   0, 255 };
static constexpr pu::ui::Color kTopBarBg = {  12,  12,  18, 255 };
static constexpr pu::ui::Color kWhite    = { 255, 255, 255, 255 };
static constexpr pu::ui::Color kMuted    = { 160, 160, 175, 255 };
static constexpr pu::ui::Color kCardDark = {  14,  14,  20, 255 };

// SNES Super Famicom colors: A=Red, X=Blue, Y=Green
static constexpr pu::ui::Color kSnesColors[3] = {
    { 196,   0,   0, 255 }, // A (Red)   -> Ports/Browse
    {  20,  80, 200, 255 }, // X (Blue)  -> Queue
    {   0, 140,   0, 255 }, // Y (Green) -> Settings
};

// Tab i18n keys (order: Browse, Queue, Settings)
static constexpr const char *kTabKeys[3] = {
    "tabs.ports", "tabs.queue", "tabs.settings"
};

static const std::string kFontMedium = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);
static const std::string kFontSmall  = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);

s32 MainLayout::CardX(const s32 idx) {
    // gap = (1920 - 3*360) / 4 = 210
    const s32 gap = (kW - kTabCount * kCardW) / (kTabCount + 1);
    return gap + idx * (kCardW + gap);
}

MainLayout::MainLayout(MainApplication *app)
    : pu::ui::Layout(), app_(app)
{
    this->SetBackgroundColor(kBlack);
    BuildHome();
    BuildContentHeader();
    BuildTabs();
    ShowHome();

    this->SetOnInput([this](const u64 kd, const u64 ku, const u64 kh,
                             const pu::ui::TouchPoint tp) {
        OnInput(kd, ku, kh, tp);
    });
}

void MainLayout::BuildHome() {
    home_bg_ = pu::ui::elm::Rectangle::New(0, 0, kW, kH, kBlack);
    this->Add(home_bg_);

    // Logo: 240x240 centered at x=(1920-240)/2=840, y=50
    {
        auto raw = pu::ui::render::LoadImageFromFile("romfs:/icon.jpg");
        pu::sdl2::TextureHandle::Ref th = raw ? pu::sdl2::TextureHandle::New(raw) : nullptr;
        home_logo_ = pu::ui::elm::Image::New(840, 50, th);
        home_logo_->SetWidth(240);
        home_logo_->SetHeight(240);
        this->Add(home_logo_);
    }

    static constexpr const char *kIconPaths[] = {
        "romfs:/download.png",
        "romfs:/queue.png",
        "romfs:/settings.png",
    };

    for (s32 i = 0; i < kTabCount; ++i) {
        const s32 cx = CardX(i);

        // Selection frame: 360x360, SNES color when selected
        card_bg_[i] = pu::ui::elm::Rectangle::New(cx, kCardY, kCardW, kCardH, kCardDark, 20);
        this->Add(card_bg_[i]);

        // Icon: 300x300, 30px inset in 360x360 frame
        {
            auto raw = pu::ui::render::LoadImageFromFile(kIconPaths[i]);
            pu::sdl2::TextureHandle::Ref th = raw ? pu::sdl2::TextureHandle::New(raw) : nullptr;
            card_icon_img_[i] = pu::ui::elm::Image::New(cx + 30, kCardY + 30, th);
            card_icon_img_[i]->SetWidth(300);
            card_icon_img_[i]->SetHeight(300);
            if (!th) card_icon_img_[i]->SetVisible(false);
            this->Add(card_icon_img_[i]);
        }

        // Label centered below card — X updated in RefreshStrings()
        card_label_[i] = pu::ui::elm::TextBlock::New(cx, kCardY + kCardH + 30,
                                                      pinx::i18n::tr(kTabKeys[i]));
        card_label_[i]->SetColor(kWhite);
        card_label_[i]->SetFont(kFontMedium);
        this->Add(card_label_[i]);
    }

    // Hint bar — B=\xEE\x82\xA1 Navigate  A=\xEE\x82\xA0 Open  + Exit
    home_hint_tb_ = pu::ui::elm::TextBlock::New(0, 1022, "");
    home_hint_tb_->SetColor(kMuted);
    home_hint_tb_->SetFont(kFontSmall);
    this->Add(home_hint_tb_);

    RefreshHints();
}

void MainLayout::RefreshHints() {
    // Home hint — centered at 1920/2
    const std::string h = std::string("\xEE\x82\xA1 ") + pinx::i18n::tr("hints.navigate") +
                          "    \xEE\x82\xA0 " + pinx::i18n::tr("hints.open") +
                          "    + " + pinx::i18n::tr("hints.exit");
    home_hint_tb_->SetText(h);
    home_hint_tb_->SetX(kW / 2 - home_hint_tb_->GetWidth() / 2);

    if (content_back_hint_)
        content_back_hint_->SetText(std::string("\xEE\x82\xA1 ") + pinx::i18n::tr("hints.back_menu"));

    for (s32 i = 0; i < kTabCount; ++i) {
        const s32 cx = CardX(i);
        card_label_[i]->SetText(pinx::i18n::tr(kTabKeys[i]));
        card_label_[i]->SetX(cx + kCardW / 2 - card_label_[i]->GetWidth() / 2);
    }
}

void MainLayout::BuildContentHeader() {
    content_topbar_ = pu::ui::elm::Rectangle::New(0, 0, kW, kTopBarH, kTopBarBg);
    content_topbar_->SetVisible(false);
    this->Add(content_topbar_);

    content_title_ = pu::ui::elm::TextBlock::New(48, 24, "");
    content_title_->SetColor(kWhite);
    content_title_->SetFont(pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Large));
    content_title_->SetVisible(false);
    this->Add(content_title_);

    content_back_hint_ = pu::ui::elm::TextBlock::New(kW - 300, 38, "");
    content_back_hint_->SetColor(kMuted);
    content_back_hint_->SetFont(kFontSmall);
    content_back_hint_->SetVisible(false);
    this->Add(content_back_hint_);

    RefreshHints();
}

void MainLayout::BuildTabs() {
    browse_   = std::make_unique<BrowseTab>(app_->GetConfig(),
                                            app_->GetDownloader(),
                                            app_->GetInstaller());
    queue_    = std::make_unique<QueueTab>(app_->GetDownloader(),
                                           app_->GetInstaller());
    settings_ = std::make_unique<SettingsTab>(app_->GetConfig(), [this]() {
        // Called when URL changes in Settings; language change triggers RefreshStrings()
        browse_->reload();
    }, [this]() {
        // Called when language changes
        pinx::i18n::Init(app_->GetConfig()->language);
        RefreshStrings();
    });

    browse_->AddElementsTo(this);
    queue_->AddElementsTo(this);
    settings_->AddElementsTo(this);
}

void MainLayout::RefreshStrings() {
    RefreshHints();
    browse_->RefreshStrings();
    queue_->RefreshStrings();
    settings_->RefreshStrings();
    // Re-enter current context to update title
    if (!home_mode_)
        content_title_->SetText(pinx::i18n::tr(kTabKeys[static_cast<u8>(current_tab_)]));
}

void MainLayout::ShowHome() {
    home_mode_ = true;

    home_bg_->SetVisible(true);
    if (home_logo_) home_logo_->SetVisible(true);
    for (s32 i = 0; i < kTabCount; ++i) {
        card_bg_[i]->SetVisible(true);
        if (card_icon_img_[i]) card_icon_img_[i]->SetVisible(true);
        card_label_[i]->SetVisible(true);
    }
    home_hint_tb_->SetVisible(true);

    content_topbar_->SetVisible(false);
    content_title_->SetVisible(false);
    content_back_hint_->SetVisible(false);

    browse_->Hide();
    queue_->Hide();
    settings_->Hide();

    UpdateCardColors();
}

void MainLayout::EnterTab(Tab t) {
    home_mode_   = false;
    current_tab_ = t;

    home_bg_->SetVisible(false);
    if (home_logo_) home_logo_->SetVisible(false);
    for (s32 i = 0; i < kTabCount; ++i) {
        card_bg_[i]->SetVisible(false);
        if (card_icon_img_[i]) card_icon_img_[i]->SetVisible(false);
        card_label_[i]->SetVisible(false);
    }
    home_hint_tb_->SetVisible(false);

    content_topbar_->SetVisible(true);
    content_title_->SetVisible(true);
    content_back_hint_->SetVisible(true);
    content_title_->SetText(pinx::i18n::tr(kTabKeys[static_cast<u8>(t)]));

    browse_->Hide();
    queue_->Hide();
    settings_->Hide();
    switch (t) {
        case Tab::Browse:   browse_->Show();   break;
        case Tab::Queue:    queue_->Show();    break;
        case Tab::Settings: settings_->Show(); break;
        default: break;
    }
}

void MainLayout::BackToHome() {
    browse_->Hide();
    queue_->Hide();
    settings_->Hide();
    ShowHome();
}

void MainLayout::OnSettingsChanged() {
    browse_->reload();
    EnterTab(Tab::Browse);
}

void MainLayout::UpdateCardColors() {
    for (s32 i = 0; i < kTabCount; ++i) {
        if (i == home_sel_) {
            card_bg_[i]->SetColor(kSnesColors[i]);
            card_label_[i]->SetColor(kWhite);
        } else {
            card_bg_[i]->SetColor(kCardDark);
            card_label_[i]->SetColor(kMuted);
        }
    }
}

void MainLayout::OnInput(u64 kd, u64 /*ku*/, u64 /*kh*/, pu::ui::TouchPoint tp) {
    if (kd & HidNpadButton_Plus) {
        app_->CloseWithFadeOut();
        return;
    }

    if (home_mode_) {
        if (kd & HidNpadButton_Left) {
            if (home_sel_ > 0) { --home_sel_; UpdateCardColors(); }
        }
        if (kd & HidNpadButton_Right) {
            if (home_sel_ < kTabCount - 1) { ++home_sel_; UpdateCardColors(); }
        }
        if (kd & HidNpadButton_A) {
            EnterTab(static_cast<Tab>(home_sel_));
        }
        if (!tp.IsEmpty()) {
            const s32 tx = tp.x * 3 / 2;
            const s32 ty = tp.y * 3 / 2;
            const pu::ui::TouchPoint scaled(tx, ty);
            for (s32 i = 0; i < kTabCount; ++i) {
                if (scaled.HitsRegion(CardX(i), kCardY, kCardW, kCardH)) {
                    if (home_sel_ == i) {
                        EnterTab(static_cast<Tab>(i));
                    } else {
                        home_sel_ = i;
                        UpdateCardColors();
                    }
                    break;
                }
            }
        }
    } else {
        if (current_tab_ == Tab::Browse) {
            if (browse_->HandleInput(kd)) {
                browse_->Poll();
                queue_->Poll();
                settings_->Poll();
                return;
            }
        }

        if (kd & HidNpadButton_B) {
            BackToHome();
            return;
        }

        // Touch for browse grid cells (scale 720p→1080p), 3x2 grid
        if (!tp.IsEmpty() && current_tab_ == Tab::Browse) {
            const s32 tx = tp.x * 3 / 2;
            const s32 ty = tp.y * 3 / 2;
            const pu::ui::TouchPoint scaled(tx, ty);
            for (s32 ci = 0; ci < 6; ++ci) {
                const s32 col = ci % 3;
                const s32 row = ci / 3;
                const s32 cx  = 120 + col * 560;
                const s32 cy  = 130 + row * 420;
                if (scaled.HitsRegion(cx, cy, 560, 420)) {
                    browse_->TouchCell(ci);
                    break;
                }
            }
        }

        browse_->Poll();
        queue_->Poll();
        settings_->Poll();
    }
}

} // namespace pinx::ui
