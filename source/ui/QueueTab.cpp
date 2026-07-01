#include <ui/QueueTab.hpp>

#include <algorithm>
#include <cstdio>

#include <borealis/i18n.hpp>
using namespace brls::i18n::literals;

namespace pinx::ui {

namespace {

using DState = pinx::download::DownloadManager::State;
using IState = pinx::install::InstallManager::State;

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

void drawGlassCard(NVGcontext *vg, float /*cx*/, float card_x, float card_y,
                   float card_w, float card_h, float radius = 20.0f) {
    NVGpaint bg = nvgLinearGradient(vg, card_x, card_y, card_x, card_y + card_h,
        nvgRGBA(41, 38, 65, 210), nvgRGBA(25, 22, 45, 225));
    nvgBeginPath(vg);
    nvgRoundedRect(vg, card_x, card_y, card_w, card_h, radius);
    nvgFillPaint(vg, bg);
    nvgFill(vg);

    NVGpaint sheen = nvgLinearGradient(vg, card_x, card_y, card_x, card_y + card_h * 0.5f,
        nvgRGBA(255, 255, 255, 26), nvgRGBA(255, 255, 255, 0));
    nvgBeginPath(vg);
    nvgRoundedRect(vg, card_x, card_y, card_w, card_h * 0.5f, radius);
    nvgFillPaint(vg, sheen);
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, card_x + 0.5f, card_y + 0.5f, card_w - 1.0f, card_h - 1.0f, radius - 0.5f);
    nvgStrokeColor(vg, nvgRGBA(140, 140, 160, 50));
    nvgStrokeWidth(vg, 1.0f);
    nvgStroke(vg);
}

} // namespace

QueueTab::QueueTab(pinx::download::DownloadManager *dl,
                   pinx::install::InstallManager   *inst)
    : brls::View(), downloader_(dl), installer_(inst) {
    status_text_ = "portnx/queue/idle"_i18n;
}

void QueueTab::frame(brls::FrameContext *ctx) {
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
                ? "portnx/queue/idle"_i18n
                : "portnx/queue/all_done"_i18n;
            break;

        case Phase::Downloading:
            display_name_ = dl.name.empty() ? "portnx/queue/downloading"_i18n : dl.name;
            if(dl.total > 0) {
                progress_ = static_cast<float>(dl.done) / static_cast<float>(dl.total);
                status_text_ = brls::i18n::getStr("portnx/queue/dl_progress",
                    static_cast<int>(progress_ * 100.f),
                    HumanSize(dl.done), HumanSize(dl.total));
            } else {
                progress_    = 0.0f;
                status_text_ = brls::i18n::getStr("portnx/queue/dl_bytes", HumanSize(dl.done));
            }
            break;

        case Phase::Installing:
            display_name_ = inst.display_name.empty() ? "portnx/queue/installing"_i18n : inst.display_name;
            if(inst.bytes_total > 0) {
                progress_ = static_cast<float>(inst.bytes_done) /
                            static_cast<float>(inst.bytes_total);
                status_text_ = brls::i18n::getStr("portnx/queue/inst_progress",
                    static_cast<int>(progress_ * 100.f),
                    HumanSize(inst.bytes_done), HumanSize(inst.bytes_total));
            } else {
                progress_    = 0.0f;
                status_text_ = brls::i18n::getStr("portnx/queue/inst_bytes",
                    HumanSize(inst.bytes_done));
            }
            break;

        case Phase::Done:
            progress_ = 1.0f;
            if(dl.state == DState::Done && inst.state != IState::Done)
                status_text_ = "portnx/queue/dl_done"_i18n;
            else {
                status_text_ = "portnx/queue/inst_done"_i18n;
                if(!inst.display_name.empty()) display_name_ = inst.display_name;
            }
            break;

        case Phase::Failed:
            progress_    = 0.0f;
            status_text_ = brls::i18n::getStr("portnx/queue/failed",
                inst.error.empty() ? dl.error : inst.error);
            break;
    }

    brls::View::frame(ctx);
}

