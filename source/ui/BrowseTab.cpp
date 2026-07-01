#include <ui/BrowseTab.hpp>
#include <ui/GlassListItem.hpp>

#include <algorithm>
#include <cstdio>
#include <sys/stat.h>

#include <borealis/i18n.hpp>
using namespace brls::i18n::literals;

#include <catalog/IndexParser.hpp>
#include <catalog/TinfoilDecryptor.hpp>
#include <download/FsGuard.hpp>
#include <install/TitleManager.hpp>
#include <net/HttpClient.hpp>

namespace pinx::ui {
namespace {

static std::unordered_map<std::string, std::vector<uint8_t>> s_icon_cache;

static constexpr const char *kIconCacheDir = "sdmc:/switch/PortNX/icons";

static std::uint64_t fnv1a64(const std::string &s) {
    std::uint64_t h = 14695981039346656037ULL;
    for(unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

static std::string iconCachePath(const std::string &url) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s/%016llx.jpg", kIconCacheDir,
                  static_cast<unsigned long long>(fnv1a64(url)));
    return buf;
}

static std::vector<uint8_t> loadIconFromDisk(const std::string &url) {
    FILE *f = fopen(iconCachePath(url).c_str(), "rb");
    if(!f) return {};
    fseek(f, 0, SEEK_END);
    const long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if(sz <= 0) { fclose(f); return {}; }
    std::vector<uint8_t> buf(static_cast<std::size_t>(sz));
    fread(buf.data(), 1, buf.size(), f);
    fclose(f);
    return buf;
}

static void saveIconToDisk(const std::string &url, const std::vector<uint8_t> &data) {
    mkdir(kIconCacheDir, 0777);
    FILE *f = fopen(iconCachePath(url).c_str(), "wb");
    if(!f) return;
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}

std::string HumanSize(std::uint64_t bytes) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while(value >= 1024.0 && unit < 4) { value /= 1024.0; ++unit; }
    char buf[32];
    if(unit == 0) std::snprintf(buf, sizeof(buf), "%llu B",
                                static_cast<unsigned long long>(bytes));
    else          std::snprintf(buf, sizeof(buf), "%.1f %s", value, units[unit]);
    return buf;
}

struct IconThreadArgs {
    std::shared_ptr<BrowseTab::IconState> state;
    std::uint32_t                         gen;
    std::vector<std::string>              urls;
    pinx::net::HttpOptions                opts;
};

void iconThreadFunc(void *arg) {
    auto *a = static_cast<IconThreadArgs *>(arg);
    for(const auto &iurl : a->urls) {
        if(a->state->gen.load() != a->gen) break;

        pinx::net::HttpOptions icon_opts = a->opts;
        icon_opts.timeout_ms = 5000;
        icon_opts.extra_headers.clear();

        const pinx::net::HttpResponse res = pinx::net::Get(iurl, icon_opts);
        if(res.success && !res.body.empty()) {
            BrowseTab::IconResult r;
            r.gen = a->gen;
            r.url = iurl;
            r.bytes.assign(res.body.begin(), res.body.end());
            saveIconToDisk(iurl, r.bytes);
            std::lock_guard<std::mutex> lk(a->state->mtx);
            a->state->results.push_back(std::move(r));
        }
    }
    delete a;
}

struct FetchThreadArgs {
    std::shared_ptr<BrowseTab::CatalogFetch> fetch;
};

void fetchThreadFunc(void *arg) {
    auto *a = static_cast<FetchThreadArgs *>(arg);
    auto &f = *a->fetch;

    const pinx::net::HttpResponse res = pinx::net::Get(f.url, f.opts);
    if(!res.success) {
        f.error = res.error;
        f.ready.store(true);
        delete a;
        return;
    }

    std::string body = res.body;
    if(body.size() >= 7 && body.compare(0, 7, "TINFOIL") == 0) {
        const std::optional<std::string> decrypted = pinx::catalog::TryTinfoilDecrypt(body);
        if(!decrypted) {
            f.error = "Decryption failed (missing or wrong key?).";
            f.ready.store(true);
            delete a;
            return;
        }
        body = *decrypted;
    }

    f.body    = std::move(body);
    f.success = true;
    f.ready.store(true);
    delete a;
}

} // namespace

BrowseTab::BrowseTab(pinx::app::Config *cfg,
                     pinx::download::DownloadManager *dl,
                     pinx::install::InstallManager *inst)
    : brls::List(), config(cfg), downloader(dl), installer(inst),
      icon_state_(std::make_shared<IconState>()) {
    pending_initial_load_ = true;
    showMessage("portnx/browse/loading"_i18n, "");
}

BrowseTab::~BrowseTab() {
    ++icon_state_->gen;
    if(icon_thread_running_) {
        threadWaitForExit(&icon_thread_);
        threadClose(&icon_thread_);
    }
    if(fetch_thread_running_) {
        threadWaitForExit(&fetch_thread_);
        threadClose(&fetch_thread_);
    }
}

