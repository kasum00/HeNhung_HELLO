#ifndef ABOUTVIEW_HPP
#define ABOUTVIEW_HPP

/**
 * @file    AboutView.hpp
 * @brief   About / system information with the mandatory medical disclaimer.
 * @note    Owner: user (non-generated).
 */

#include <gui_generated/about_screen/AboutViewBase.hpp>
#include <gui/about_screen/AboutPresenter.hpp>
#include <gui/widgets/TopBar.hpp>
#include <gui/widgets/InfoRow.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/Unicode.hpp>

class AboutView : public AboutViewBase
{
public:
    static constexpr uint8_t ROW_COUNT = 6U;

    AboutView();
    virtual ~AboutView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

protected:
    touchgfx::Box background;
    gui::TopBar topBar;
    touchgfx::TextAreaWithOneWildcard titleText;
    gui::InfoRow rows[ROW_COUNT];
    touchgfx::Box disclaimerBox;
    touchgfx::TextAreaWithOneWildcard disclaimerLine1;
    touchgfx::TextAreaWithOneWildcard disclaimerLine2;

    touchgfx::Unicode::UnicodeChar titleBuffer[24];
    touchgfx::Unicode::UnicodeChar disclaimer1Buffer[28];
    touchgfx::Unicode::UnicodeChar disclaimer2Buffer[28];
};

#endif // ABOUTVIEW_HPP
