/*
    Borealis, a Nintendo Switch UI Library
    Copyright (C) 2019  natinusala
    Copyright (C) 2019  WerWolv
    Copyright (C) 2019  p-sam

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <chrono>
#include <filesystem>
#include <fstream>

#include <borealis/application.hpp>
#include <borealis/i18n.hpp>
#include <borealis/sidebar.hpp>

using namespace brls::i18n::literals;

namespace brls
{

SidebarSeparator::SidebarSeparator()
{
    Style* style = Application::getStyle();
    this->setHeight(style->Sidebar.Separator.height);
}

void SidebarSeparator::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, Style* style, FrameContext* ctx)
{
    nvgFillColor(vg, a(ctx->theme->sidebarSeparatorColor));
    nvgBeginPath(vg);
    nvgRect(vg, x, y + height / 2, width, 1);
    nvgFill(vg);
}

SidebarItem::SidebarItem(std::string label, Sidebar* sidebar)
    : label(label)
    , sidebar(sidebar)
{
    Style* style = Application::getStyle();
    this->setHeight(style->Sidebar.Item.height);

    this->registerAction("brls/hints/ok"_i18n, Key::A, [this] { return this->onClick(); });
}

SidebarItem* Sidebar::addItem(std::string label, View* view)
{
    SidebarItem* item = new SidebarItem(label, this);

    item->setIndex(this->children.size());

    item->setAssociatedView(view);

    if (this->isEmpty())
        setActive(item);

    this->addView(item);

    return item;
}

void SidebarItem::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, Style* style, FrameContext* ctx)
{
    // Subtle glass highlight behind the active item
    if (this->active)
    {
        const float pad = 6.0f;
        NVGpaint bg = nvgLinearGradient(vg,
            static_cast<float>(x), static_cast<float>(y),
            static_cast<float>(x + width), static_cast<float>(y),
            nvgRGBA(89, 140, 255, 35), nvgRGBA(89, 140, 255, 0));
        nvgBeginPath(vg);
        nvgRoundedRect(vg, static_cast<float>(x) + pad, static_cast<float>(y) + pad,
                       static_cast<float>(width) - pad * 2.0f,
                       static_cast<float>(height) - pad * 2.0f, 10.0f);
        nvgFillPaint(vg, bg);
        nvgFill(vg);
    }

    // Label
    nvgFillColor(vg, a(this->active ? ctx->theme->activeTabColor : nvgRGBA(179, 184, 204, 180)));
    nvgFontSize(vg, style->Sidebar.Item.textSize);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFontFaceId(vg, ctx->fontStash->regular);
    nvgBeginPath(vg);
    nvgText(vg, x + style->Sidebar.Item.textOffsetX + style->Sidebar.Item.padding, y + height / 2, this->label.c_str(), nullptr);

    // Active marker: left-side accent bar
    if (this->active)
    {
        const int pad  = style->Sidebar.Item.padding;
        const int barW = style->Sidebar.Item.activeMarkerWidth;
        const int barH = style->Sidebar.Item.height - pad * 2;
        NVGpaint bar = nvgLinearGradient(vg,
            static_cast<float>(x + pad), static_cast<float>(y + pad),
            static_cast<float>(x + pad), static_cast<float>(y + pad + barH),
            nvgRGB(89, 140, 255), nvgRGB(38, 100, 255));
        nvgBeginPath(vg);
        nvgRoundedRect(vg, static_cast<float>(x + pad), static_cast<float>(y + pad),
                       static_cast<float>(barW), static_cast<float>(barH), 2.0f);
        nvgFillPaint(vg, bar);
        nvgFill(vg);
    }

    // Right-edge divider (drawn only on last item pass — actually just draw on every item;
    // it stacks fine since they're all at the same x+width position)
    nvgBeginPath(vg);
    nvgRect(vg, x + width - 1, y, 1, height);
    nvgFillColor(vg, nvgRGBA(140, 140, 160, 30));
    nvgFill(vg);
}

bool SidebarItem::onClick()
{
    Application::onGamepadButtonPressed(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT, false);
    return true;
}

void SidebarItem::click(View* sender)
{
//    forceWriteLog("sidebar.cpp", "Method click() started");

//    forceWriteLog("sidebar.cpp", fmt::format("CurrentTouchableView at start: '{}'", Application::getCurrentTouchableView()->describe()));

    Sidebar* sidebar = dynamic_cast<Sidebar*>(this->getParent());

    sidebar->childFocusFromClick = true;

    View* view = nullptr;
    View* oldFocus = sidebar->getChild(sidebar->lastFocus);
    View* newFocus = this->getDefaultFocus();

    if (oldFocus != newFocus) {

//        forceWriteLog("sidebar.cpp", fmt::format("click() - lastfocus at the start of the method: '{}'", sidebar->lastFocus));
//        forceWriteLog("sidebar.cpp", fmt::format("click() - getSelectedItemIndex() at the start of the method: '{}' ", sidebar->getSelectedItemIndex()));
//        forceWriteLog("sidebar.cpp", fmt::format("click() - sender index'{}'", dynamic_cast<SidebarItem*>(sender)->getIndex()));

//        forceWriteLog("sidebar.cpp", fmt::format("click() - oldFocus: '{}' - oldFocus->getIndex(): '{}' - oldFocus->getLabel(): '{}'", oldFocus->describe(), dynamic_cast<SidebarItem*>(oldFocus)->getIndex(), dynamic_cast<SidebarItem*>(oldFocus)->getLabel()));
//        forceWriteLog("sidebar.cpp", fmt::format("click() - newFocus: '{}' - newFocus->getIndex(): '{}' - newFocus->getLabel(): '{}'", newFocus->describe(), dynamic_cast<SidebarItem*>(newFocus)->getIndex(), dynamic_cast<SidebarItem*>(newFocus)->getLabel()));

        if (oldFocus) {
            dynamic_cast<SidebarItem*>(oldFocus)->setActive(false);
            //calling onWindowFocusLost for old view
            oldFocus->onFocusLost();

            view = dynamic_cast<SidebarItem*>(oldFocus)->getAssociatedView();
            if (view) {
//                forceWriteLog("sidebar.cpp", fmt::format("click() - calling onWindowFocusLost() for old view '{}'", view->describe()));
                view->onWindowFocusLost();
                view->wasAlreadyFirstFocused(false);
                view = view->getDefaultFocus();
                if (view)
                    view->onFocusLost();
            }
        }

        if (newFocus) {
            setActive(newFocus);
            //calling onWindowFocus for new view
//            newFocus->onFocusGained();

//            Application::onGamepadButtonPressed(GLFW_GAMEPAD_BUTTON_DPAD_LEFT, false);
//            Application::onGamepadButtonPressed(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT, false);

            newFocus->onFocusLost();
 
            Application::getGlobalFocusChangeEvent()->fire(newFocus);
//            dynamic_cast<SidebarItem*>(newFocus)->onClick();

/*
            view = dynamic_cast<SidebarItem*>(newFocus)->getAssociatedView();
            if (view) {
//                Application::getGlobalFocusChangeEvent()->fire(view);

                View* view2 = view->getDefaultFocus();
                if (view2)
                    Application::giveFocus(view2);
//                forceWriteLog("sidebar.cpp", fmt::format("click() - calling onWindowFocus for old view '{}'", view->describe()));
                view->onWindowFocus();
            }
*/
        }
    }

    sidebar->childFocusFromClick = false;