void BrowseTab::reload() {
    nav_stack.clear();
    if(config->server_url.empty()) {
        this->clear();
        showMessage("portnx/browse/no_server"_i18n, "portnx/browse/no_server_hint"_i18n);
        return;
    }
    startFetch(config->server_url, true);
}

void BrowseTab::startFetch(const std::string &url, bool push) {
    if(fetch_thread_running_) {
        threadWaitForExit(&fetch_thread_);
        threadClose(&fetch_thread_);
        fetch_thread_running_ = false;
    }
    pending_fetch_.reset();

    icon_items_.clear();
    icon_pending_schedule_ = false;
    ++icon_state_->gen;

    if(push) nav_stack.push_back(url);

    this->clear();
    showMessage("portnx/browse/loading"_i18n, "");

    pinx::net::HttpOptions opts;
    opts.verify_tls         = false;
    opts.timeout_ms         = 20000;
    opts.connect_timeout_ms = 8000;
    opts.extra_headers.push_back("Accept: application/json");

    auto fetch   = std::make_shared<CatalogFetch>();
    fetch->url   = url;
    fetch->push  = push;
    fetch->opts  = opts;
    pending_fetch_ = fetch;

    auto *args = new FetchThreadArgs{fetch};
    const Result rc = threadCreate(&fetch_thread_, fetchThreadFunc, args,
                                   fetch_stack_, kFetchStackSize, 0x2C, -2);
    if(R_SUCCEEDED(rc)) {
        threadStart(&fetch_thread_);
        fetch_thread_running_ = true;
    } else {
        delete args;
        this->clear();
        showMessage("Error", "Failed to start network thread.");
    }
}

void BrowseTab::tickFetch() {
    if(!pending_fetch_ || !pending_fetch_->ready.load()) return;

    auto fetch = std::move(pending_fetch_);
    if(fetch_thread_running_) {
        threadWaitForExit(&fetch_thread_);
        threadClose(&fetch_thread_);
        fetch_thread_running_ = false;
    }

    this->clear();

    if(!fetch->success) {
        showMessage("portnx/browse/fetch_failed"_i18n, fetch->error);
        return;
    }

    const pinx::catalog::ParseResult parsed =
        pinx::catalog::ParseIndex(fetch->body, fetch->url);
    if(!parsed.ok) {
        showMessage("portnx/browse/invalid_index"_i18n, parsed.error);
        return;
    }

    if(nav_stack.size() > 1) {
        auto *back = new pinx::ui::GlassListItem("portnx/browse/back"_i18n);
        back->getClickEvent()->subscribe([this](brls::View *) {
            if(nav_stack.size() > 1) {
                nav_stack.pop_back();
                const std::string parent = nav_stack.back();
                nav_stack.pop_back();
                startFetch(parent, true);
            }
        });
        this->addView(back);
    }

    if(parsed.items.empty()) {
        showMessage("portnx/browse/empty"_i18n,
                    parsed.message.empty() ? "portnx/browse/no_items"_i18n : parsed.message);
        return;
    }

    for(const auto &item : parsed.items) {
        if(item.kind == pinx::catalog::EntryKind::Directory) {
            auto *li = new pinx::ui::GlassListItem("[dir] " + item.name);
            const std::string dir_url = item.url;
            li->getClickEvent()->subscribe([this, dir_url](brls::View *) {
                startFetch(dir_url, true);
            });
            this->addView(li);
        } else {
            auto *li = new pinx::ui::GlassListItem(item.name);

            std::string meta = item.format;
            if(item.size > 0)         meta += (meta.empty() ? "" : "  ") + HumanSize(item.size);
            if(!item.version.empty()) meta += "  v" + item.version;

            {
                bool installed = false;
                if(item.title_id != 0) {
                    const std::uint32_t inst_ver =
                        pinx::install::GetInstalledVersion(item.title_id);
                    if(inst_ver != UINT32_MAX) {
                        installed = true;
                        const std::string inst_str = pinx::install::VersionString(inst_ver);
                        if(!item.version.empty() && item.version != inst_str)
                            meta += "  " + brls::i18n::getStr("portnx/browse/update", inst_str);
                        else
                            meta += "  " + "portnx/browse/installed"_i18n;
                    }
                }
                if(!installed) {
                    const auto &urls = session_installed_urls_;
                    const bool session_hit = std::find(urls.begin(), urls.end(),
                                                       item.url) != urls.end();
                    meta += session_hit ? "  " + "portnx/browse/installed"_i18n
                                       : "  " + "portnx/browse/not_installed"_i18n;
                }
            }

            li->setSubLabel(meta);

            if(!item.icon_url.empty()) icon_items_[item.icon_url] = li;

            const std::string   furl    = item.url;
            const std::string   fname   = item.name;
            const std::string   ffile   = item.filename.empty() ? item.name : item.filename;
            const std::string   fhash   = item.sha256;
            const std::uint64_t fsize   = item.size;
            const std::uint64_t fverify = item.size_authoritative ? item.size : 0;

            std::string lower = ffile;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
            const bool is_installable = lower.size() >= 4 && (
                lower.substr(lower.size() - 4) == ".nsp" ||
                lower.substr(lower.size() - 4) == ".nsz" ||
                lower.substr(lower.size() - 4) == ".xci" ||
                lower.substr(lower.size() - 4) == ".xcz");

            li->getClickEvent()->subscribe(
                [this, furl, fname, ffile, fhash, fverify, fsize, is_installable](brls::View *) {
                    if(is_installable) {
                        pinx::install::InstallManager::StreamRequest req;
                        req.url                            = furl;
                        req.display_name                   = fname;
                        req.http_opts.verify_tls           = false;
                        req.http_opts.connect_timeout_ms   = 8000;
                        req.install_config.dest_storage_id = config->install_to_nand
                            ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard;
                        req.install_config.ignore_req_fw   = true;
                        req.install_config.allow_unsigned  = true;
                        req.install_config.reinstall_ncas  = config->force_reinstall;

                        installer->enqueueStream(req);
                        brls::Application::notify(brls::i18n::getStr("portnx/browse/queued", fname));
                    } else {
                        if(pinx::download::ExceedsFat32(fsize))
                            brls::Application::notify("portnx/browse/fat32_warn"_i18n);
                        pinx::download::DownloadManager::Request req;
                        req.url             = furl;
                        req.name            = fname;
                        req.dest_path       = "sdmc:/switch/PortNX/downloads/" + ffile;
                        req.expected_size   = fverify;
                        req.expected_sha256 = fhash;
                        req.verify_tls      = false;

                        if(downloader->start(req))
                            brls::Application::notify(brls::i18n::getStr("portnx/browse/downloading", fname));
                        else
                            brls::Application::notify("portnx/browse/already_dl"_i18n);
                    }
                });
            this->addView(li);
        }
    }

    if(!icon_items_.empty()) {
        icon_pending_opts_     = fetch->opts;
        icon_pending_schedule_ = true;
    }

    brls::Application::giveFocus(this);
}

