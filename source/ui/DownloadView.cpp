#include <ui/DownloadView.hpp>
#include <ui/InstallView.hpp>

#include <cstdio>
#include <string>

namespace pinx::ui {
namespace {

using DState = pinx::download::DownloadManager::State;

std::string HumanSize(std::uint64_t bytes) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while(value >= 1024.0 && unit < 4) { value /= 1024.0; ++unit; }
    char buf[40];
    if(unit == 0) std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    else          std::snprintf(buf, sizeof(buf), "%.1f %s", value, units[unit]);
    return buf;
}

}

DownloadView::DownloadView(pinx::download::DownloadManager *d,
                            pinx::install::InstallManager   *inst)
    : brls::List(), dm(d), installer(inst) {

    const auto snap = dm->snapshot();

    bar          = new brls::ProgressDisplay();
    status       = new brls::ListItem(snap.name.empty() ? "Download" : snap.name);
    install_item = new brls::ListItem("Install");

    status->setSubLabel("Starting...");
    install_item->setSubLabel("(waiting for download to finish)");

    this->addView(bar);
    this->addView(status);
    this->addView(install_item);

    last_tick = std::chrono::steady_clock::now();

    install_item->getClickEvent()->subscribe([this](brls::View *) {
        if(!install_enabled) {
            brls::Application::notify("Download not finished yet.");
            return;
        }
        const auto snap = dm->snapshot();
        if(snap.result_path.empty()) {
            brls::Application::notify("No file path available.");
            return;
        }
        pinx::install::InstallManager::Request req;
        req.file_path = snap.result_path;
        if(!installer->start(req)) {
            brls::Application::notify("Install already running.");
            return;
        }
        brls::Application::pushView(new pinx::ui::InstallView(installer));
    });

    this->registerAction("Cancel / Close", brls::Key::B, [this]() {
        const auto s = dm->snapshot();
        if(s.state == DState::Running || s.state == DState::Verifying) {
            dm->cancel();
            return true;
        }
        brls::Application::popView();
        return true;
    });
}

void DownloadView::frame(brls::FrameContext *ctx) {
    brls::List::frame(ctx);

    const auto s = dm->snapshot();

    const auto now = std::chrono::steady_clock::now();
    const double dt = std::chrono::duration<double>(now - last_tick).count();
    if(dt >= 0.5) {
        if(s.done >= last_done) {
            speed_bps = static_cast<double>(s.done - last_done) / dt;
        }
        last_done = s.done;
        last_tick = now;
    }

    if(s.total > 0) {
        bar->setProgress(static_cast<int>((s.done * 1000) / s.total), 1000);
    } else {
        bar->setProgress(0, 1000);
    }

    std::string sub;
    switch(s.state) {
        case DState::Running: {
            sub = (s.total > 0) ? std::to_string((s.done * 100) / s.total) + "%"
                                : HumanSize(s.done);
            sub += "  -  " + HumanSize(static_cast<std::uint64_t>(speed_bps)) + "/s";
            if(s.total > 0 && speed_bps > 1.0) {
                const std::uint64_t remaining = (s.total > s.done) ? (s.total - s.done) : 0;
                sub += "  -  ETA " + std::to_string(
                    static_cast<std::uint64_t>(remaining / speed_bps)) + "s";
            }
            break;
        }
        case DState::Verifying: sub = "Verifying integrity..."; break;
        case DState::Done:
            sub = "Done - saved to SD.";
            if(!install_enabled) {
                install_enabled = true;
                install_item->setSubLabel("Press A to install");
            }
            break;
        case DState::Failed:    sub = "Failed: " + s.error; break;
        case DState::Canceled:  sub = "Canceled. Press B to close."; break;
        default:                sub = "Starting..."; break;
    }
    status->setSubLabel(sub);
}

}
