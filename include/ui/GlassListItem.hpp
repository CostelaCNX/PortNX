#pragma once

#include <borealis.hpp>

namespace pinx::ui {

// GlassListItem — drop-in replacement for brls::ListItem with a frosted-glass
// card aesthetic (NVG rounded rect + gradient fill + white highlight border).
// All functionality (thumbnail, sublabel, value, click event) is inherited.
class GlassListItem : public brls::ListItem {
    public:
        explicit GlassListItem(const std::string &label,
                               const std::string &description = "",
                               const std::string &subLabel    = "");

        void draw(NVGcontext *vg, int x, int y, unsigned width, unsigned height,
                  brls::Style *style, brls::FrameContext *ctx) override;

        void layout(NVGcontext *vg, brls::Style *style,
                    brls::FontStash *stash) override;

        void getHighlightInsets(unsigned *top, unsigned *right,
                                unsigned *bottom, unsigned *left) override;
};

} // namespace pinx::ui
