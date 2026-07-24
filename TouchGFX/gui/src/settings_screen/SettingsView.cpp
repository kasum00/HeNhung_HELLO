/**
 * @file    SettingsView.cpp
 * @brief   Settings prototype implementation (draft + apply/cancel/restore).
 * @note    Owner: user (non-generated).
 */

#include <gui/settings_screen/SettingsView.hpp>
#include <gui/common/GuiTheme.hpp>
#include <gui/common/GuiSnapshots.hpp>
#include <touchgfx/TypedText.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <cstdio>

using namespace touchgfx;
using namespace gui;

namespace
{
constexpr int16_t ROW_TOP = layout::settings::ROW_TOP;
constexpr int16_t ROW_H = layout::settings::ROW_H;

const char* const SETTING_LABELS[SettingsView::SETTING_COUNT] = {
    "Filter", "Min SQI", "Logging", "Buzzer", "Adaptive LED", "Brightness"
};

/* Setting identifiers (also the value-button ids). */
enum SettingId
{
    ID_FILTER = 0,
    ID_MIN_SQI,
    ID_LOGGING,
    ID_BUZZER,
    ID_ADAPTIVE_LED,
    ID_BRIGHTNESS
};
}

SettingsView::SettingsView()
    : valueClicked(this, &SettingsView::onValue),
      applyClicked(this, &SettingsView::onApply),
      cancelClicked(this, &SettingsView::onCancel),
      restoreClicked(this, &SettingsView::onRestore)
{
    statusBuffer[0] = 0;
    for (uint8_t i = 0U; i < SETTING_COUNT; ++i)
    {
        labelBuffers[i][0] = 0;
    }
}