void QueueTab::draw(NVGcontext *vg, int x, int y, unsigned w, unsigned h,
                    brls::Style *style, brls::FrameContext *ctx) {
    const float fx    = static_cast<float>(x);
    const float fy    = static_cast<float>(y);
    const float fw    = static_cast<float>(w);
    const float fh    = static_cast<float>(h);
    const float cx    = fx + fw * 0.5f;
    const float pad_x = fw * 0.08f;
    const float card_w = fw - 2.0f * pad_x;
    const float gap    = 16.0f;
    const float line_h = 26.0f;
    const float bar_h  = 14.0f;

    nvgSave(vg);
    nvgFontFaceId(vg, ctx->fontStash->regular);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

    const float small_fs = static_cast<float>(style->Label.smallFontSize);
    const float med_fs   = static_cast<float>(style->Label.mediumFontSize);

    const std::size_t n_done = completed_names_.size();
    const std::size_t show   = n_done > 5 ? 5 : n_done;
    const float inst_h       = n_done > 0
        ? (20.0f + line_h + static_cast<float>(show) * line_h + 16.0f)
        : 0.0f;

    const float act_h  = 20.0f + 28.0f + 24.0f + bar_h + 24.0f + med_fs + 20.0f;

    const std::size_t n_pend  = queue_names_.size();
    const std::size_t show_p  = n_pend > 5 ? 5 : n_pend;
    const float pend_h        = n_pend > 0
        ? (16.0f + line_h + static_cast<float>(show_p) * line_h + 16.0f)
        : 0.0f;

    const float total_h  = inst_h + (inst_h > 0 ? gap : 0) + act_h
                         + (pend_h > 0 ? gap : 0) + pend_h;
    float cur_y = fy + (fh - total_h) * 0.4f;
    if(cur_y < fy + 16.0f) cur_y = fy + 16.0f;

    const float card_x = fx + pad_x;

    if(n_done > 0) {
        drawGlassCard(vg, cx, card_x, cur_y, card_w, inst_h);

        float ty = cur_y + 20.0f + line_h * 0.5f;
        nvgFontSize(vg, small_fs);
        nvgFillColor(vg, a(ctx->theme->listItemValueColor));

        const std::string hdr_str = brls::i18n::getStr("portnx/queue/installed_hdr", n_done);
        nvgText(vg, cx, ty, hdr_str.c_str(), nullptr);
        ty += line_h;

        const std::size_t from = n_done > 5 ? n_done - 5 : 0;
        for(std::size_t i = from; i < n_done; ++i) {
            const std::string lbl = "✓ " + completed_names_[i];
            nvgText(vg, cx, ty, lbl.c_str(), nullptr);
            ty += line_h;
        }

        cur_y += inst_h + gap;
    }

    drawGlassCard(vg, cx, card_x, cur_y, card_w, act_h);
    {
        float ty = cur_y + 20.0f;

        nvgFontSize(vg, 28.0f);
        nvgFillColor(vg, a(ctx->theme->textColor));

        const std::string idle_str = n_done > 0 ? "portnx/queue/all_done"_i18n
                                                 : "portnx/queue/idle"_i18n;
        const char *name_str = display_name_.empty() ? idle_str.c_str() : display_name_.c_str();
        nvgText(vg, cx, ty + 14.0f, name_str, nullptr);
        ty += 28.0f + 24.0f;

        if(phase_ == Phase::Downloading || phase_ == Phase::Installing ||
           phase_ == Phase::Done) {
            const float bar_x = card_x + pad_x;
            const float bw    = card_w - 2.0f * pad_x;
            const float bar_y = ty + bar_h * 0.5f;

            nvgBeginPath(vg);
            nvgMoveTo(vg, bar_x, bar_y);
            nvgLineTo(vg, bar_x + bw, bar_y);
            nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 40));
            nvgStrokeWidth(vg, bar_h);
            nvgLineCap(vg, NVG_ROUND);
            nvgStroke(vg);

            if(progress_ > 0.001f) {
                nvgBeginPath(vg);
                nvgMoveTo(vg, bar_x, bar_y);
                nvgLineTo(vg, bar_x + bw * progress_, bar_y);
                nvgStrokeColor(vg, a(ctx->theme->listItemValueColor));
                nvgStrokeWidth(vg, bar_h);
                nvgLineCap(vg, NVG_ROUND);
                nvgStroke(vg);
            }
        }
        ty += bar_h + 24.0f;

        nvgFontSize(vg, med_fs);
        nvgFillColor(vg, a(ctx->theme->descriptionColor));
        nvgText(vg, cx, ty + med_fs * 0.5f, status_text_.c_str(), nullptr);
    }
    cur_y += act_h + gap;

    if(n_pend > 0) {
        drawGlassCard(vg, cx, card_x, cur_y, card_w, pend_h);

        float ty = cur_y + 16.0f + line_h * 0.5f;
        nvgFontSize(vg, small_fs);
        nvgFillColor(vg, a(ctx->theme->descriptionColor));

        const std::string qhdr_str = brls::i18n::getStr("portnx/queue/pending_hdr", n_pend);
        nvgText(vg, cx, ty, qhdr_str.c_str(), nullptr);
        ty += line_h;

        for(std::size_t i = 0; i < show_p; ++i) {
            nvgText(vg, cx, ty, queue_names_[i].c_str(), nullptr);
            ty += line_h;
        }
    }

    nvgRestore(vg);
}

} // namespace pinx::ui
