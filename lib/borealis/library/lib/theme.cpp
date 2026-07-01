/*
    Borealis, a Nintendo Switch UI Library
    Copyright (C) 2019  natinusala
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

#include <borealis/theme.hpp>

namespace brls
{

HorizonLightTheme::HorizonLightTheme()
{
    this->backgroundColor[0] = 0.922f;
    this->backgroundColor[1] = 0.922f;
    this->backgroundColor[2] = 0.922f;
    this->backgroundColorRGB = nvgRGB(235, 235, 235);

    this->textColor        = nvgRGB(51, 51, 51);
    this->descriptionColor = nvgRGB(140, 140, 140);

    this->notificationTextColor = nvgRGB(255, 255, 255);
    this->backdropColor         = nvgRGBA(0, 0, 0, 178);

    this->separatorColor = nvgRGB(45, 45, 45);

    this->sidebarColor          = nvgRGB(240, 240, 240);
    this->activeTabColor        = nvgRGB(49, 79, 235);
    this->sidebarSeparatorColor = nvgRGB(208, 208, 208);

    this->highlightBackgroundColor = nvgRGB(252, 255, 248);
    this->highlightColor1          = nvgRGB(13, 182, 213);
    this->highlightColor2          = nvgRGB(80, 239, 217);

    this->listItemSeparatorColor  = nvgRGB(207, 207, 207);
    this->listItemValueColor      = nvgRGB(43, 81, 226);
    this->listItemFaintValueColor = nvgRGB(181, 184, 191);

    this->tableEvenBackgroundColor = nvgRGB(240, 240, 240);
    this->tableBodyTextColor       = nvgRGB(131, 131, 131);

    this->dropdownBackgroundColor = nvgRGBA(0, 0, 0, 178);

    this->nextStageBulletColor = nvgRGB(165, 165, 165);

    this->spinnerBarColor = nvgRGBA(131, 131, 131, 102);

    this->scrollBarColor = nvgRGB(138, 138, 138);
    this->scrollBarAlphaNormal = 0.2f;
    this->scrollBarAlphaFull = 0.5f;

    this->clickAnimationAlpha = 0.3f;

    this->headerRectangleColor = nvgRGB(127, 127, 127);

    this->buttonPrimaryEnabledBackgroundColor  = nvgRGB(50, 79, 241);
    this->buttonPrimaryDisabledBackgroundColor = nvgRGB(201, 201, 209);
    this->buttonPrimaryEnabledTextColor        = nvgRGB(255, 255, 255);
//    this->buttonPrimaryDisabledTextColor       = nvgRGB(220, 220, 228);
    this->buttonPrimaryDisabledTextColor       = nvgRGB(172, 172, 172);
    this->buttonBorderedBorderColor            = nvgRGB(45, 45, 45);
    this->buttonBorderedTextColor              = nvgRGB(45, 45, 45);
    this->buttonRegularBackgroundColor         = nvgRGB(255, 255, 255);
    this->buttonRegularTextColor               = nvgRGB(46, 46, 46);
    this->buttonRegularBorderColor             = nvgRGB(223, 223, 223);

    this->dialogColor                = nvgRGB(240, 240, 240);
    this->dialogBackdrop             = nvgRGBA(0, 0, 0, 100);
    this->dialogButtonColor          = nvgRGB(46, 78, 255);
    this->dialogButtonSeparatorColor = nvgRGB(210, 210, 210);
}

HorizonDarkTheme::HorizonDarkTheme()
{
    // SwitchU-inspired dark theme — near-black navy background, bright blue accent
    this->backgroundColor[0] = 0.039f; // #0A0A14
    this->backgroundColor[1] = 0.039f;
    this->backgroundColor[2] = 0.078f;
    this->backgroundColorRGB = nvgRGB(10, 10, 20);

    this->textColor        = nvgRGB(255, 255, 255);
    this->descriptionColor = nvgRGB(179, 184, 204); // SwitchU text secondary

    this->notificationTextColor = nvgRGB(255, 255, 255);
    this->backdropColor         = nvgRGBA(0, 0, 0, 200);

    this->separatorColor = nvgRGBA(140, 140, 160, 55); // very subtle

    // Sidebar: transparent so the gradient background shows through
    this->sidebarColor          = nvgRGBA(10, 8, 22, 0);
    this->activeTabColor        = nvgRGB(89, 140, 255);  // SwitchU accent #5988FF
    this->sidebarSeparatorColor = nvgRGBA(140, 140, 160, 40);

    // Focus highlight: SwitchU cyan cursor
    this->highlightBackgroundColor = nvgRGB(10, 12, 28);
    this->highlightColor1          = nvgRGB(0, 191, 255);  // bright cyan
    this->highlightColor2          = nvgRGB(77, 217, 255); // lighter cyan

    this->listItemSeparatorColor  = nvgRGBA(140, 140, 160, 35);
    this->listItemValueColor      = nvgRGB(89, 140, 255);  // accent blue
    this->listItemFaintValueColor = nvgRGB(100, 105, 130);

    this->tableEvenBackgroundColor = nvgRGB(18, 18, 32);
    this->tableBodyTextColor       = nvgRGB(155, 160, 185);

    this->dropdownBackgroundColor = nvgRGBA(0, 0, 0, 210);

    this->nextStageBulletColor = nvgRGB(140, 145, 170);

    this->spinnerBarColor = nvgRGBA(89, 140, 255, 120);

    this->scrollBarColor = nvgRGB(89, 140, 255);
    this->scrollBarAlphaNormal = 0.25f;
    this->scrollBarAlphaFull   = 0.60f;

    this->clickAnimationAlpha = 0.25f;

    this->headerRectangleColor = nvgRGB(89, 140, 255);

    this->buttonPrimaryEnabledBackgroundColor  = nvgRGB(89, 140, 255);
    this->buttonPrimaryDisabledBackgroundColor = nvgRGB(50, 52, 72);
    this->buttonPrimaryEnabledTextColor        = nvgRGB(255, 255, 255);
    this->buttonPrimaryDisabledTextColor       = nvgRGB(120, 125, 150);
    this->buttonBorderedBorderColor            = nvgRGB(89, 140, 255);
    this->buttonBorderedTextColor              = nvgRGB(255, 255, 255);
    this->buttonRegularBackgroundColor         = nvgRGB(28, 28, 48);
    this->buttonRegularTextColor               = nvgRGB(255, 255, 255);
    this->buttonRegularBorderColor             = nvgRGBA(140, 140, 160, 60);

    this->dialogColor                = nvgRGB(22, 20, 40);
    this->dialogBackdrop             = nvgRGBA(0, 0, 8, 140);
    this->dialogButtonColor          = nvgRGB(89, 140, 255);
    this->dialogButtonSeparatorColor = nvgRGBA(140, 140, 160, 60);
}

} // namespace brls
