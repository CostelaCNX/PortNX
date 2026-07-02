#pragma once

#include <pu/Plutonium>
#include <app/Config.hpp>
#include <download/DownloadManager.hpp>
#include <install/InstallManager.hpp>

namespace pinx::ui {

class MainLayout;

class MainApplication : public pu::ui::Application {
public:
    PU_SMART_CTOR(MainApplication)
    explicit MainApplication(pu::ui::render::Renderer::Ref renderer);

    void OnLoad() override;
    void OnSettingsChanged();

    pinx::app::Config               *GetConfig()     { return &config_;     }
    pinx::download::DownloadManager *GetDownloader() { return &downloader_; }
    pinx::install::InstallManager   *GetInstaller()  { return &installer_;  }
    const std::string               &GetDeviceUid()  { return device_uid_;  }

private:
    pinx::app::Config               config_;
    pinx::download::DownloadManager downloader_;
    pinx::install::InstallManager   installer_;
    std::shared_ptr<MainLayout>     layout_;
    std::string                     device_uid_;
};

} // namespace pinx::ui
