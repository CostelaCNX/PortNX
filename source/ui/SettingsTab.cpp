#include <ui/SettingsTab.hpp>
#include <app/I18n.hpp>

#include <switch.h>

namespace pinx::ui {

namespace {

std::string LangLabel(const std::string &lang) {
    if (lang == "en-US") return pinx::i18n::tr("settings.lang_en");
    return pinx::i18n::tr("settings.lang_ptbr");
}

std::string UrlLabel(const std::string &url) {
    return url.empty() ? pinx::i18n::tr("settings.not_set") : url;
}

} // namespace

SettingsTab::SettingsTab(pinx::app::Config *cfg,
                         std::function<void()> on_url_changed,
                         std::function<void()> on_lang_changed)
    : config_(cfg),
      on_url_changed_(std::move(on_url_changed)),
      on_lang_changed_(std::move(on_lang_changed)) {}

void SettingsTab::AddElementsTo(pu::ui::Layout *layout) {
    menu_ = pu::ui::elm::Menu::New(60, 100, 1800, kItemClr, kFocusClr, kItemH, kItemN);

    lang_item_      = pu::ui::elm::MenuItem::New("");
    url_item_       = pu::ui::elm::MenuItem::New("");
    storage_item_   = pu::ui::elm::MenuItem::New("");
    reinstall_item_ = pu::ui::elm::MenuItem::New("");

    lang_item_->SetColor(kTextClr);
    url_item_->SetColor(kTextClr);
    storage_item_->SetColor(kTextClr);
    reinstall_item_->SetColor(kTextClr);

    constexpr u64 kActivate = HidNpadButton_A | pu::ui::TouchPseudoKey;

    lang_item_->AddOnKey([this]() {
        if (input_guard_ > 0) return;
        std::string &lang = config_->language;
        if (lang == "en-US") lang = "pt-BR";
        else                 lang = "en-US";
        config_->Save();
        pending_rebuild_      = true;
        pending_lang_refresh_ = true;
    }, kActivate);

    url_item_->AddOnKey([this]() {
        SwkbdConfig swkbd;
        swkbdCreate(&swkbd, 0);
        swkbdConfigMakePresetDefault(&swkbd);
        swkbdConfigSetHeaderText(&swkbd, "Index URL");
        swkbdConfigSetInitialText(&swkbd, config_->server_url.c_str());
        swkbdConfigSetStringLenMax(&swkbd, 512);
        char out[512] = {};
        if (R_SUCCEEDED(swkbdShow(&swkbd, out, sizeof(out)))) {
            config_->server_url = out;
            config_->Save();
            UpdateItemLabels();
            if (on_url_changed_) on_url_changed_();
        }
        swkbdClose(&swkbd);
    }, kActivate);

    storage_item_->AddOnKey([this]() {
        config_->install_to_nand = !config_->install_to_nand;
        config_->Save();
        UpdateItemLabels();
    }, kActivate);

    reinstall_item_->AddOnKey([this]() {
        config_->force_reinstall = !config_->force_reinstall;
        config_->Save();
        UpdateItemLabels();
    }, kActivate);

    UpdateItemLabels();

    menu_->AddItem(lang_item_);
    menu_->AddItem(url_item_);
    menu_->AddItem(storage_item_);
    menu_->AddItem(reinstall_item_);
    menu_->ForceReloadItems();

    layout->Add(menu_);
    Hide();
}

void SettingsTab::Show() {
    visible_      = true;
    input_guard_  = 3;
    menu_->SetVisible(true);
}

void SettingsTab::Hide() {
    visible_ = false;
    menu_->SetVisible(false);
}

void SettingsTab::Poll() {
    if (!visible_) return;
    if (input_guard_ > 0) { --input_guard_; return; }

    if (pending_rebuild_) {
        pending_rebuild_ = false;
        UpdateItemLabels();
        menu_->ForceReloadItems();
    }

    if (pending_lang_refresh_) {
        pending_lang_refresh_ = false;
        if (on_lang_changed_) on_lang_changed_();
    }
}

void SettingsTab::RefreshStrings() {
    UpdateItemLabels();
    if (menu_) menu_->ForceReloadItems();
}

void SettingsTab::UpdateItemLabels() {
    lang_item_->SetName(pinx::i18n::tr("settings.language") + "  |  " +
                        LangLabel(config_->language));
    url_item_->SetName(pinx::i18n::tr("settings.server_url") + "  |  " +
                       UrlLabel(config_->server_url));
    storage_item_->SetName(pinx::i18n::tr("settings.destination") + "  |  " +
                           (config_->install_to_nand
                                ? pinx::i18n::tr("settings.nand")
                                : pinx::i18n::tr("settings.sd_card")));
    reinstall_item_->SetName(pinx::i18n::tr("settings.force_reinstall") + "  |  " +
                             (config_->force_reinstall
                                  ? pinx::i18n::tr("settings.on")
                                  : pinx::i18n::tr("settings.off")));
    if (menu_) menu_->ForceReloadItems();
}

} // namespace pinx::ui
