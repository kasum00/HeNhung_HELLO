/**
 * @file    HomeView.cpp
 * @brief   Home menu implementation.
 * @note    Owner: user (non-generated).
 */

#include <gui/home_screen/HomeView.hpp>
#include <gui/common/GuiTheme.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <gui/common/GuiSnapshots.hpp>
#include <touchgfx/TypedText.hpp>
#include <texts/TextKeysAndLanguages.hpp>

using namespace touchgfx;
using namespace gui;

namespace
{
struct MenuItem
{
    const char* label;
    ScreenId target;
};

const MenuItem MENU_ITEMS[HomeView::MENU_COUNT] = {
    { "Measure",     ScreenId::Dashboard },
    { "Waveform",    ScreenId::Waveform },
    { "History",     ScreenId::History },
    { "Clock",       ScreenId::DateTimeSettings }
};

/* Two-column button grid below the status bar (see GuiLayout::home). */
constexpr int16_t GRID_TOP = layout::home::GRID_TOP;
constexpr int16_t BTN_W = layout::home::BTN_W;
constexpr int16_t BTN_H = layout::home::BTN_H;
constexpr int16_t COL_GAP = layout::home::COL_GAP;
constexpr int16_t ROW_GAP = layout::home::ROW_GAP;
constexpr int16_t GRID_LEFT = layout::home::GRID_LEFT;
}

HomeView::HomeView()
    : menuClickedCallback(this, &HomeView::onMenuButtonClicked),
      tickCounter(0U)
{
    clockBuffer[0] = 0;
    statusBuffer[0] = 0;
}

void HomeView::setupScreen()
{
    HomeViewBase::setupScreen();

    background.setPosition(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    background.setColor(theme::background());
    add(background);

    statusBar.setPosition(0, 0, SCREEN_WIDTH, theme::STATUS_BAR_HEIGHT);
    statusBar.setColor(theme::statusBar());
    add(statusBar);

    statusText.setPosition(layout::home::STATUS_TEXT_X, layout::home::STATUS_TEXT_Y,
                           layout::home::STATUS_TEXT_W, layout::home::STATUS_TEXT_H);
    statusText.setColor(theme::textSecondary());
    statusText.setTypedText(TypedText(T_WCMEDIUMLEFT));
    statusText.setWildcard1(statusBuffer);
    add(statusText);

    clockText.setPosition(static_cast<int16_t>(SCREEN_WIDTH - layout::home::CLOCK_W - 6),
                          layout::home::CLOCK_Y, layout::home::CLOCK_W, layout::home::CLOCK_H);
    clockText.setColor(theme::textPrimary());
    clockText.setTypedText(TypedText(T_WCMEDIUMRIGHT));
    clockText.setWildcard1(clockBuffer);
    add(clockText);

    for (uint8_t i = 0U; i < MENU_COUNT; ++i)
    {
        const int16_t col = static_cast<int16_t>(i % 2U);
        const int16_t row = static_cast<int16_t>(i / 2U);
        const int16_t x = static_cast<int16_t>(GRID_LEFT + col * (BTN_W + COL_GAP));
        const int16_t y = static_cast<int16_t>(GRID_TOP + row * (BTN_H + ROW_GAP));

        menuButtons[i].setup(x, y, BTN_W, BTN_H);
        menuButtons[i].setColors(theme::surface(), theme::primaryDark(), theme::textPrimary());
        menuButtons[i].setLabel(MENU_ITEMS[i].label);
        menuButtons[i].setId(static_cast<int16_t>(MENU_ITEMS[i].target));
        menuButtons[i].setAction(menuClickedCallback);
        add(menuButtons[i]);
    }

    tickCounter = 0U;
    updateStatus();
}

void HomeView::tearDownScreen()
{
    HomeViewBase::tearDownScreen();
}

void HomeView::onMenuButtonClicked(const TextButton& button)
{
    const ScreenId target = static_cast<ScreenId>(button.getId());
    FrontendApplication* app = static_cast<FrontendApplication*>(Application::getInstance());
    app->requestScreen(target);
}

void HomeView::updateStatus()
{
    GuiMeasurementSnapshot m;
    if (!presenter->data().getMeasurementSnapshot(m))
    {
        return;
    }

    Unicode::snprintf(clockBuffer, 12, "%02u:%02u:%02u",
                      m.time.hour, m.time.minute, m.time.second);
    clockText.setWildcard1(clockBuffer);
    clockText.invalidate();

    /* %s expects a UnicodeChar list, so convert the ASCII status labels first. */
    Unicode::UnicodeChar sensorU[10];
    Unicode::UnicodeChar storageU[10];
    Unicode::strncpy(sensorU, toText(m.sensorStatus), 10);
    Unicode::strncpy(storageU, toText(m.storageStatus), 10);
    Unicode::snprintf(statusBuffer, 28, "Sensor %s  SD %s", sensorU, storageU);
    statusText.setWildcard1(statusBuffer);
    statusText.invalidate();
}

void HomeView::handleTickEvent()
{
    ++tickCounter;
    if ((tickCounter % 60U) == 0U)
    {
        updateStatus();
    }
}
