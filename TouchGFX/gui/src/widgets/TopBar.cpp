/**
 * @file    TopBar.cpp
 * @brief   Implementation of the reusable top navigation strip.
 * @note    Owner: user (non-generated).
 */

#include <gui/widgets/TopBar.hpp>
#include <gui/common/GuiTheme.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <touchgfx/TypedText.hpp>
#include <texts/TextKeysAndLanguages.hpp>

using namespace touchgfx;

namespace gui
{
TopBar::TopBar()
    : bar(),
      titleText(),
      backButton(),
      backClickedCallback(this, &TopBar::onBack),
      backScreen(ScreenId::Home)
{
    titleBuffer[0] = 0;
}

void TopBar::build(const char* title, bool withBack)
{
    setPosition(0, 0, SCREEN_WIDTH, HEIGHT);

    bar.setPosition(0, 0, SCREEN_WIDTH, HEIGHT);
    bar.setColor(theme::statusBar());
    add(bar);

    const int16_t titleX = withBack ? layout::widget::TOPBAR_TITLE_X_WITH_BACK
                                    : layout::widget::TOPBAR_TITLE_X_PLAIN;
    const int16_t titleW = static_cast<int16_t>(SCREEN_WIDTH - titleX - layout::MARGIN);
    titleText.setPosition(titleX, layout::widget::TOPBAR_TITLE_Y, titleW, layout::widget::TOPBAR_TITLE_H);
    titleText.setColor(theme::textPrimary());
    titleText.setTypedText(TypedText(T_WCDEFAULTLEFT));
    Unicode::strncpy(titleBuffer, title, TITLE_CAPACITY + 1U);
    titleBuffer[TITLE_CAPACITY] = 0;
    titleText.setWildcard1(titleBuffer);
    add(titleText);

    if (withBack)
    {
        backButton.setup(layout::widget::TOPBAR_BACK_X, layout::widget::TOPBAR_BACK_Y,
                         layout::widget::TOPBAR_BACK_W,
                         static_cast<int16_t>(HEIGHT - 2 * layout::widget::TOPBAR_BACK_Y));
        backButton.setColors(theme::surfaceAlt(), theme::primaryDark(), theme::textPrimary());
        backButton.setLabel("<");
        backButton.setAction(backClickedCallback);
        add(backButton);
    }
}

void TopBar::setup(const char* title, ScreenId backTarget)
{
    backScreen = backTarget;
    build(title, true);
}

void TopBar::setupTitleOnly(const char* title)
{
    build(title, false);
}

void TopBar::onBack(const TextButton& /*button*/)
{
    FrontendApplication* app = static_cast<FrontendApplication*>(Application::getInstance());
    app->requestScreen(backScreen);
}

} // namespace gui
