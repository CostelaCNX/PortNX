#include <ui/TitlesTab.hpp>

#include <algorithm>
#include <cstdio>
#include <string>

namespace pinx::ui {
namespace {

std::string StorageName(NcmStorageId id) {
    return id == NcmStorageId_SdCard ? "SD" : "NAND";
}

std::uint64_t BaseAppId(std::uint64_t tid, NcmContentMetaType type) {
    if(type == NcmContentMetaType_Patch)        return tid ^ 0x800ull;
    if(type == NcmContentMetaType_AddOnContent) return tid & ~0xFFFull;
    return tid;
}

std::string MakeSub(const pinx::install::InstalledTitle &t, bool include_tid) {
    std::string s;
    if(include_tid) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%016llX  ",
                      static_cast<unsigned long long>(t.title_id));
        s = buf;
    }
    s += pinx::install::MetaTypeName(t.type);
    s += "  ";
    s += pinx::install::VersionString(t.version);
    s += "  [";
    s += StorageName(t.storage_id);
    s += "]";
    if(t.content_count > 0) {
        char nca_buf[32];
        std::snprintf(nca_buf, sizeof(nca_buf), "  %u NCAs", t.content_count);
        s += nca_buf;
    }
    return s;
}

}

TitlesTab::TitlesTab() : brls::List() {
    pending_refresh_ = true;
}

TitlesTab::~TitlesTab() {
    if(uninstall_thread_.joinable()) uninstall_thread_.detach();
}

void TitlesTab::frame(brls::FrameContext *ctx) {
    if(uninstall_done_.load()) {
        uninstall_done_.store(false);
        if(uninstall_thread_.joinable()) uninstall_thread_.join();
        brls::Application::unblockInputs();
        brls::Application::notify(uninstall_ok_.load()
            ? "Uninstalled." : "Uninstall failed (partial).");
        pending_refresh_ = true;
    }

    if(pending_refresh_) {
        pending_refresh_ = false;
        refresh();
        brls::Application::giveFocus(this);
    }

    brls::List::frame(ctx);
}

void TitlesTab::refresh() {
    this->clear();

    const auto titles = pinx::install::ListInstalledTitles();
    if(titles.empty()) {
        auto *item = new brls::ListItem("No titles installed");
        item->setSubLabel("Install a title from the Browse tab.");
        this->addView(item);
        return;
    }
    populate(titles);
}

void TitlesTab::populate(const std::vector<pinx::install::InstalledTitle> &titles) {
    std::vector<pinx::install::InstalledTitle> sorted = titles;
    std::sort(sorted.begin(), sorted.end(), [](const auto &a, const auto &b) {
        if(a.type != b.type) return static_cast<int>(a.type) < static_cast<int>(b.type);
        return a.title_id < b.title_id;
    });

    auto *refresh_item = new brls::ListItem("Refresh list");
    refresh_item->getClickEvent()->subscribe([this](brls::View *) {
        pending_refresh_ = true;
    });
    this->addView(refresh_item);

    char count_buf[64];
    std::snprintf(count_buf, sizeof(count_buf), "%zu title(s)", sorted.size());
    auto *count_item = new brls::ListItem(count_buf);
    count_item->setSubLabel("Select a title to uninstall.");
    this->addView(count_item);

    for(auto t : sorted) {
        const std::uint64_t app_id = BaseAppId(t.title_id, t.type);
        const pinx::install::TitleInfo info = pinx::install::ReadTitleInfo(app_id);
        t.name = info.name;

        const std::string label = t.name.empty()
            ? [&]{ char b[32]; std::snprintf(b,sizeof(b),"%016llX",
                    static_cast<unsigned long long>(t.title_id)); return std::string(b); }()
            : t.name;

        auto *item = new brls::ListItem(label);
        item->setSubLabel(MakeSub(t, t.name.empty()));
        if(!info.icon_jpeg.empty()) {
            item->setThumbnail(const_cast<unsigned char *>(info.icon_jpeg.data()), info.icon_jpeg.size());
        }
        item->getClickEvent()->subscribe([this, t](brls::View *) {
            if(uninstalling_.load()) {
                brls::Application::notify("Uninstall already in progress.");
                return;
            }
            showUninstallDialog(t);
        });
        this->addView(item);
    }
}

void TitlesTab::showUninstallDialog(const pinx::install::InstalledTitle &title) {
    char msg[256];
    if(title.name.empty()) {
        std::snprintf(msg, sizeof(msg), "Uninstall %016llX?\n%s  %s  [%s]",
            static_cast<unsigned long long>(title.title_id),
            pinx::install::MetaTypeName(title.type),
            pinx::install::VersionString(title.version).c_str(),
            title.storage_id == NcmStorageId_SdCard ? "SD" : "NAND");
    } else {
        std::snprintf(msg, sizeof(msg), "Uninstall %s?\n%s  %s  [%s]",
            title.name.c_str(),
            pinx::install::MetaTypeName(title.type),
            pinx::install::VersionString(title.version).c_str(),
            title.storage_id == NcmStorageId_SdCard ? "SD" : "NAND");
    }

    auto *dialog = new brls::Dialog(msg);

    dialog->addButton("Uninstall", [this, title, dialog](brls::View *) {
        dialog->close();  // pop from view stack before starting work
        brls::Application::blockInputs();
        uninstalling_.store(true);
        uninstall_done_.store(false);
        uninstall_ok_.store(false);
        if(uninstall_thread_.joinable()) uninstall_thread_.join();
        uninstall_thread_ = std::thread([this, title]() {
            const bool ok = pinx::install::UninstallTitle(title);
            uninstall_ok_.store(ok);
            uninstalling_.store(false);
            uninstall_done_.store(true);
        });
    });

    dialog->addButton("Cancel", [dialog](brls::View *) { dialog->close(); });
    dialog->setCancelable(true);
    dialog->open();
}

}