//    forceWriteLog("sidebar.cpp", fmt::format("CurrentTouchableView at end: '{}'", Application::getCurrentTouchableView()->describe()));

//    forceWriteLog("sidebar.cpp", "Method click() endeded");
}

void SidebarItem::setActive(bool active)
{
    this->active = active;
}

void SidebarItem::setAssociatedView(View* view)
{
    this->associatedView = view;
}

bool SidebarItem::isActive()
{
    return this->active;
}

void SidebarItem::onFocusGained()
{
    this->sidebar->setActive(this);
    View::onFocusGained();
}

void SidebarItem::setFocusable(bool focusable) {
    this->focusable = focusable;
}

bool SidebarItem::isFocusable() {
    return this->focusable;
}

View* SidebarItem::getAssociatedView()
{
    return this->associatedView;
}

ViewType SidebarItem::getViewType()
{
    return this->viewType;
}

std::string SidebarItem::getLabel() {
    return label;
}

int SidebarItem::getIndex() {
    return index;
}

void SidebarItem::setIndex(int index) {
    this->index = index;
}

SidebarItem::~SidebarItem()
{
    if (this->associatedView)
        delete this->associatedView;
}

Sidebar::Sidebar()
    : BoxLayout(BoxLayoutOrientation::VERTICAL)
{
    Style* style = Application::getStyle();

    this->setWidth(style->Sidebar.width);
    this->setSpacing(style->Sidebar.spacing);
    this->setMargins(style->Sidebar.marginTop, style->Sidebar.marginRight, style->Sidebar.marginBottom, style->Sidebar.marginLeft);
    this->setBackground(ViewBackground::SIDEBAR);
}

View* Sidebar::getDefaultFocus()
{
//    forceWriteLog("sidebar.cpp", "Method getDefaultFocus() started");
    // Sanity check
    if (this->lastFocus >= this->children.size()) {
        this->lastFocus = 0;
//        forceWriteLog("sidebar.cpp", fmt::format("getDefaultFocus() - sidebar->lastFocus: '{}'", this->lastFocus));
    }

    View* toFocus { nullptr };
    // Try to focus last focused one
    if (this->children.size() != 0)
        toFocus = this->children[this->lastFocus]->view->getDefaultFocus();

    if (toFocus) {
//        forceWriteLog("sidebar.cpp", fmt::format("Method getDefaultFocus() ended - returned '{}' - '{}'", toFocus->describe(), dynamic_cast<SidebarItem*>(toFocus)->getLabel()));
        return toFocus;
    }

    // Otherwise just get the first available item
//    forceWriteLog("sidebar.cpp", fmt::format("Method getDefaultFocus() ended - returned '{}'", toFocus->describe()));
    return BoxLayout::getDefaultFocus();
}

