#include <ui/SettingsTab.hpp>
#include <ui/GlassListItem.hpp>

#include <borealis/i18n.hpp>
using namespace brls::i18n::literals;

namespace pinx::ui {

SettingsTab::SettingsTab(pinx::app::Config *cfg, std::function<void()> changed)
    : brls::List(), config(cfg), on_changed(std::move(changed)) {
    build();
}

void SettingsTab::frame(brls::FrameContext *ctx) {
    if(pending_rebuild_) {
        pending_rebuild_ = false;
        build();
        brls::Application::giveFocus(this);
    }

    brls::List::frame(ctx);
}

void SettingsTab::build() {
    this->clear();

    auto *lang_header = new brls::Label(brls::LabelStyle::REGULAR,
                                        "portnx/settings/lang_hdr"_i18n, true);
    this->addView(lang_header);

    const std::string &cur_lang = config->language;
    std::string lang_label;
    if(cur_lang == "en-US")       lang_label = "portnx/settings/lang_en"_i18n;
    else if(cur_lang == "pt-BR")  lang_label = "portnx/settings/lang_ptbr"_i18n;
    else                          lang_label = "portnx/settings/lang_system"_i18n;

    auto *lang_item = new pinx::ui::GlassListItem("portnx/settings/language"_i18n);
    lang_item->setValue(lang_label);
    lang_item->getClickEvent()->subscribe([this](brls::View *) {
        std::string &lang = config->language;
        if(lang.empty())       lang = "en-US";
        else if(lang == "en-US") lang = "pt-BR";
        else                   lang = "";
        config->Save();
        brls::Application::notify("portnx/settings/lang_restart"_i18n);
        pending_rebuild_ = true;
    });
    this->addView(lang_item);

    auto *srv_header = new brls::Label(brls::LabelStyle::REGULAR,
                                       "portnx/settings/server_hdr"_i18n, true);
    this->addView(srv_header);

    auto *url_item = new pinx::ui::GlassListItem("portnx/settings/server_url"_i18n);
    url_item->setValue(config->server_url.empty()
                       ? "portnx/settings/not_set"_i18n
                       : config->server_url);
    url_item->getClickEvent()->subscribe([this, url_item](brls::View *) {
        const std::string current = config->server_url;
        brls::Swkbd::openForText([this, url_item](std::string text) {
            config->server_url = text;
            config->Save();
            url_item->setValue(text.empty() ? "portnx/settings/not_set"_i18n : text);
            if(on_changed) on_changed();
        }, "portnx/settings/server_url"_i18n, "https://...", 512, current);
    });
    this->addView(url_item);

    auto *install_header = new brls::Label(brls::LabelStyle::REGULAR,
                                           "portnx/settings/install_hdr"_i18n, true);
    this->addView(install_header);

    auto *storage_item = new pinx::ui::GlassListItem("portnx/settings/destination"_i18n);
    storage_item->setValue(config->install_to_nand
                           ? "portnx/settings/nand"_i18n
                           : "portnx/settings/sd_card"_i18n);
    storage_item->getClickEvent()->subscribe([this, storage_item](brls::View *) {
        config->install_to_nand = !config->install_to_nand;
        config->Save();
        storage_item->setValue(config->install_to_nand
                               ? "portnx/settings/nand"_i18n
                               : "portnx/settings/sd_card"_i18n, false, false);
    });
    this->addView(storage_item);

    auto *reinstall_item = new pinx::ui::GlassListItem("portnx/settings/force_reinstall"_i18n);
    reinstall_item->setValue(config->force_reinstall
                             ? "portnx/settings/on"_i18n
                             : "portnx/settings/off"_i18n);
    reinstall_item->getClickEvent()->subscribe([this, reinstall_item](brls::View *) {
        config->force_reinstall = !config->force_reinstall;
        config->Save();
        reinstall_item->setValue(config->force_reinstall
                                 ? "portnx/settings/on"_i18n
                                 : "portnx/settings/off"_i18n, false, false);
    });
    this->addView(reinstall_item);
}

}
