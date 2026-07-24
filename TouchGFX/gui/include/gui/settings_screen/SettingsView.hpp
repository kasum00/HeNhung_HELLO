#ifndef SETTINGSVIEW_HPP
#define SETTINGSVIEW_HPP

/**
 * @file    SettingsView.hpp
 * @brief   Settings prototype: draft edits with Apply / Cancel / Restore.
 *
 * Each setting has a value button that edits the DRAFT configuration via typed
 * commands. Nothing is written to the active configuration until Apply. Cancel
 * reverts the draft; Restore Defaults loads defaults into the draft.
 *
 * @note  Owner: user (non-generated).
 */

#include <gui_generated/settings_screen/SettingsViewBase.hpp>
#include <gui/settings_screen/SettingsPresenter.hpp>
#include <gui/widgets/TopBar.hpp>
#include <gui/widgets/TextButton.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/Unicode.hpp>
#include <touchgfx/Callback.hpp>

class SettingsView : public SettingsViewBase
{
public:
    static constexpr uint8_t SETTING_COUNT = 6U;

    SettingsView();
    virtual ~SettingsView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

protected:
    void refresh();
    void onValue(const gui::TextButton& button);
    void onApply(const gui::TextButton& button);
    void onCancel(const gui::TextButton& button);
    void onRestore(const gui::TextButton& button);
    void showStatus(const char* message, touchgfx::colortype color);

    touchgfx::Box background;
    gui::TopBar topBar;
    touchgfx::TextAreaWithOneWildcard labels[SETTING_COUNT];
    gui::TextButton valueButtons[SETTING_COUNT];
    touchgfx::TextAreaWithOneWildcard statusText;
    gui::TextButton applyButton;
    gui::TextButton cancelButton;
    gui::TextButton restoreButton;

    touchgfx::Callback<SettingsView, const gui::TextButton&> valueClicked;
    touchgfx::Callback<SettingsView, const gui::TextButton&> applyClicked;
    touchgfx::Callback<SettingsView, const gui::TextButton&> cancelClicked;
    touchgfx::Callback<SettingsView, const gui::TextButton&> restoreClicked;

    touchgfx::Unicode::UnicodeChar labelBuffers[SETTING_COUNT][16];
    touchgfx::Unicode::UnicodeChar statusBuffer[24];
};

#endif // SETTINGSVIEW_HPP