void Sidebar::onChildFocusGained(View* child)
{
//    forceWriteLog("sidebar.cpp", "Method onChildFocusGained() started");
    size_t position = *((size_t*)child->getParentUserData());

    SidebarItem* item = dynamic_cast<SidebarItem*>(child);
    View* view = nullptr;
    view = item->getAssociatedView();
/*
BEFORE SELECTING THE NEW CHILD
*/
//    forceWriteLog("sidebar.cpp", fmt::format("onChildFocusGained() - lastfocus at the start of the method: '{}'", this->lastFocus));
//    forceWriteLog("sidebar.cpp", fmt::format("onChildFocusGained() - getSelectedItemIndex() at the start of the method: '{}' ", dynamic_cast<Sidebar*>(item->getParent())->getSelectedItemIndex()));

//    if (position != this->lastFocus) { //making sure the new child is different than the last focus
//        forceWriteLog("sidebar.cpp", "onChildFocusGained() - lastFocus is different than the newly selected item");

        if (!childFocusFromClick) {
            //calling onWindowFocusLost for old view
            if (Application::getCurrentTouchableView() != nullptr) {
                if (Application::getCurrentTouchableView() != view) {
                    Application::getCurrentTouchableView()->onWindowFocusLost();
                    Application::getCurrentTouchableView()->wasAlreadyFirstFocused(false);
//                    forceWriteLog("sidebar.cpp", fmt::format("onChildFocusGained() - calling onWindowFocusLost() for old view '{}'", Application::getCurrentTouchableView()->describe()));
                }
            }
        }

        this->lastFocus = position;
        dynamic_cast<Sidebar*>(item->getParent())->setSelectedItemIndex(position);

//        forceWriteLog("sidebar.cpp", fmt::format("onChildFocusGained() - new lastfocus value: '{}'", this->lastFocus));
//        forceWriteLog("sidebar.cpp", fmt::format("onChildFocusGained() - new getSelectedItemIndex() value: '{}' ", dynamic_cast<Sidebar*>(item->getParent())->getSelectedItemIndex()));

//        forceWriteLog("sidebar.cpp", fmt::format("onChildFocusGained() - View Stack Size: '{}'", Application::getViewStackSize()));

        if (item->isFocusable()) {
            Application::setCurrentTouchableView(view);
//            forceWriteLog("sidebar.cpp", fmt::format("onChildFocusGained() - Setting current touchable view to '{}'", view->describe()));
//            forceWriteLog("sidebar.cpp", fmt::format("onChildFocusGained() - current Main View is '{}'", Application::getMainView()->describe()));
        }
//        else {
//            forceWriteLog("sidebar.cpp", "onChildFocusGained() - item is not focusable... skipping setting current touchable view!");
//            forceWriteLog("sidebar.cpp", fmt::format("onChildFocusGained() - current touchable view is '{}'", Application::getCurrentTouchableView()->describe()));
//            forceWriteLog("sidebar.cpp", fmt::format("onChildFocusGained() - current Main View is '{}'", Application::getMainView()->describe()));
//        }

        if (!childFocusFromClick) {
            //calling onWindowFocus for new view
            if (Application::getCurrentTouchableView() != nullptr) {
                Application::getCurrentTouchableView()->onWindowFocus();
//                forceWriteLog("sidebar.cpp", fmt::format("onChildFocusGained() - calling onWindowFocus() for new view '{}'", Application::getCurrentTouchableView()->describe()));
            }
        }

        BoxLayout::onChildFocusGained(child);
//    }
//    else
//        forceWriteLog("sidebar.cpp", "onChildFocusGained() - lastFocus is the same as the one already selected");

//    forceWriteLog("sidebar.cpp", "Method onChildFocusGained() ended");
}

void Sidebar::addSeparator()
{
    SidebarSeparator* separator = new SidebarSeparator();
    this->addView(separator);
}

void Sidebar::setActive(SidebarItem* active)
{
    if (currentActive)
        currentActive->setActive(false);

    currentActive = active;
    active->setActive(true);
}

void Sidebar::switchToCurrentActive() {
    Application::setCurrentTouchableView(currentActive->getAssociatedView());
//    forceWriteLog("sidebar.cpp", fmt::format("switchToCurrentActive() - Setting current touchable view to '{}'", currentActive->getAssociatedView()->describe()));

    if (Application::getCurrentTouchableView() != nullptr) {
        Application::getCurrentTouchableView()->onWindowFocus();
//        forceWriteLog("sidebar.cpp", fmt::format("switchToCurrentActive() - calling onWindowFocus() for new view '{}'", currentActive->getAssociatedView()->describe()));
    }
}

} // namespace brls
