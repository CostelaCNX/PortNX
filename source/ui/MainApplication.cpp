#include <ui/MainApplication.hpp>
#include <ui/MainLayout.hpp>
#include <app/I18n.hpp>

namespace pinx::ui {

MainApplication::MainApplication(pu::ui::render::Renderer::Ref renderer)
    : pu::ui::Application(renderer) {}

void MainApplication::OnLoad() {
    // Read device serial number once at startup — used as UID in catalog requests
    {
        SetSysSerialNumber sn;
        if (R_SUCCEEDED(setsysInitialize())) {
            if (R_SUCCEEDED(setsysGetSerialNumber(&sn)))
                device_uid_ = std::string(sn.number,
                                  strnlen(sn.number, sizeof(sn.number)));
            setsysExit();
        }
    }

    config_ = pinx::app::Config::Load();
    pinx::i18n::Init(config_.language);
    layout_ = MainLayout::New(this);
    this->LoadLayout(layout_);
}

void MainApplication::OnSettingsChanged() {
    layout_->OnSettingsChanged();
}

} // namespace pinx::ui
