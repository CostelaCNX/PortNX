#include <switch.h>
#include <install/es_ipc.h>
#include <install/ns_ext_ipc.h>
#include <ui/MainApplication.hpp>

int main(int argc, char *argv[]) {
    socketInitializeDefault();
    splInitialize();
    splCryptoInitialize();
    ncmInitialize();
    esInitialize();
    nsInitialize();
    nsextInitialize();

    auto renderer_opts = pu::ui::render::RendererInitOptions(SDL_INIT_EVERYTHING,
                             pu::ui::render::RendererHardwareFlags);
    renderer_opts.UseRomfs();
    renderer_opts.SetPlServiceType(PlServiceType_User);
    renderer_opts.AddDefaultAllSharedFonts();
    renderer_opts.SetInputPlayerCount(1);
    renderer_opts.AddInputNpadStyleTag(HidNpadStyleSet_NpadStandard);
    renderer_opts.AddInputNpadIdType(HidNpadIdType_Handheld);
    renderer_opts.AddInputNpadIdType(HidNpadIdType_No1);

    renderer_opts.AddExtraDefaultFontSize(72);
    renderer_opts.AddExtraDefaultFontSize(56);

    auto renderer = pu::ui::render::Renderer::New(renderer_opts);
    auto app = pinx::ui::MainApplication::New(renderer);
    app->Load();
    app->ShowWithFadeIn();

    app->GetInstaller()->shutdown();
    app->GetDownloader()->shutdown();

    nsextExit();
    nsExit();
    esExit();
    ncmExit();
    splCryptoExit();
    splExit();
    socketExit();

    return 0;
}