void SettingsView::setupScreen()
{
    SettingsViewBase::setupScreen();

    background.setPosition(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    background.setColor(theme::background());
    add(background);

    for (uint8_t i = 0U; i < SETTING_COUNT; ++i)
    {
        const int16_t y = static_cast<int16_t>(ROW_TOP + i * ROW_H);

        labels[i].setPosition(layout::MARGIN, static_cast<int16_t>(y + 4), layout::settings::LABEL_W, 18);
        labels[i].setColor(theme::textSecondary());
        labels[i].setTypedText(TypedText(T_WCMEDIUMLEFT));
        Unicode::strncpy(labelBuffers[i], SETTING_LABELS[i], 16);
        labels[i].setWildcard1(labelBuffers[i]);
        add(labels[i]);

        valueButtons[i].setup(layout::settings::VALUE_X, static_cast<int16_t>(y + 1),
                              layout::settings::VALUE_W, layout::settings::VALUE_H);
        valueButtons[i].setColors(theme::surface(), theme::primaryDark(), theme::textPrimary());
        valueButtons[i].setLabel("--");
        valueButtons[i].setId(static_cast<int16_t>(i));
        valueButtons[i].setAction(valueClicked);
        add(valueButtons[i]);
    }

    namespace L = layout::settings;
    statusText.setPosition(layout::MARGIN, L::STATUS_Y, layout::CONTENT_W, L::STATUS_H);
    statusText.setColor(theme::textSecondary());
    statusText.setTypedText(TypedText(T_WCSMALLCENTER));
    statusText.setWildcard1(statusBuffer);
    add(statusText);

    applyButton.setup(L::APPLY_X, L::ACTION_Y, L::ACTION_W, L::ACTION_H);
    applyButton.setColors(theme::ok(), theme::primaryDark(), theme::textOnPrimary());
    applyButton.setLabel("Apply");
    applyButton.setAction(applyClicked);
    add(applyButton);

    cancelButton.setup(L::CANCEL_X, L::ACTION_Y, L::ACTION_W, L::ACTION_H);
    cancelButton.setColors(theme::surfaceAlt(), theme::primaryDark(), theme::textPrimary());
    cancelButton.setLabel("Cancel");
    cancelButton.setAction(cancelClicked);
    add(cancelButton);

    restoreButton.setup(L::RESTORE_X, L::ACTION_Y, L::ACTION_W, L::ACTION_H);
    restoreButton.setColors(theme::surfaceAlt(), theme::primaryDark(), theme::textPrimary());
    restoreButton.setLabel("Reset");
    restoreButton.setAction(restoreClicked);
    add(restoreButton);

    topBar.setup("Settings", ScreenId::Home);
    add(topBar);

    refresh();
}

void SettingsView::tearDownScreen()
{
    SettingsViewBase::tearDownScreen();
}

void SettingsView::showStatus(const char* message, colortype color)
{
    Unicode::strncpy(statusBuffer, message, 24);
    statusText.setColor(color);
    statusText.setWildcard1(statusBuffer);
    statusText.invalidate();
}

void SettingsView::refresh()
{
    GuiConfigurationSnapshot c;
    if (!presenter->data().getConfigurationSnapshot(c))
    {
        return;
    }

    char buf[16];
    valueButtons[ID_FILTER].setLabel(toText(c.filterMode));
    snprintf(buf, sizeof(buf), "%u%%", static_cast<unsigned>(c.minimumSqiPercent));
    valueButtons[ID_MIN_SQI].setLabel(buf);
    valueButtons[ID_LOGGING].setLabel(c.loggingEnabled ? "On" : "Off");
    valueButtons[ID_BUZZER].setLabel(c.buzzerEnabled ? "On" : "Off");
    valueButtons[ID_ADAPTIVE_LED].setLabel(c.adaptiveLedEnabled ? "On" : "Off");
    snprintf(buf, sizeof(buf), "%u%%", static_cast<unsigned>(c.brightnessPercent));
    valueButtons[ID_BRIGHTNESS].setLabel(buf);

    if (c.dirty)
    {
        showStatus("Unsaved changes", theme::warning());
    }
    else
    {
        showStatus("Saved", theme::textSecondary());
    }
}

void SettingsView::onValue(const TextButton& button)
{
    GuiConfigurationSnapshot c;
    if (!presenter->data().getConfigurationSnapshot(c))
    {
        return;
    }

    switch (button.getId())
    {
    case ID_FILTER:
    {
        const uint8_t next = static_cast<uint8_t>((static_cast<uint8_t>(c.filterMode) + 1U) % 5U);
        presenter->postCommand(makeSelectFilter(static_cast<FilterMode>(next)));
        break;
    }
    case ID_MIN_SQI:
    {
        uint8_t next = static_cast<uint8_t>(c.minimumSqiPercent + 5U);
        if (next > 70U) { next = 30U; }
        presenter->postCommand(makeSetMinimumSqi(next));
        break;
    }
    case ID_LOGGING:
        presenter->postCommand(makeFlagCommand(GuiCommandType::SetLoggingEnabled, !c.loggingEnabled));
        break;
    case ID_BUZZER:
        presenter->postCommand(makeFlagCommand(GuiCommandType::SetBuzzerEnabled, !c.buzzerEnabled));
        break;
    case ID_ADAPTIVE_LED:
        presenter->postCommand(makeFlagCommand(GuiCommandType::SetAdaptiveLedEnabled, !c.adaptiveLedEnabled));
        break;
    case ID_BRIGHTNESS:
    {
        uint8_t next = static_cast<uint8_t>(c.brightnessPercent + 20U);
        if (next > 100U) { next = 20U; }
        presenter->postCommand(makeSetBrightness(next));
        break;
    }
    default:
        break;
    }
    refresh();
}

void SettingsView::onApply(const TextButton& /*button*/)
{
    presenter->postCommand(makeCommand(GuiCommandType::ApplySettings));
    refresh();
    showStatus("Applied", theme::ok());
}

void SettingsView::onCancel(const TextButton& /*button*/)
{
    presenter->postCommand(makeCommand(GuiCommandType::CancelSettings));
    refresh();
    showStatus("Cancelled", theme::neutral());
}

void SettingsView::onRestore(const TextButton& /*button*/)
{
    presenter->postCommand(makeCommand(GuiCommandType::RestoreDefaults));
    refresh();
    showStatus("Defaults restored", theme::warning());
}
