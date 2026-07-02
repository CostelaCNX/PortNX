#include <ui/QueueTab.hpp>
#include <app/I18n.hpp>

#include <algorithm>
#include <cstdio>

namespace pinx::ui {

namespace {

using DState = pinx::download::DownloadManager::State;
using IState = pinx::install::InstallManager::State;

static constexpr s32 kCX = 60;
static constexpr s32 kCY = 100;
static constexpr s32 kCW = 1800;

static constexpr pu::ui::Color kTextPrimary = { 230, 237, 243, 255 };
static constexpr pu::ui::Color kTextMuted   = { 139, 148, 158, 255 };
static constexpr pu::ui::Color kAccent      = {  31, 111, 235, 255 };
static constexpr pu::ui::Color kSuccess     = {  63, 185, 80,  255 };
static constexpr pu::ui::Color kError       = { 248,  81,  73, 255 };

std::string HumanSize(std::uint64_t bytes) {
    const char *units[] = {"B", "KB", "MB", "GB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while(v >= 1024.0 && u < 3) { v /= 1024.0; ++u; }
    char buf[32];
    if(u == 0) std::snprintf(buf, sizeof(buf), "%llu B",
                              static_cast<unsigned long long>(bytes));
    else        std::snprintf(buf, sizeof(buf), "%.1f %s", v, units[u]);
    return buf;
}

} // namespace

QueueTab::QueueTab(pinx::download::DownloadManager *dl,
                   pinx::install::InstallManager   *inst)
    : downloader_(dl), installer_(inst) {}

void QueueTab::AddElementsTo(pu::ui::Layout *layout) {
    completed_text_ = pu::ui::elm::TextBlock::New(kCX + 30, kCY + 36, "");
    completed_text_->SetColor(kSuccess);
    completed_text_->SetFont(pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small));
    layout->Add(completed_text_);

    display_name_text_ = pu::ui::elm::TextBlock::New(kCX + 30, kCY + 300, "");
    display_name_text_->SetColor(kTextPrimary);
    display_name_text_->SetFont(pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Large));
    layout->Add(display_name_text_);

    progress_bar_ = pu::ui::elm::ProgressBar::New(kCX + 60, kCY + 405, kCW - 120, 36, 1.0);
    progress_bar_->SetProgressColor(kAccent);
    layout->Add(progress_bar_);

    status_text_elm_ = pu::ui::elm::TextBlock::New(kCX + 30, kCY + 474,
                                                     pinx::i18n::tr("queue.idle"));
    status_text_elm_->SetColor(kTextMuted);
    status_text_elm_->SetFont(pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium));
    layout->Add(status_text_elm_);

    queue_text_ = pu::ui::elm::TextBlock::New(kCX + 30, kCY + 630, "");
    queue_text_->SetColor(kTextMuted);
    queue_text_->SetFont(pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small));
    layout->Add(queue_text_);

    Hide();
}

void QueueTab::Show() {
    visible_ = true;
    completed_text_->SetVisible(true);
    display_name_text_->SetVisible(true);
    progress_bar_->SetVisible(true);
    status_text_elm_->SetVisible(true);
    queue_text_->SetVisible(true);
}

void QueueTab::Hide() {
    visible_ = false;
    completed_text_->SetVisible(false);
    display_name_text_->SetVisible(false);
    progress_bar_->SetVisible(false);
    status_text_elm_->SetVisible(false);
    queue_text_->SetVisible(false);
}

