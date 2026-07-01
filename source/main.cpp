#include <borealis.hpp>

#include <switch.h>

#include <app/Config.hpp>
#include <download/DownloadManager.hpp>
#include <install/InstallManager.hpp>
#include <install/es_ipc.h>
#include <install/ns_ext_ipc.h>
#include <ui/BrowseTab.hpp>
#include <ui/QueueTab.hpp>
#include <ui/SettingsTab.hpp>

namespace i18n = brls::i18n;

namespace {
pinx::app::Config               g_config;
pinx::download::DownloadManager g_downloader;
pinx::install::InstallManager   g_installer;
}

int main(int argc, char *argv[]) {
    splInitialize();
    splCryptoInitialize();
    ncmInitialize();
    esInitialize();
    nsInitialize();
    nsextInitialize();

    if(!brls::Application::init(APP_TITLE)) {
        brls::Logger::error("Unable to init Borealis application");
        nsextExit();
        esExit();
        ncmExit();
        splCryptoExit();
        splExit();
        return EXIT_FAILURE;
    }

    g_config = pinx::app::Config::Load();

    if(g_config.language.empty())
        i18n::loadTranslations();
    else
        i18n::loadTranslations(g_config.language);

    using namespace brls::i18n::literals;

    brls::TabFrame *root = new brls::TabFrame();
    root->setTitle(APP_TITLE);
    root->setFooterText("v" APP_VERSION);
    root->setIcon("romfs:/icon/borealis.jpg");

    auto *browse = new pinx::ui::BrowseTab(&g_config, &g_downloader, &g_installer);
    root->addTab("portnx/tabs/ports"_i18n, browse);
    root->addTab("portnx/tabs/queue"_i18n, new pinx::ui::QueueTab(&g_downloader, &g_installer));

    root->addSeparator();

    root->addTab("portnx/tabs/settings"_i18n, new pinx::ui::SettingsTab(&g_config, [browse]() {
        browse->reload();
    }));

    root->registerAction("", brls::Key::B, [] { return true; });

    brls::Application::pushView(root);

    while(brls::Application::mainLoop());

    g_installer.shutdown();
    g_downloader.shutdown();

    nsextExit();
    nsExit();
    esExit();
    ncmExit();
    splCryptoExit();
    splExit();

    return EXIT_SUCCESS;
}
