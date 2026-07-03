#include <ui/BrowseTab.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <sys/stat.h>

#include <app/I18n.hpp>
#include <catalog/IndexParser.hpp>
#include <catalog/TinfoilDecryptor.hpp>
#include <net/HttpClient.hpp>

namespace pinx::ui {
namespace {

constexpr pu::ui::Color kCellClr  = {  18,  22,  30, 255 };
constexpr pu::ui::Color kSelClr   = {  30,  60, 100, 255 };
constexpr pu::ui::Color kTextClr  = { 230, 237, 243, 255 };
constexpr pu::ui::Color kMutedClr = { 139, 148, 158, 255 };

const std::string kFontSmall  = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);
const std::string kFontMedium = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);

constexpr const char *kIconCacheDir = "sdmc:/switch/PortNX/icons";

std::unordered_map<std::string, std::vector<uint8_t>> s_icon_cache;

std::uint64_t fnv1a64(const std::string &s) {
    std::uint64_t h = 14695981039346656037ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

std::string iconCachePath(const std::string &url) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s/%016llx.jpg", kIconCacheDir,
                  static_cast<unsigned long long>(fnv1a64(url)));
    return buf;
}

std::vector<uint8_t> loadIconFromDisk(const std::string &url) {
    FILE *f = fopen(iconCachePath(url).c_str(), "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    const long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return {}; }
    std::vector<uint8_t> buf(static_cast<std::size_t>(sz));
    fread(buf.data(), 1, buf.size(), f);
    fclose(f);
    return buf;
}

void saveIconToDisk(const std::string &url, const std::vector<uint8_t> &data) {
    mkdir(kIconCacheDir, 0777);
    FILE *f = fopen(iconCachePath(url).c_str(), "wb");
    if (!f) return;
    fwrite(data.data(), 1, data.size(), f);
    fclose(f);
}

std::string HumanSize(std::uint64_t bytes) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 4) { value /= 1024.0; ++unit; }
    char buf[32];
    if (unit == 0)
        std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(bytes));
    else
        std::snprintf(buf, sizeof(buf), "%.1f %s", value, units[unit]);
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
    for (const auto &iurl : a->urls) {
        if (a->state->gen.load() != a->gen) break;

        pinx::net::HttpOptions icon_opts = a->opts;
        icon_opts.timeout_ms = 5000;
        icon_opts.extra_headers.clear();

        const pinx::net::HttpResponse res = pinx::net::Get(iurl, icon_opts);
        if (res.success && !res.body.empty()) {
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
    if (!res.success) {
        f.error = res.error;
        f.ready.store(true);
        delete a;
        return;
    }

    std::string body = res.body;
    if (body.size() >= 7 && body.compare(0, 7, "TINFOIL") == 0) {
        const std::optional<std::string> decrypted = pinx::catalog::TryTinfoilDecrypt(body);
        if (!decrypted) {
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

}

BrowseTab::BrowseTab(pinx::app::Config *cfg,
                     pinx::download::DownloadManager *dl,
                     pinx::install::InstallManager *inst)
    : config(cfg), downloader(dl), installer(inst),
      icon_state_(std::make_shared<IconState>()) {
    pending_initial_load_ = true;
}

BrowseTab::~BrowseTab() {
    ++icon_state_->gen;
    if (icon_thread_running_) {
        threadWaitForExit(&icon_thread_);
        threadClose(&icon_thread_);
    }
    if (fetch_thread_running_) {
        threadWaitForExit(&fetch_thread_);
        threadClose(&fetch_thread_);
    }
}

void BrowseTab::AddElementsTo(pu::ui::Layout *layout) {
    status_tb_ = pu::ui::elm::TextBlock::New(kGridX + 50, kGridY + 300,
                                              pinx::i18n::tr("browse.loading"));
    status_tb_->SetColor(kMutedClr);
    status_tb_->SetFont(kFontMedium);
    layout->Add(status_tb_);

    for (s32 i = 0; i < kMaxCells; ++i) {
        const s32 col = i % kGridCols;
        const s32 row = i / kGridCols;
        const s32 cx  = kGridX + col * kCellW;
        const s32 cy  = kGridY + row * kCellH;
        const s32 ix  = cx + (kCellW - kIconSz) / 2;
        const s32 iy  = cy + 12;

        cell_bg_[i] = pu::ui::elm::Rectangle::New(cx, cy, kCellW - 4, kCellH - 4, kCellClr, 10);
        layout->Add(cell_bg_[i]);

        pu::sdl2::TextureHandle::Ref empty_th = nullptr;
        cell_img_[i] = pu::ui::elm::Image::New(ix, iy, empty_th);
        cell_img_[i]->SetWidth(kIconSz);
        cell_img_[i]->SetHeight(kIconSz);
        cell_img_[i]->SetVisible(false);
        layout->Add(cell_img_[i]);

        cell_lbl_[i] = pu::ui::elm::TextBlock::New(cx + 8, iy + kIconSz + 8, "");
        cell_lbl_[i]->SetColor(kTextClr);
        cell_lbl_[i]->SetFont(kFontSmall);
        layout->Add(cell_lbl_[i]);

        cell_meta_[i] = pu::ui::elm::TextBlock::New(cx + 8, iy + kIconSz + 32, "");
        cell_meta_[i]->SetColor(kMutedClr);
        cell_meta_[i]->SetFont(kFontSmall);
        layout->Add(cell_meta_[i]);
    }

    page_tb_ = pu::ui::elm::TextBlock::New(kGridX + 400, kGridY + kGridRows * kCellH + 20, "");
    page_tb_->SetColor(kMutedClr);
    page_tb_->SetFont(kFontSmall);
    layout->Add(page_tb_);

    browse_hint_tb_ = pu::ui::elm::TextBlock::New(kGridX, kGridY + kGridRows * kCellH + 50, "");
    browse_hint_tb_->SetColor(kMutedClr);
    browse_hint_tb_->SetFont(kFontSmall);
    layout->Add(browse_hint_tb_);
    RefreshStrings();

    toast_bg_ = pu::ui::elm::Rectangle::New(1230, 28, 660, 64, pu::ui::Color{20, 130, 30, 230}, 12);
    toast_bg_->SetVisible(false);
    layout->Add(toast_bg_);

    toast_tb_ = pu::ui::elm::TextBlock::New(1250, 48, "");
    toast_tb_->SetColor(pu::ui::Color{210, 255, 215, 255});
    toast_tb_->SetFont(kFontSmall);
    toast_tb_->SetVisible(false);
    layout->Add(toast_tb_);

    Hide();
}

void BrowseTab::RefreshStrings() {
    if (browse_hint_tb_) {
        const std::string hint =
            std::string("\xEE\x82\xA0 ") + pinx::i18n::tr("hints.queue_open") +
            "   \xEE\x82\xA1 " + pinx::i18n::tr("hints.back") +
            "   \xEE\x82\xA3 " + pinx::i18n::tr("hints.view") +
            "   L/R: " + pinx::i18n::tr("hints.page");
        browse_hint_tb_->SetText(hint);
    }
}

void BrowseTab::Show() {
    visible_ = true;
    status_tb_->SetVisible(true);
    for (s32 i = 0; i < kMaxCells; ++i) {
        cell_bg_[i]->SetVisible(true);
        cell_lbl_[i]->SetVisible(true);
        cell_meta_[i]->SetVisible(true);
    }
    page_tb_->SetVisible(true);
    browse_hint_tb_->SetVisible(true);
    UpdateGridCells();
}

void BrowseTab::Hide() {
    visible_ = false;
    if (status_tb_)      status_tb_->SetVisible(false);
    if (page_tb_)        page_tb_->SetVisible(false);
    if (browse_hint_tb_) browse_hint_tb_->SetVisible(false);
    for (s32 i = 0; i < kMaxCells; ++i) {
        if (cell_bg_[i])   cell_bg_[i]->SetVisible(false);
        if (cell_img_[i])  cell_img_[i]->SetVisible(false);
        if (cell_lbl_[i])  cell_lbl_[i]->SetVisible(false);
        if (cell_meta_[i]) cell_meta_[i]->SetVisible(false);
    }
}

void BrowseTab::Poll() {
    if (toast_countdown_ > 0) {
        --toast_countdown_;
        if (toast_countdown_ == 0) {
            toast_bg_->SetVisible(false);
            toast_tb_->SetVisible(false);
        }
    }

    if (!visible_) return;

    if (pending_initial_load_) {
        pending_initial_load_ = false;
        showMessage("Loading...");
        reload();
    }

    tickFetch();

    if (++sync_frame_ >= 60) {
        sync_frame_ = 0;
        SyncQueuedUrls();
    }

    if (icon_pending_schedule_) {
        icon_pending_schedule_ = false;
        scheduleIcons();
    }

    tickIcons();
}

void BrowseTab::reload() {
    queued_urls_.clear();
    entries_.clear();
    nav_stack.clear();
    grid_page_ = 0;
    grid_sel_  = 0;
    is_loading_ = true;
    if (status_tb_) status_tb_->SetText(pinx::i18n::tr("browse.loading"));
    UpdateGridCells();

    if (config->server_url.empty()) {
        is_loading_ = false;
        if (status_tb_) status_tb_->SetText(pinx::i18n::tr("browse.no_server"));
        UpdateGridCells();
        return;
    }
    startFetch(config->server_url, true);
}

void BrowseTab::startFetch(const std::string &url, bool push) {
    if (fetch_thread_running_) {
        threadWaitForExit(&fetch_thread_);
        threadClose(&fetch_thread_);
        fetch_thread_running_ = false;
    }
    pending_fetch_.reset();

    icon_items_.clear();
    icon_pending_schedule_ = false;
    ++icon_state_->gen;

    if (push) nav_stack.push_back(url);

    entries_.clear();
    grid_page_ = 0;
    grid_sel_  = 0;
    is_loading_ = true;
    showMessage(pinx::i18n::tr("browse.loading"));

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
    if (R_SUCCEEDED(rc)) {
        threadStart(&fetch_thread_);
        fetch_thread_running_ = true;
    } else {
        delete args;
        is_loading_ = false;
        showMessage(pinx::i18n::tr("browse.thread_error"));
    }
}

void BrowseTab::tickFetch() {
    if (!pending_fetch_ || !pending_fetch_->ready.load()) return;

    auto fetch = std::move(pending_fetch_);
    if (fetch_thread_running_) {
        threadWaitForExit(&fetch_thread_);
        threadClose(&fetch_thread_);
        fetch_thread_running_ = false;
    }

    if (!fetch->success) {
        is_loading_ = false;
        showMessage(pinx::i18n::tr("browse.fetch_failed") + fetch->error);
        return;
    }

    const pinx::catalog::ParseResult parsed =
        pinx::catalog::ParseIndex(fetch->body, fetch->url);
    if (!parsed.ok) {
        is_loading_ = false;
        showMessage(pinx::i18n::tr("browse.invalid_index") + parsed.error);
        return;
    }

    entries_.clear();
    grid_page_ = 0;
    grid_sel_  = 0;

    if (nav_stack.size() > 1) {
        GridEntry back;
        back.name      = pinx::i18n::tr("browse.back_name");
        back.meta_line = pinx::i18n::tr("browse.back_meta");
        back.is_back   = true;
        entries_.push_back(back);
    }

    if (parsed.items.empty()) {
        const std::string msg = parsed.message.empty() ? pinx::i18n::tr("browse.no_items") : parsed.message;
        is_loading_ = false;
        showMessage(msg);
        UpdateGridCells();
        return;
    }

    for (const auto &item : parsed.items) {
        GridEntry e;
        e.name      = item.name;
        e.url       = item.url;
        e.icon_url  = item.icon_url;
        e.filename  = item.filename.empty() ? item.name : item.filename;
        e.format    = item.format;
        e.version   = item.version;
        e.sha256    = item.sha256;
        e.size      = item.size;
        e.title_id  = item.title_id;
        e.size_authoritative = item.size_authoritative;
        e.is_directory = (item.kind == pinx::catalog::EntryKind::Directory);

        if (!e.is_directory) {
            std::string lower = e.filename;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
            e.is_installable = lower.size() >= 4 && (
                lower.substr(lower.size() - 4) == ".nsp" ||
                lower.substr(lower.size() - 4) == ".nsz" ||
                lower.substr(lower.size() - 4) == ".xci" ||
                lower.substr(lower.size() - 4) == ".xcz");
        }

        if (e.is_directory) {
            e.meta_line = pinx::i18n::tr("browse.folder");
        } else {
            e.meta_line = e.format;
            if (e.size > 0)
                e.meta_line += (e.meta_line.empty() ? "" : " ") + HumanSize(e.size);
            if (!e.version.empty())
                e.meta_line += (e.meta_line.empty() ? "" : "  v") + e.version;

        }

        if (!e.icon_url.empty()) {
            icon_items_[e.icon_url] = entries_.size();
        }

        entries_.push_back(std::move(e));
    }

    is_loading_ = false;
    UpdateGridCells();

    if (!icon_items_.empty()) {
        icon_pending_opts_     = fetch->opts;
        icon_pending_schedule_ = true;
    }
}

void BrowseTab::showMessage(const std::string &msg) {
    if (status_tb_) status_tb_->SetText(msg);
    UpdateGridCells();
}

void BrowseTab::UpdateGridCells() {
    if (!status_tb_) return;

    const s32 page_sz = PageSize();
    const s32 n       = static_cast<s32>(entries_.size());
    const s32 base    = grid_page_ * page_sz;

    for (s32 i = 0; i < kMaxCells; ++i) {
        if (!cell_bg_[i]) continue;

        s32 bg_x, bg_y, bg_w, bg_h;
        s32 img_x, img_y, img_sz;
        s32 lbl_x, lbl_y;
        s32 meta_x, meta_y;

        if (view_mode_ == ViewMode::Grid) {
            const s32 col = i % kGridCols;
            const s32 row = i / kGridCols;
            bg_x  = kGridX + col * kCellW;
            bg_y  = kGridY + row * kCellH;
            bg_w  = kCellW - 4;  bg_h  = kCellH - 4;
            img_sz = kIconSz;
            img_x  = bg_x + (kCellW - kIconSz) / 2;
            img_y  = bg_y + 12;
            lbl_x  = bg_x + 8;  lbl_y  = img_y + kIconSz + 8;
            meta_x = bg_x + 8;  meta_y = img_y + kIconSz + 32;
        } else {
            bg_x  = kListX;
            bg_y  = kListY + i * kListH;
            bg_w  = kListW;      bg_h  = kListH - 4;
            img_sz = kListIconSz;
            img_x  = bg_x + 8;
            img_y  = bg_y + (kListH - kListIconSz) / 2;
            lbl_x  = bg_x + kListIconSz + 20;  lbl_y  = bg_y + 16;
            meta_x = bg_x + kListIconSz + 20;  meta_y = bg_y + 48;
        }

        cell_bg_[i]->SetX(bg_x);    cell_bg_[i]->SetY(bg_y);
        cell_bg_[i]->SetWidth(bg_w); cell_bg_[i]->SetHeight(bg_h);
        cell_img_[i]->SetX(img_x);  cell_img_[i]->SetY(img_y);
        cell_lbl_[i]->SetX(lbl_x);  cell_lbl_[i]->SetY(lbl_y);
        cell_meta_[i]->SetX(meta_x); cell_meta_[i]->SetY(meta_y);

        const s32 abs = base + i;
        const bool has = !is_loading_ && i < page_sz && abs < n;

        cell_bg_[i]->SetVisible(visible_ && has);
        cell_lbl_[i]->SetVisible(visible_ && has);
        cell_meta_[i]->SetVisible(visible_ && has);

        if (!has) {
            cell_img_[i]->SetVisible(false);
            continue;
        }

        const auto &e = entries_[abs];

        cell_bg_[i]->SetColor(i == grid_sel_ ? kSelClr : kCellClr);

        if (e.icon_tex) {
            cell_img_[i]->SetImage(e.icon_tex);
            cell_img_[i]->SetWidth(img_sz);
            cell_img_[i]->SetHeight(img_sz);
            cell_img_[i]->SetVisible(visible_);
        } else {
            cell_img_[i]->SetVisible(false);
        }

        const s32 max_chars = (view_mode_ == ViewMode::Grid) ? 32 : 50;
        std::string lbl = e.name;
        if (static_cast<s32>(lbl.size()) > max_chars)
            lbl = lbl.substr(0, max_chars - 3) + "...";
        if (cell_lbl_[i]->GetText() != lbl) cell_lbl_[i]->SetText(lbl);

        if (cell_meta_[i]->GetText() != e.meta_line)
            cell_meta_[i]->SetText(e.meta_line);
    }

    status_tb_->SetVisible(visible_ && (is_loading_ || (n == 0 && !is_loading_)));

    const s32 hint_y = (view_mode_ == ViewMode::Grid)
        ? kGridY + kGridRows * kCellH + 20
        : kListY + kListRows * kListH + 10;

    if (visible_ && !is_loading_ && n > 0) {
        const s32 total_pages = (n + page_sz - 1) / page_sz;
        const std::string page_str = pinx::i18n::trf("browse.page_fmt", {
            std::to_string(grid_page_ + 1),
            std::to_string(total_pages),
            std::to_string(n)
        });
        page_tb_->SetText(page_str);
        page_tb_->SetY(hint_y);
        page_tb_->SetVisible(true);
        browse_hint_tb_->SetY(hint_y + 30);
        browse_hint_tb_->SetVisible(true);
    } else {
        if (page_tb_)        page_tb_->SetVisible(false);
        if (browse_hint_tb_) browse_hint_tb_->SetVisible(false);
    }
}

bool BrowseTab::HandleInput(u64 kd) {
    if (!visible_) return false;

    const s32 n    = static_cast<s32>(entries_.size());
    const s32 base = grid_page_ * PageSize();

    if (kd & HidNpadButton_B) {
        if (nav_stack.size() > 1) {
            nav_stack.pop_back();
            const std::string parent = nav_stack.back();
            nav_stack.pop_back();
            startFetch(parent, true);
            return true;
        }
        return false; // not consumed — let MainLayout handle B → home
    }

    if (kd & HidNpadButton_A) {
        ActivateSelected();
        return true;
    }

    if (kd & HidNpadButton_Y) {
        view_mode_ = (view_mode_ == ViewMode::Grid) ? ViewMode::List : ViewMode::Grid;
        grid_page_ = 0;
        grid_sel_  = 0;
        UpdateGridCells();
        return true;
    }

    const s32 page_sz = PageSize();
    bool any = false;

    if (view_mode_ == ViewMode::List) {
        if ((kd & HidNpadButton_Up) || (kd & HidNpadButton_StickLUp) || (kd & HidNpadButton_StickRUp)) {
            if (grid_sel_ > 0) { --grid_sel_; any = true; }
            else if (grid_page_ > 0) { --grid_page_; grid_sel_ = page_sz - 1; any = true; }
        }
        if ((kd & HidNpadButton_Down) || (kd & HidNpadButton_StickLDown) || (kd & HidNpadButton_StickRDown)) {
            if (grid_sel_ + 1 < page_sz && base + grid_sel_ + 1 < n) { ++grid_sel_; any = true; }
            else if (base + page_sz < n) { ++grid_page_; grid_sel_ = 0; any = true; }
        }
        if ((kd & HidNpadButton_Left) || (kd & HidNpadButton_StickLLeft) || (kd & HidNpadButton_StickRLeft)) {
            if (grid_page_ > 0) { --grid_page_; grid_sel_ = 0; any = true; }
        }
        if ((kd & HidNpadButton_Right) || (kd & HidNpadButton_StickLRight) || (kd & HidNpadButton_StickRRight)) {
            if (base + page_sz < n) { ++grid_page_; grid_sel_ = 0; any = true; }
        }
    } else {
        if ((kd & HidNpadButton_Left) || (kd & HidNpadButton_StickLLeft) || (kd & HidNpadButton_StickRLeft)) {
            if (grid_sel_ % kGridCols > 0) { --grid_sel_; any = true; }
            else if (grid_page_ > 0)       { --grid_page_; grid_sel_ = kGridPage - 1; any = true; }
        }
        if ((kd & HidNpadButton_Right) || (kd & HidNpadButton_StickLRight) || (kd & HidNpadButton_StickRRight)) {
            if (grid_sel_ % kGridCols < kGridCols - 1 && base + grid_sel_ + 1 < n)
                { ++grid_sel_; any = true; }
            else if (base + kGridPage < n)
                { ++grid_page_; grid_sel_ = 0; any = true; }
        }
        if ((kd & HidNpadButton_Up) || (kd & HidNpadButton_StickLUp) || (kd & HidNpadButton_StickRUp)) {
            if (grid_sel_ >= kGridCols) { grid_sel_ -= kGridCols; any = true; }
        }
        if ((kd & HidNpadButton_Down) || (kd & HidNpadButton_StickLDown) || (kd & HidNpadButton_StickRDown)) {
            if (grid_sel_ + kGridCols < kGridPage && base + grid_sel_ + kGridCols < n)
                { grid_sel_ += kGridCols; any = true; }
        }
    }

    bool lr = false;
    if (kd & HidNpadButton_L) {
        lr = true;
        if (grid_page_ > 0) { --grid_page_; grid_sel_ = 0; any = true; }
    }
    if (kd & HidNpadButton_R) {
        lr = true;
        if (base + page_sz < n) { ++grid_page_; grid_sel_ = 0; any = true; }
    }

    if (any) UpdateGridCells();
    return any || lr;
}

void BrowseTab::SyncQueuedUrls() {
    const auto inst = installer->snapshot();
    const auto dl   = downloader->snapshot();

    std::unordered_set<std::string> active;
    if (!inst.active_url.empty()) active.insert(inst.active_url);
    for (const auto &u : inst.queue_urls) active.insert(u);
    for (const auto &u : inst.installed_urls) active.insert(u);

    const bool dl_active = dl.state == pinx::download::DownloadManager::State::Running ||
                           dl.state == pinx::download::DownloadManager::State::Verifying;
    if (dl_active && !dl.active_url.empty()) active.insert(dl.active_url);

    for (auto it = queued_urls_.begin(); it != queued_urls_.end(); ) {
        it = active.count(*it) ? std::next(it) : queued_urls_.erase(it);
    }
}

void BrowseTab::HandleTouch(const pu::ui::TouchPoint &tp) {
    if (view_mode_ == ViewMode::Grid) {
        for (s32 ci = 0; ci < kGridPage; ++ci) {
            const s32 col = ci % kGridCols;
            const s32 row = ci / kGridCols;
            if (tp.HitsRegion(kGridX + col * kCellW, kGridY + row * kCellH, kCellW, kCellH)) {
                TouchCell(ci);
                return;
            }
        }
    } else {
        for (s32 ci = 0; ci < kListRows; ++ci) {
            if (tp.HitsRegion(kListX, kListY + ci * kListH, kListW, kListH)) {
                TouchCell(ci);
                return;
            }
        }
    }
}

void BrowseTab::TouchCell(s32 slot) {
    const s32 base = grid_page_ * PageSize();
    if (base + slot >= static_cast<s32>(entries_.size())) return;
    if (grid_sel_ == slot) {
        ActivateSelected();
    } else {
        grid_sel_ = slot;
        UpdateGridCells();
    }
}

void BrowseTab::ActivateSelected() {
    const s32 abs = grid_page_ * PageSize() + grid_sel_;
    if (abs < 0 || abs >= static_cast<s32>(entries_.size())) return;
    const auto &e = entries_[abs];

    if (e.is_back) {
        if (nav_stack.size() > 1) {
            nav_stack.pop_back();
            const std::string parent = nav_stack.back();
            nav_stack.pop_back();
            startFetch(parent, true);
        }
        return;
    }
    if (e.is_directory) {
        startFetch(e.url, true);
        return;
    }
    if (queued_urls_.count(e.url)) return; // duplicate prevention

    if (e.is_installable) {
        pinx::install::InstallManager::StreamRequest req;
        req.url                            = e.url;
        req.display_name                   = e.name;
        req.http_opts.verify_tls           = false;
        req.http_opts.connect_timeout_ms   = 8000;
        req.install_config.dest_storage_id = config->install_to_nand
            ? NcmStorageId_BuiltInUser : NcmStorageId_SdCard;
        req.install_config.ignore_req_fw   = true;
        req.install_config.reinstall_ncas  = config->force_reinstall;
        installer->enqueueStream(req);
    } else {
        mkdir("sdmc:/switch/PortNX/downloads", 0777);
        pinx::download::DownloadManager::Request req;
        req.url           = e.url;
        req.name          = e.name;
        req.dest_path     = "sdmc:/switch/PortNX/downloads/" + e.filename;
        req.expected_size = e.size_authoritative ? e.size : 0;
        req.expected_sha256 = e.sha256;
        req.verify_tls    = false;
        downloader->start(req);
    }
    queued_urls_.insert(e.url);
    ShowToast(e.name);
}

void BrowseTab::ShowToast(const std::string &name) {
    std::string n = name.size() > 26 ? name.substr(0, 23) + "..." : name;
    toast_tb_->SetText(std::string("\xEE\x82\xA0 ") + pinx::i18n::tr("browse.toast_queued") + n);
    toast_bg_->SetVisible(true);
    toast_tb_->SetVisible(true);
    toast_countdown_ = 180;
}

void BrowseTab::scheduleIcons() {
    if (icon_thread_running_) {
        threadWaitForExit(&icon_thread_);
        threadClose(&icon_thread_);
        icon_thread_running_ = false;
    }

    for (auto it = icon_items_.begin(); it != icon_items_.end(); ) {
        auto mem_it = s_icon_cache.find(it->first);
        if (mem_it == s_icon_cache.end()) {
            auto disk_data = loadIconFromDisk(it->first);
            if (!disk_data.empty()) {
                s_icon_cache[it->first] = std::move(disk_data);
                mem_it = s_icon_cache.find(it->first);
            }
        }
        if (mem_it != s_icon_cache.end()) {
            const auto &bytes = mem_it->second;
            if (!bytes.empty()) {
                auto raw = pu::ui::render::LoadImageFromBuffer(bytes.data(), bytes.size());
                if (raw != nullptr) {
                    auto th = pu::sdl2::TextureHandle::New(raw);
                    const std::size_t idx = it->second;
                    if (idx < entries_.size()) {
                        entries_[idx].icon_tex = th;
                    }
                }
            }
            it = icon_items_.erase(it);
        } else {
            ++it;
        }
    }

    if (!icon_items_.empty()) {
        UpdateGridCells();
    }

    if (icon_items_.empty()) return;

    auto *args = new IconThreadArgs();
    args->state = icon_state_;
    args->gen   = icon_state_->gen.load();
    args->opts  = icon_pending_opts_;
    args->urls.reserve(icon_items_.size());
    for (const auto &[iurl, _] : icon_items_) args->urls.push_back(iurl);

    const Result rc = threadCreate(&icon_thread_, iconThreadFunc, args,
                                   icon_stack_, kIconStackSize, 0x2C, -2);
    if (R_SUCCEEDED(rc)) {
        threadStart(&icon_thread_);
        icon_thread_running_ = true;
    } else {
        delete args;
    }
}

void BrowseTab::tickIcons() {
    if (icon_items_.empty()) return;

    std::vector<IconResult> done;
    {
        std::lock_guard<std::mutex> lk(icon_state_->mtx);
        if (icon_state_->results.empty()) return;
        done = std::move(icon_state_->results);
    }

    const std::uint32_t cur_gen = icon_state_->gen.load();
    bool any = false;
    for (auto &r : done) {
        if (r.gen != cur_gen) continue;
        s_icon_cache[r.url] = r.bytes;
        auto it = icon_items_.find(r.url);
        if (it != icon_items_.end()) {
            if (!r.bytes.empty()) {
                auto raw = pu::ui::render::LoadImageFromBuffer(r.bytes.data(), r.bytes.size());
                if (raw != nullptr) {
                    auto th = pu::sdl2::TextureHandle::New(raw);
                    const std::size_t idx = it->second;
                    if (idx < entries_.size()) {
                        entries_[idx].icon_tex = th;
                        any = true;
                    }
                }
            }
            icon_items_.erase(it);
        }
    }
    if (any) UpdateGridCells();
}

}
