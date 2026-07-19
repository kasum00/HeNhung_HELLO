#ifndef HOMEVIEW_HPP
#define HOMEVIEW_HPP

/**
 * @file    HomeView.hpp
 * @brief   Home menu: status bar (clock + sensor/SD) and shortcut buttons.
 *
 * Widgets are built in code. A single click handler serves all menu buttons,
 * navigating to the screen encoded in each button's id.
 *
 * @note  Owner: user (non-generated).
 */

#include <gui_generated/home_screen/HomeViewBase.hpp>
#include <gui/home_screen/HomePresenter.hpp>
#include <gui/widgets/TextButton.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/Unicode.hpp>
#include <touchgfx/Callback.hpp>

class HomeView : public HomeViewBase
{
public:
    /** Number of shortcut buttons on the menu. */
    static constexpr uint8_t MENU_COUNT = 6U;

    HomeView();
    virtual ~HomeView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();

protected:
    void updateStatus();
    void onMenuButtonClicked(const gui::TextButton& button);

    touchgfx::Box background;
    touchgfx::Box statusBar;
    touchgfx::TextAreaWithOneWildcard clockText;
    touchgfx::TextAreaWithOneWildcard statusText;

    gui::TextButton menuButtons[MENU_COUNT];
    touchgfx::Callback<HomeView, const gui::TextButton&> menuClickedCallback;

    touchgfx::Unicode::UnicodeChar clockBuffer[12];
    touchgfx::Unicode::UnicodeChar statusBuffer[28];

    uint32_t tickCounter;
};

#endif // HOMEVIEW_HPP
