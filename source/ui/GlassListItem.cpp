#include <ui/GlassListItem.hpp>
#include <cmath>

namespace pinx::ui {

static constexpr int   kInset  = 6;
static constexpr float kRadius = 20.0f;

GlassListItem::GlassListItem(const std::string &label,
                             const std::string &description,
                             const std::string &subLabel)
    : brls::ListItem(label, description, subLabel) {}

void GlassListItem::layout(NVGcontext *vg, brls::Style *style, brls::FontStash *stash) {
    if(this->thumbnailView) {
        const int thumbPad  = static_cast<int>(style->List.Item.thumbnailPadding);
        const int thumbSize = static_cast<int>(this->height) - thumbPad * 2;
        if(thumbSize > 0) {
            this->thumbnailView->setBoundaries(
                this->x + thumbPad, this->y + thumbPad,
                static_cast<unsigned>(thumbSize), static_cast<unsigned>(thumbSize));
        }
    }
    brls::ListItem::layout(vg, style, stash);
}

void GlassListItem::getHighlightInsets(unsigned *top, unsigned *right,
                                        unsigned *bottom, unsigned *left) {
    *top = *bottom = *left = *right = static_cast<unsigned>(kInset);
}

void GlassListItem::draw(NVGcontext *vg, int x, int y, unsigned width,
                          unsigned height, brls::Style *style,
                          brls::FrameContext *ctx) {
    const int baseH = static_cast<int>(this->height);

    if(this->indented) {
        x     += style->List.Item.indent;
        width -= style->List.Item.indent;
    }

    const float rx = static_cast<float>(x + kInset);
    const float ry = static_cast<float>(y + kInset);
    const float rw = static_cast<float>(static_cast<int>(width) - 2 * kInset);
    const float rh = static_cast<float>(baseH - 2 * kInset);

    nvgSave(vg);

    NVGpaint bg = nvgLinearGradient(vg, rx, ry, rx, ry + rh,
        nvgRGBA(41, 38, 65, 210),
        nvgRGBA(25, 22, 45, 225));
    nvgBeginPath(vg);
    nvgRoundedRect(vg, rx, ry, rw, rh, kRadius);
    nvgFillPaint(vg, bg);
    nvgFill(vg);

    NVGpaint sheen = nvgLinearGradient(vg, rx, ry, rx, ry + rh * 0.5f,
        nvgRGBA(255, 255, 255, 26),
        nvgRGBA(255, 255, 255,  0));
    nvgBeginPath(vg);
    nvgRoundedRect(vg, rx, ry, rw, rh * 0.5f, kRadius);
    nvgFillPaint(vg, sheen);
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, rx + 0.5f, ry + 0.5f, rw - 1.0f, rh - 1.0f, kRadius - 0.5f);
    nvgStrokeColor(vg, nvgRGBA(140, 140, 160, 50));
    nvgStrokeWidth(vg, 1.0f);
    nvgStroke(vg);

    if(this->thumbnailView) this->thumbnailView->frame(ctx);
    if(this->labelView)     this->labelView->frame(ctx);
    if(this->subLabelView)  this->subLabelView->frame(ctx);
    if(this->valueView) {
        if(this->oldValueView && this->valueView->getTextAnimation() != 1.0f)
            this->oldValueView->frame(ctx);
        this->valueView->frame(ctx);
    }

    if(this->checked) {
        const unsigned radius  = style->List.Item.selectRadius;
        const int      centerX = x + static_cast<int>(width)
                               - static_cast<int>(radius)
                               - style->List.Item.padding;
        const int      centerY = y + baseH / 2;
        const float    rf      = static_cast<float>(radius);
        const int      thick   = static_cast<int>(::roundf(rf * 0.10f));

        nvgFillColor(vg, a(ctx->theme->listItemValueColor));
        nvgBeginPath(vg);
        nvgCircle(vg, centerX, centerY, rf);
        nvgFill(vg);

        nvgFillColor(vg, a(ctx->theme->backgroundColorRGB));

        nvgSave(vg);
        nvgTranslate(vg, centerX, centerY);
        nvgRotate(vg, -NVG_PI / 4.0f);
        nvgBeginPath(vg);
        nvgRect(vg, -(rf * 0.55f), 0, rf * 1.3f, thick);
        nvgFill(vg);
        nvgRestore(vg);

        nvgSave(vg);
        nvgTranslate(vg, centerX - rf * 0.65f, centerY);
        nvgRotate(vg, NVG_PI / 4.0f);
        nvgBeginPath(vg);
        nvgRect(vg, 0, -thick / 2, rf * 0.53f, thick);
        nvgFill(vg);
        nvgRestore(vg);
    }

    nvgRestore(vg);
}

} // namespace pinx::ui
