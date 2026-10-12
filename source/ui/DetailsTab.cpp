#include <ui/DetailsTab.hpp>
#include <app/Format.hpp>
#include <app/I18n.hpp>

#include <cctype>
#include <cstdio>
#include <sstream>
#include <vector>

namespace pinx::ui {
namespace {

constexpr pu::ui::Color kPanelClr  = {  18,  22,  30, 255 };
constexpr pu::ui::Color kActionClr = { 196,   0,   0, 255 };
constexpr pu::ui::Color kIdleClr   = {  40,  44,  54, 255 };
constexpr pu::ui::Color kTextClr   = { 230, 237, 243, 255 };
constexpr pu::ui::Color kMutedClr  = { 139, 148, 158, 255 };

const std::string kFontLarge  = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Large);
const std::string kFontMedium = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Medium);
const std::string kFontSmall  = pu::ui::GetDefaultFont(pu::ui::DefaultFontSize::Small);

constexpr s32 kGap = 24;

std::size_t Utf8Next(const std::string &s, std::size_t i) {
    ++i;
    while(i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
    return i;
}

// TextBlock renders '\n' but never wraps on its own, so lines are broken here by
// measured width. Words wider than a line are split on UTF-8 boundaries so a
// long URL or name never runs off the panel.
std::string Wrap(const std::string &font, const std::string &text, s32 max_w, std::size_t max_lines) {
    std::vector<std::string> lines;
    std::istringstream paragraphs(text);
    std::string para;
    auto fits = [&](const std::string &t) { return pu::ui::render::GetTextWidth(font, t) <= max_w; };

    while(std::getline(paragraphs, para)) {
        std::istringstream words(para);
        std::string word, line;
        while(words >> word) {
            const std::string candidate = line.empty() ? word : line + " " + word;
            if(fits(candidate)) { line = candidate; continue; }
            if(!line.empty()) { lines.push_back(line); line.clear(); }
            while(!fits(word)) {
                std::size_t cut = 0;
                for(std::size_t next = Utf8Next(word, 0); next <= word.size() && fits(word.substr(0, next));
                    next = Utf8Next(word, next)) {
                    cut = next;
                }
                if(cut == 0) cut = Utf8Next(word, 0);
                lines.push_back(word.substr(0, cut));
                word.erase(0, cut);
            }
            line = word;
        }
        lines.push_back(line);
    }

    while(!lines.empty() && lines.back().empty()) lines.pop_back();
    if(lines.size() > max_lines) {
        lines.resize(max_lines);
        std::string &last = lines.back();
        while(!last.empty() && !fits(last + "...")) {
            std::size_t back = last.size() - 1;
            while(back > 0 && (static_cast<unsigned char>(last[back]) & 0xC0) == 0x80) --back;
            last.erase(back);
        }
        last += "...";
    }

    std::string out;
    for(std::size_t i = 0; i < lines.size(); ++i) {
        if(i) out += '\n';
        out += lines[i];
    }
    return out;
}

std::string FormatDate(std::uint64_t yyyymmdd) {
    const unsigned year  = static_cast<unsigned>(yyyymmdd / 10000);
    const unsigned month = static_cast<unsigned>((yyyymmdd / 100) % 100);
    const unsigned day   = static_cast<unsigned>(yyyymmdd % 100);
    if(yyyymmdd > 99991231 || year < 1970 || month < 1 || month > 12 || day < 1 || day > 31) return "";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02u", year, month, day);
    return buf;
}

std::string Upper(std::string s) {
    for(char &c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

}

void DetailsTab::AddElementsTo(pu::ui::Layout *layout) {
    cover_bg_ = pu::ui::elm::Rectangle::New(kCoverX, kCoverY, kCoverSz, kCoverSz, kPanelClr, 16);
    layout->Add(cover_bg_);

    pu::sdl2::TextureHandle::Ref empty_th = nullptr;
    cover_img_ = pu::ui::elm::Image::New(kCoverX, kCoverY, empty_th);
    layout->Add(cover_img_);

    cover_fallback_ = pu::ui::elm::TextBlock::New(kCoverX, kCoverY, "");
    cover_fallback_->SetColor(kMutedClr);
    cover_fallback_->SetFont(kFontMedium);
    layout->Add(cover_fallback_);

    action_bg_ = pu::ui::elm::Rectangle::New(kCoverX, kActionY, kCoverSz, kActionH, kActionClr, 16);
    layout->Add(action_bg_);

    action_tb_ = pu::ui::elm::TextBlock::New(kCoverX, kActionY, "");
    action_tb_->SetColor(kTextClr);
    action_tb_->SetFont(kFontMedium);
    layout->Add(action_tb_);

    title_tb_ = pu::ui::elm::TextBlock::New(kTextX, kCoverY, "");
    title_tb_->SetColor(kTextClr);
    title_tb_->SetFont(kFontLarge);
    layout->Add(title_tb_);

    facts_tb_ = pu::ui::elm::TextBlock::New(kTextX, kCoverY, "");
    facts_tb_->SetColor(kMutedClr);
    facts_tb_->SetFont(kFontSmall);
    layout->Add(facts_tb_);

    desc_tb_ = pu::ui::elm::TextBlock::New(kTextX, kCoverY, "");
    desc_tb_->SetColor(kTextClr);
    desc_tb_->SetFont(kFontMedium);
    layout->Add(desc_tb_);

    notes_tb_ = pu::ui::elm::TextBlock::New(kTextX, kCoverY, "");
    notes_tb_->SetColor(kMutedClr);
    notes_tb_->SetFont(kFontSmall);
    layout->Add(notes_tb_);

    hint_tb_ = pu::ui::elm::TextBlock::New(0, 1022, "");
    hint_tb_->SetColor(kMutedClr);
    hint_tb_->SetFont(kFontSmall);
    layout->Add(hint_tb_);

    Hide();
}

void DetailsTab::Show(const BrowseTab::GridEntry &entry, bool queued) {
    entry_   = entry;
    queued_  = queued;
    visible_ = true;
    Populate();
}

void DetailsTab::Hide() {
    visible_ = false;
    for(auto &el : std::initializer_list<pu::ui::elm::Element::Ref>{
            cover_bg_, cover_img_, cover_fallback_, action_bg_, action_tb_,
            title_tb_, facts_tb_, desc_tb_, notes_tb_, hint_tb_}) {
        if(el) el->SetVisible(false);
    }
}

void DetailsTab::RefreshStrings() {
    if(visible_) Populate();
}

bool DetailsTab::HitsAction(const pu::ui::TouchPoint &tp) const {
    return visible_ && !queued_ && tp.HitsRegion(kCoverX, kActionY, kCoverSz, kActionH);
}

void DetailsTab::Populate() {
    if(!visible_) return;

    cover_bg_->SetVisible(true);
    if(entry_.icon_tex) {
        cover_img_->SetImage(entry_.icon_tex);
        cover_img_->SetWidth(kCoverSz);
        cover_img_->SetHeight(kCoverSz);
        cover_img_->SetVisible(true);
        cover_fallback_->SetVisible(false);
    } else {
        cover_img_->SetVisible(false);
        cover_fallback_->SetText(Upper(entry_.format.empty() ? "?" : entry_.format));
        cover_fallback_->SetX(kCoverX + (kCoverSz - cover_fallback_->GetWidth()) / 2);
        cover_fallback_->SetY(kCoverY + (kCoverSz - cover_fallback_->GetHeight()) / 2);
        cover_fallback_->SetVisible(true);
    }

    const std::string verb = entry_.is_installable ? pinx::i18n::tr("details.install")
                                                   : pinx::i18n::tr("details.download");
    action_bg_->SetColor(queued_ ? kIdleClr : kActionClr);
    action_tb_->SetText(queued_ ? pinx::i18n::tr("details.queued")
                                : std::string("\xEE\x82\xA0 ") + verb);
    action_tb_->SetColor(queued_ ? kMutedClr : kTextClr);
    action_tb_->SetX(kCoverX + (kCoverSz - action_tb_->GetWidth()) / 2);
    action_tb_->SetY(kActionY + (kActionH - action_tb_->GetHeight()) / 2);
    action_bg_->SetVisible(true);
    action_tb_->SetVisible(true);

    s32 y = kCoverY;
    title_tb_->SetText(Wrap(kFontLarge, entry_.name, kTextW, 2));
    title_tb_->SetY(y);
    title_tb_->SetVisible(true);
    y += title_tb_->GetHeight() + kGap / 2;

    std::vector<std::string> facts;
    if(!entry_.genre.empty())       facts.push_back(entry_.genre);
    if(!entry_.ported_from.empty()) facts.push_back(pinx::i18n::trf("details.ported_from", {entry_.ported_from}));
    if(!entry_.publisher.empty())   facts.push_back(entry_.publisher);
    if(!entry_.region.empty())      facts.push_back(entry_.region);
    if(const std::string d = FormatDate(entry_.release_date); !d.empty()) facts.push_back(d);
    if(!entry_.version.empty())     facts.push_back("v" + entry_.version);
    if(entry_.size > 0)             facts.push_back(pinx::app::HumanSize(entry_.size));
    if(!entry_.format.empty())      facts.push_back(Upper(entry_.format));
    std::string facts_line;
    for(const auto &f : facts) facts_line += (facts_line.empty() ? "" : "   \xC2\xB7   ") + f;
    facts_tb_->SetText(Wrap(kFontSmall, facts_line, kTextW, 2));
    facts_tb_->SetY(y);
    facts_tb_->SetVisible(!facts_line.empty());
    if(!facts_line.empty()) y += facts_tb_->GetHeight() + kGap;

    const std::string desc = entry_.description.empty() ? pinx::i18n::tr("details.no_description")
                                                        : entry_.description;
    desc_tb_->SetColor(entry_.description.empty() ? kMutedClr : kTextClr);
    desc_tb_->SetText(Wrap(kFontMedium, desc, kTextW, 10));
    desc_tb_->SetY(y);
    desc_tb_->SetVisible(true);
    y += desc_tb_->GetHeight() + kGap;

    if(!entry_.notes.empty()) {
        notes_tb_->SetText(Wrap(kFontSmall, pinx::i18n::tr("details.notes") + "\n" + entry_.notes, kTextW, 5));
        notes_tb_->SetY(y);
        notes_tb_->SetVisible(true);
    } else {
        notes_tb_->SetVisible(false);
    }

    std::string hint = std::string("\xEE\x82\xA1 ") + pinx::i18n::tr("hints.back");
    if(!queued_) hint = std::string("\xEE\x82\xA0 ") + verb + "    " + hint;
    hint_tb_->SetText(hint);
    hint_tb_->SetX(1920 / 2 - hint_tb_->GetWidth() / 2);
    hint_tb_->SetVisible(true);
}

}
