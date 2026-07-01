#include <ui/InstallView.hpp>

#include <cstdio>
#include <string>

namespace pinx::ui {
namespace {

using State = pinx::install::InstallManager::State;

std::string HumanSize(std::uint64_t bytes) {
    const char *units[] = {"B", "KB", "MB", "GB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while(v >= 1024.0 && u < 3) { v /= 1024.0; ++u; }
    char buf[32];
    if(u == 0) std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    else        std::snprintf(buf, sizeof(buf), "%.1f %s", v, units[u]);
    return buf;
}

}

InstallView::InstallView(pinx::install::InstallManager *mgr) : brls::List(), mgr(mgr) {
    title_item = new brls::ListItem("Installing...");
    bar        = new brls::ProgressDisplay();
    info_item  = new brls::ListItem("");

    this->addView(title_item);
    this->addView(bar);
    this->addView(info_item);

    this->registerAction("Cancel / Close", brls::Key::B, [this]() {
        const auto s = this->mgr->snapshot();
        if(s.state == State::Running) {
            this->mgr->cancel();
            brls::Application::notify("Canceling...");
        } else {
            brls::Application::popView();
        }
        return true;
    });
}

void InstallView::frame(brls::FrameContext *ctx) {
    brls::List::frame(ctx);

    const auto s = mgr->snapshot();

    if(!name_set_ && !s.display_name.empty()) {
        title_item->setLabel(s.display_name);
        name_set_ = true;
    }

    switch(s.state) {
        case State::Running: {
            if(s.bytes_total > 0) {
                const int pct = static_cast<int>((s.bytes_done * 100) / s.bytes_total);
                bar->setProgress(pct * 10, 1000);

                char info[64];
                std::snprintf(info, sizeof(info), "%d%%  %s / %s",
                    pct,
                    HumanSize(s.bytes_done).c_str(),
                    HumanSize(s.bytes_total).c_str());
                info_item->setLabel(info);
            } else {
                bar->setProgress(0, 1000);
                info_item->setLabel(HumanSize(s.bytes_done));
            }
            break;
        }
        case State::Done:
            bar->setProgress(1000, 1000);
            info_item->setLabel("Done!  Press B to close.");
            break;
        case State::Failed:
            info_item->setLabel("Error: " + s.error);
            break;
        default:
            break;
    }
}

}
