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

#include <borealis/animations.hpp>
#include <borealis/application.hpp>
#include <borealis/i18n.hpp>
#include <borealis/logger.hpp>
#include <borealis/popup_frame.hpp>
#include <borealis/tab_frame.hpp>

using namespace brls::i18n::literals;

namespace brls
{

PopupFrame::PopupFrame(std::string title, unsigned char* imageBuffer, size_t imageBufferSize, AppletFrame* contentView, std::string subTitleLeft, std::string subTitleRight)
    : contentView(contentView)
{
    // Enable touch support for PopupFrame
    this->touchable = true;

    if (this->contentView)
    {
        this->contentView->setParent(this);
        this->contentView->setHeaderStyle(HeaderStyle::POPUP);
        this->contentView->setTitle(title);
        this->contentView->setSubtitle(subTitleLeft, subTitleRight);
        this->contentView->setIcon(imageBuffer, imageBufferSize);
        this->contentView->invalidate();
    }

    contentView->setAnimateHint(true);
//    this->registerAction("brls/hints/back"_i18n, Key::B, [this] { return this->onCancel(); });
    this->registerAction("brls/hints/back"_i18n, Key::B, [this] { Application::popView(ViewAnimation::FADE); return true; });
}

PopupFrame::PopupFrame(std::string title, std::string imagePath, AppletFrame* contentView, std::string subTitleLeft, std::string subTitleRight)
    : contentView(contentView)
{
    // Enable touch support for PopupFrame
    this->touchable = true;

    if (this->contentView)
    {
        this->contentView->setParent(this);
        this->contentView->setHeaderStyle(HeaderStyle::POPUP);
        this->contentView->setTitle(title);
        this->contentView->setSubtitle(subTitleLeft, subTitleRight);
        this->contentView->setIcon(imagePath);
        this->contentView->invalidate();
    }

    contentView->setAnimateHint(true);
//    this->registerAction("brls/hints/back"_i18n, Key::B, [this] { return this->onCancel(); });
    this->registerAction("brls/hints/back"_i18n, Key::B, [this] { Application::popView(ViewAnimation::FADE); return true; });
}

PopupFrame::PopupFrame(std::string title, AppletFrame* contentView, std::string subTitleLeft, std::string subTitleRight)
    : contentView(contentView)
{
    // Enable touch support for PopupFrame
    this->touchable = true;

    if (this->contentView)
    {
        this->contentView->setParent(this);
        this->contentView->setHeaderStyle(HeaderStyle::POPUP);
        this->contentView->setTitle(title);
        this->contentView->setSubtitle(subTitleLeft, subTitleRight);
        this->contentView->invalidate();
    }

    contentView->setAnimateHint(true);
//    this->registerAction("brls/hints/back"_i18n, Key::B, [this] { return this->onCancel(); });
    this->registerAction("brls/hints/back"_i18n, Key::B, [this] { Application::popView(ViewAnimation::FADE); return true; });
}

void PopupFrame::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, Style* style, FrameContext* ctx)
{
    // Backdrop
    nvgFillColor(vg, a(ctx->theme->dropdownBackgroundColor));
    nvgBeginPath(vg);
    nvgRect(vg, 0, y, width, height);
    nvgFill(vg);

    // Background
    nvgFillColor(vg, a(ctx->theme->backgroundColorRGB));
    nvgBeginPath(vg);
    nvgRect(vg, style->PopupFrame.edgePadding, y, width - style->PopupFrame.edgePadding * 2, height);
    nvgFill(vg);

    // TODO: Shadow

    // Content view
    nvgSave(vg);
    nvgScissor(vg, style->PopupFrame.edgePadding, 0, style->PopupFrame.contentWidth, height);

    this->contentView->frame(ctx);

    nvgRestore(vg);
}

bool PopupFrame::onCancel()
{
    Application::popView();
    return true;
}

unsigned PopupFrame::getShowAnimationDuration(ViewAnimation animation)
{
    return View::getShowAnimationDuration(animation) / 2;
}

void PopupFrame::layout(NVGcontext* vg, Style* style, FontStash* stash)
{
    this->contentView->setBoundaries(style->PopupFrame.edgePadding, 0, style->PopupFrame.contentWidth, this->getHeight());
    this->contentView->invalidate();
}

View* PopupFrame::getDefaultFocus()
{
    if (this->contentView) {
        return this->contentView->getDefaultFocus();
    }

    return nullptr;
}

void PopupFrame::open(std::string title, unsigned char* imageBuffer, size_t imageBufferSize, AppletFrame* contentView, std::string subTitleLeft, std::string subTitleRight, bool setAsTouchable)
{
    PopupFrame* popupFrame = new PopupFrame(title, imageBuffer, imageBufferSize, contentView, subTitleLeft, subTitleRight);

    popupFrame->touchable = setAsTouchable;

    if (setAsTouchable) {
//        Application::setMainView(popupFrame);
//        Application::setCurrentTouchableView(popupFrame);
    }

    PopupFrame::createTouchableItems(contentView);

    Application::pushView(popupFrame);
}
void PopupFrame::open(std::string title, std::string imagePath, AppletFrame* contentView, std::string subTitleLeft, std::string subTitleRight, bool setAsTouchable)
{
    PopupFrame* popupFrame = new PopupFrame(title, imagePath, contentView, subTitleLeft, subTitleRight);

    popupFrame->touchable = setAsTouchable;

    if (setAsTouchable) {
//        Application::setMainView(popupFrame);
//        Application::setCurrentTouchableView(popupFrame);
    }

    PopupFrame::createTouchableItems(contentView);

    Application::pushView(popupFrame);
}