void QueueTab::Poll() {
    const auto dl   = downloader_->snapshot();
    const auto inst = installer_->snapshot();

    completed_names_ = inst.completed_names;

    Phase new_phase = Phase::Idle;
    if(dl.state == DState::Running || dl.state == DState::Verifying)
        new_phase = Phase::Downloading;
    else if(inst.state == IState::Running)
        new_phase = Phase::Installing;
    else if(inst.state == IState::Done || dl.state == DState::Done)
        new_phase = Phase::Done;
    else if(inst.state == IState::Failed ||
            dl.state   == DState::Failed ||
            dl.state   == DState::Canceled)
        new_phase = Phase::Failed;

    phase_       = new_phase;
    queue_names_ = inst.queue_names;

    switch(phase_) {
        case Phase::Idle:
            display_name_ = "";
            progress_     = 0.0f;
            status_text_  = completed_names_.empty()
                ? pinx::i18n::tr("queue.idle")
                : pinx::i18n::tr("queue.all_done");
            break;

        case Phase::Downloading:
            display_name_ = dl.name.empty() ? pinx::i18n::tr("queue.downloading") : dl.name;
            if(dl.total > 0) {
                progress_ = static_cast<float>(dl.done) / static_cast<float>(dl.total);
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%d%%  %s / %s",
                    static_cast<int>(progress_ * 100.f),
                    HumanSize(dl.done).c_str(), HumanSize(dl.total).c_str());
                status_text_ = buf;
            } else {
                progress_    = 0.0f;
                status_text_ = HumanSize(dl.done);
            }
            break;

        case Phase::Installing:
            display_name_ = inst.display_name.empty() ? pinx::i18n::tr("queue.installing") : inst.display_name;
            if(inst.bytes_total > 0) {
                progress_ = static_cast<float>(inst.bytes_done) /
                            static_cast<float>(inst.bytes_total);
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%d%%  %s / %s",
                    static_cast<int>(progress_ * 100.f),
                    HumanSize(inst.bytes_done).c_str(), HumanSize(inst.bytes_total).c_str());
                status_text_ = buf;
            } else {
                progress_    = 0.0f;
                status_text_ = HumanSize(inst.bytes_done);
            }
            break;

        case Phase::Done:
            progress_ = 1.0f;
            if(dl.state == DState::Done && inst.state != IState::Done)
                status_text_ = pinx::i18n::tr("queue.dl_done");
            else {
                status_text_ = pinx::i18n::tr("queue.inst_done");
                if(!inst.display_name.empty()) display_name_ = inst.display_name;
            }
            break;

        case Phase::Failed:
            progress_    = 0.0f;
            status_text_ = pinx::i18n::tr("queue.failed") + (inst.error.empty() ? dl.error : inst.error);
            break;
    }

    if(visible_) UpdateElements();
}

void QueueTab::UpdateElements() {
    // Completed installs
    {
        std::string text;
        const std::size_t n = completed_names_.size();
        if(n > 0) {
            char hdr[64];
            const std::string hdr_s = pinx::i18n::trf("queue.installed_hdr", {std::to_string(n)});
            std::snprintf(hdr, sizeof(hdr), "%s", hdr_s.c_str());
            text = hdr;
            const std::size_t from = n > 5 ? n - 5 : 0;
            for(std::size_t i = from; i < n; ++i)
                text += "  ✓ " + completed_names_[i];
        }
        SetText(completed_text_, text);
    }

    // Current job
    SetText(display_name_text_, display_name_);

    // Progress bar
    progress_bar_->SetProgress(static_cast<double>(progress_));

    // Status text color (changes rarely; SetColor re-renders texture so track manually)
    {
        const pu::ui::Color clr = (phase_ == Phase::Done)   ? kSuccess :
                                   (phase_ == Phase::Failed) ? kError   : kTextMuted;
        const auto cur = status_text_elm_->GetColor();
        if(cur.r != clr.r || cur.g != clr.g || cur.b != clr.b || cur.a != clr.a)
            status_text_elm_->SetColor(clr);
    }
    SetText(status_text_elm_, status_text_);

    // Pending queue
    {
        std::string text;
        const std::size_t n = queue_names_.size();
        if(n > 0) {
            char hdr[64];
            const std::string hdr_s2 = pinx::i18n::trf("queue.pending_hdr", {std::to_string(n)});
            std::snprintf(hdr, sizeof(hdr), "%s", hdr_s2.c_str());
            text = hdr;
            const std::size_t show = n > 5 ? 5 : n;
            for(std::size_t i = 0; i < show; ++i)
                text += "  " + queue_names_[i];
        }
        SetText(queue_text_, text);
    }
}

void QueueTab::RefreshStrings() {
    if (status_text_elm_ && phase_ == Phase::Idle)
        SetText(status_text_elm_, pinx::i18n::tr("queue.idle"));
}

void QueueTab::SetText(pu::ui::elm::TextBlock::Ref &tb, const std::string &text) {
    if(tb->GetText() != text) tb->SetText(text);
}

} // namespace pinx::ui