void BrowseTab::frame(brls::FrameContext *ctx) {
    brls::List::frame(ctx);

    if(pending_initial_load_) {
        pending_initial_load_ = false;
        reload();
    }

    {
        const auto snap = installer->snapshot();
        if(snap.completions != last_completions_) {
            last_completions_       = snap.completions;
            session_installed_urls_ = snap.installed_urls;
            if(!fetch_thread_running_)
                reload();
        }
    }

    tickFetch();

    if(icon_pending_schedule_) {
        icon_pending_schedule_ = false;
        scheduleIcons();
    }

    tickIcons();
}

void BrowseTab::showMessage(const std::string &title, const std::string &sub) {
    auto *item = new pinx::ui::GlassListItem(title);
    if(!sub.empty()) item->setSubLabel(sub);
    this->addView(item);
}

void BrowseTab::scheduleIcons() {
    if(icon_thread_running_) {
        threadWaitForExit(&icon_thread_);
        threadClose(&icon_thread_);
        icon_thread_running_ = false;
    }

    for(auto it = icon_items_.begin(); it != icon_items_.end(); ) {
        auto mem_it = s_icon_cache.find(it->first);
        if(mem_it == s_icon_cache.end()) {
            auto disk_data = loadIconFromDisk(it->first);
            if(!disk_data.empty()) {
                s_icon_cache[it->first] = std::move(disk_data);
                mem_it = s_icon_cache.find(it->first);
            }
        }
        if(mem_it != s_icon_cache.end()) {
            it->second->setThumbnail(
                reinterpret_cast<unsigned char *>(
                    const_cast<uint8_t *>(mem_it->second.data())),
                mem_it->second.size());
            it = icon_items_.erase(it);
        } else {
            ++it;
        }
    }

    if(icon_items_.empty()) return;

    auto *args = new IconThreadArgs();
    args->state = icon_state_;
    args->gen   = icon_state_->gen.load();
    args->opts  = icon_pending_opts_;
    args->urls.reserve(icon_items_.size());
    for(const auto &[iurl, _] : icon_items_) args->urls.push_back(iurl);

    const Result rc = threadCreate(&icon_thread_, iconThreadFunc, args,
                                   icon_stack_, kIconStackSize, 0x2C, -2);
    if(R_SUCCEEDED(rc)) {
        threadStart(&icon_thread_);
        icon_thread_running_ = true;
    } else {
        delete args;
    }
}

void BrowseTab::tickIcons() {
    if(icon_items_.empty()) return;

    std::vector<IconResult> done;
    {
        std::lock_guard<std::mutex> lk(icon_state_->mtx);
        if(icon_state_->results.empty()) return;
        done = std::move(icon_state_->results);
    }

    const std::uint32_t cur_gen = icon_state_->gen.load();
    for(auto &r : done) {
        if(r.gen != cur_gen) continue;
        s_icon_cache[r.url] = r.bytes;
        const auto it = icon_items_.find(r.url);
        if(it != icon_items_.end()) {
            it->second->setThumbnail(
                reinterpret_cast<unsigned char *>(r.bytes.data()),
                r.bytes.size());
            icon_items_.erase(it);
        }
    }
}

}