void PopupFrame::open(std::string title, AppletFrame* contentView, std::string subTitleLeft, std::string subTitleRight, bool setAsTouchable)
{
    PopupFrame* popupFrame = new PopupFrame(title, contentView, subTitleLeft, subTitleRight);

    popupFrame->touchable = setAsTouchable;

    if (setAsTouchable) {
//        Application::setMainView(popupFrame);
//        Application::setCurrentTouchableView(popupFrame);
    }

    PopupFrame::createTouchableItems(contentView);

    Application::pushView(popupFrame);
}

void PopupFrame::willAppear(bool resetState)
{
    this->contentView->willAppear(resetState);
}

void PopupFrame::willDisappear(bool resetState)
{
    this->contentView->willDisappear(resetState);
}

void PopupFrame::processTouch(u32 x, u32 y) {
    forceWriteLog("popup_frame.cpp", fmt::format("PopupFrame::processTouch() START - touch: ({}, {})", x, y));
    forceWriteLog("popup_frame.cpp", fmt::format("  Touchable objects count: {}", this->contentView->getTouchableObjects().size()));

    for (brls::View* obj : this->contentView->getTouchableObjects()) {
        forceWriteLog("popup_frame.cpp", fmt::format("  Checking obj: '{}' - bounds: ({}, {}, {}, {})",
            obj->describe(), obj->getX(), obj->getY(), obj->getWidth(), obj->getHeight()));

	    if (obj->IsPositionInBounds(x, y)) {
            forceWriteLog("popup_frame.cpp", fmt::format("  HIT! Object: '{}' - clickFocusable: {}", obj->describe(), obj->isClickFocusable()));

            // Give focus if clickFocusable
            if (obj->isClickFocusable()) {
                Application::giveFocus(obj);
                forceWriteLog("popup_frame.cpp", fmt::format("  Gave focus to: '{}'", obj->describe()));
            }

            obj->click(obj);
            forceWriteLog("popup_frame.cpp", fmt::format("  Clicked: '{}'", obj->describe()));
            break;
        }
    }
    forceWriteLog("popup_frame.cpp", "PopupFrame::processTouch() END");
}

void PopupFrame::onWindowFocus() {
    Application::setMainView(this);

//    forceWriteLog("popup_frame.cpp", "onWindowFocus() - Trying to get sidebar...");

    if (this->contentView) {
//        forceWriteLog("popup_frame.cpp", "onWindowFocus() - contentView exists!");

        TabFrame* tf = dynamic_cast<TabFrame*>(this->contentView); 
        if (tf) {
//            forceWriteLog("popup_frame.cpp", "onWindowFocus() - tf exists!");

            Sidebar* sb = tf->getSidebar();
		    
//            forceWriteLog("popup_frame.cpp", "onWindowFocus() - sb fetched...");
		    
            if (sb) {
		    
//                forceWriteLog("popup_frame.cpp", "onWindowFocus() - sb exists...");
		    
//                forceWriteLog("popup_frame.cpp", fmt::format("onWindowFocus() - lastFocus: '{}'", sb->lastFocus));
		    
                SidebarItem* item = dynamic_cast<SidebarItem*>(sb->getChild(sb->lastFocus));
                if (item) {
//                    forceWriteLog("popup_frame.cpp", fmt::format("onWindowFocus() - SidebarItem: '{}'", item->getLabel()));
		    
                    sb->onChildFocusGained(item);
                }
            }
        }
    }

//    if (brls::Application::getCurrentTouchableView() != nullptr)
//        forceWriteLog("popup_frame.cpp", fmt::format("onWindowFocus() - Application::getCurrentTouchableView(): '{}'", brls::Application::getCurrentTouchableView()->describe()));
//    else
//        forceWriteLog("popup_frame.cpp", "onWindowFocus() - Application::getCurrentTouchableView(): 'nullptr'");

//    forceWriteLog("popup_frame.cpp", fmt::format("onWindowFocus() - Application::getMainView(): '{}'", brls::Application::getMainView()->describe()));
}

void PopupFrame::createTouchableItems(AppletFrame* contentView) {
//    forceWriteLog("tab_frame.cpp", "Creating touchable items...");
    //touchable items
    ListItem* btnA = new ListItem("Button A - Ok");
    btnA->setX(952);
    btnA->setY(661);
    btnA->setWidth(72);
    btnA->setHeight(60);
    btnA->setParent(contentView);
    btnA->setClickFocusable(false);
    btnA->setState(true);
    btnA->getClickEvent()->subscribe([contentView](View* view) {
//        forceWriteLog("popup_frame.cpp", "btnA.click() - Clicked button...");
        //
    });
    contentView->addTouchableObjects(btnA);

    ListItem* btnB = new ListItem("Button B - Back");
    btnB->setX(833);
    btnB->setY(661);
    btnB->setWidth(105);
    btnB->setHeight(60);
    btnB->setParent(contentView);
    btnB->setClickFocusable(false);
    btnB->setState(true);
    btnB->getClickEvent()->subscribe([contentView](View* view) {
//        forceWriteLog("popup_frame.cpp", "btnB.click() - Clicked button...");
        Application::popView(ViewAnimation::FADE);
    });
    contentView->addTouchableObjects(btnB);

    ListItem* btnP = new ListItem("Button + - Close");
    btnP->setX(1040);
    btnP->setY(661);
    btnP->setWidth(77);
    btnP->setHeight(60);
    btnP->setParent(contentView);
    btnP->setClickFocusable(false);
    btnP->setState(true);
    btnP->getClickEvent()->subscribe([contentView](View* view) {
//        forceWriteLog("popup_frame.cpp", "btnP.click() - Clicked button...");
        romfsExit();
        brls::Application::quit();
    });
    contentView->addTouchableObjects(btnP);
}

PopupFrame::~PopupFrame()
{
    if (this->contentView)
        delete this->contentView;
}

} // namespace brls
