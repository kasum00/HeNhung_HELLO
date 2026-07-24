#ifndef BOOTVIEW_HPP
#define BOOTVIEW_HPP

/**
 * @file    BootView.hpp
 * @brief   Boot splash: project name, version, mock init steps, progress bar.
 *
 * Widgets are built in code (no Designer layout). The screen advances a mock
 * initialization sequence using tick timing only (never blocking delays) and
 * navigates to Home when finished.
 *
 * @note  Owner: user (non-generated).
 */

#include <gui_generated/boot_screen/BootViewBase.hpp>
#include <gui/boot_screen/BootPresenter.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/Unicode.hpp>

class BootView : public BootViewBase
{
public:
    BootView();
    virtual ~BootView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();

protected:
    void updateStep();

    touchgfx::Box background;
    touchgfx::Box logo;
    touchgfx::TextAreaWithOneWildcard titleText;
    touchgfx::TextAreaWithOneWildcard versionText;
    touchgfx::TextAreaWithOneWildcard statusText;
    touchgfx::Box progressTrack;
    touchgfx::Box progressFill;

    touchgfx::Unicode::UnicodeChar titleBuffer[24];
    touchgfx::Unicode::UnicodeChar versionBuffer[24];
    touchgfx::Unicode::UnicodeChar statusBuffer[28];

    uint32_t tickCounter;
    uint8_t stepIndex;
    bool navigationRequested;
};

#endif // BOOTVIEW_HPP
