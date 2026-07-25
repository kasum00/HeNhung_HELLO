/**
 * @file    BootView.cpp
 * @brief   Boot splash implementation (mock init sequence, tick-driven).
 * @note    Owner: user (non-generated).
 */

#include <gui/boot_screen/BootView.hpp>
#include <gui/common/GuiTheme.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <touchgfx/TypedText.hpp>
#include <texts/TextKeysAndLanguages.hpp>

using namespace touchgfx;
using namespace gui;

namespace
{
/* Mock initialization steps shown on the boot screen. */
const char* const BOOT_STEPS[] = {
    "Starting system",
    "Initializing display",
    "Checking sensor",
    "Checking RTC",
    "Loading configuration",
    "Ready"
};
constexpr uint8_t BOOT_STEP_COUNT = static_cast<uint8_t>(sizeof(BOOT_STEPS) / sizeof(BOOT_STEPS[0]));
constexpr uint32_t TICKS_PER_STEP = 30U;   /**< ~0.5 s per step at 60 Hz. */
constexpr uint32_t TICKS_HOLD_END = 40U;   /**< Hold "Ready" before leaving. */

constexpr int16_t BAR_X = layout::boot::BAR_X;
constexpr int16_t BAR_W = layout::boot::BAR_W;
constexpr int16_t BAR_Y = layout::boot::BAR_Y;
constexpr int16_t BAR_H = layout::boot::BAR_H;
}

BootView::BootView()
    : tickCounter(0U),
      stepIndex(0U),
      navigationRequested(false)
{
    titleBuffer[0] = 0;
    versionBuffer[0] = 0;
    statusBuffer[0] = 0;
}

void BootView::setupScreen()
{
    BootViewBase::setupScreen();

    background.setPosition(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    background.setColor(theme::background());
    add(background);

    /* Simple logo mark: a filled accent square. */
    logo.setPosition(static_cast<int16_t>((SCREEN_WIDTH - layout::boot::LOGO_SIZE) / 2),
                     layout::boot::LOGO_Y, layout::boot::LOGO_SIZE, layout::boot::LOGO_SIZE);
    logo.setColor(theme::primary());
    add(logo);

    titleText.setPosition(0, layout::boot::TITLE_Y, SCREEN_WIDTH, layout::boot::TITLE_H);
    titleText.setColor(theme::textPrimary());
    titleText.setTypedText(TypedText(T_WCDEFAULTCENTER));
    Unicode::strncpy(titleBuffer, "PPG Analyzer", 24);
    titleText.setWildcard1(titleBuffer);
    add(titleText);

    versionText.setPosition(0, layout::boot::VERSION_Y, SCREEN_WIDTH, layout::boot::VERSION_H);
    versionText.setColor(theme::textSecondary());
    versionText.setTypedText(TypedText(T_WCSMALLCENTER));
    Unicode::strncpy(versionBuffer, "Hello-coder-lor", 24);
    versionText.setWildcard1(versionBuffer);
    add(versionText);

    progressTrack.setPosition(BAR_X, BAR_Y, BAR_W, BAR_H);
    progressTrack.setColor(theme::surfaceAlt());
    add(progressTrack);

    progressFill.setPosition(BAR_X, BAR_Y, 1, BAR_H);
    progressFill.setColor(theme::primary());
    add(progressFill);

    statusText.setPosition(0, layout::boot::STATUS_Y, SCREEN_WIDTH, layout::boot::STATUS_H);
    statusText.setColor(theme::textSecondary());
    statusText.setTypedText(TypedText(T_WCDEFAULTCENTER));
    add(statusText);

    tickCounter = 0U;
    stepIndex = 0U;
    navigationRequested = false;
    updateStep();
}

void BootView::tearDownScreen()
{
    BootViewBase::tearDownScreen();
}

void BootView::updateStep()
{
    const uint8_t clamped = (stepIndex < BOOT_STEP_COUNT) ? stepIndex
                                                          : static_cast<uint8_t>(BOOT_STEP_COUNT - 1U);
    Unicode::strncpy(statusBuffer, BOOT_STEPS[clamped], 28);
    statusText.setWildcard1(statusBuffer);
    statusText.invalidate();

    /* Progress reflects completed steps. */
    const int16_t filled = static_cast<int16_t>(
        (static_cast<int32_t>(BAR_W) * (clamped + 1)) / BOOT_STEP_COUNT);
    progressFill.setWidth((filled < 1) ? 1 : filled);
    progressFill.invalidate();
}

void BootView::handleTickEvent()
{
    ++tickCounter;

    if (stepIndex < (BOOT_STEP_COUNT - 1U))
    {
        if (tickCounter >= TICKS_PER_STEP)
        {
            tickCounter = 0U;
            ++stepIndex;
            updateStep();
        }
        return;
    }

    /* On the final step ("Ready"), hold briefly then move to Home once. */
    if (!navigationRequested && (tickCounter >= TICKS_HOLD_END))
    {
        navigationRequested = true;
        FrontendApplication* app = static_cast<FrontendApplication*>(Application::getInstance());
        app->requestScreen(ScreenId::Home);
    }
}
