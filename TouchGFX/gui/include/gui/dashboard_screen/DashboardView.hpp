#ifndef DASHBOARDVIEW_HPP
#define DASHBOARDVIEW_HPP

/**
 * @file    DashboardView.hpp
 * @brief   Measurement dashboard: BPM/SpO2/SQI, state, Start/Stop, invalid info.
 * @note    Owner: user (non-generated).
 */

#include <gui_generated/dashboard_screen/DashboardViewBase.hpp>
#include <gui/dashboard_screen/DashboardPresenter.hpp>
#include <gui/widgets/TopBar.hpp>
#include <gui/widgets/MetricCard.hpp>
#include <gui/widgets/StatusBadge.hpp>
#include <gui/widgets/TextButton.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/Unicode.hpp>
#include <touchgfx/Callback.hpp>

class DashboardView : public DashboardViewBase
{
public:
    DashboardView();
    virtual ~DashboardView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();

protected:
    void refresh();
    void onStartStop(const gui::TextButton& button);
    void onWaveform(const gui::TextButton& button);

    touchgfx::Box background;
    gui::TopBar topBar;
    gui::StatusBadge stateBadge;
    gui::MetricCard bpmCard;
    gui::MetricCard spo2Card;
    gui::MetricCard sqiCard;
    touchgfx::TextAreaWithOneWildcard reasonText;
    touchgfx::TextAreaWithOneWildcard infoText;
    gui::TextButton startStopButton;
    gui::TextButton waveformButton;

    touchgfx::Callback<DashboardView, const gui::TextButton&> startStopClicked;
    touchgfx::Callback<DashboardView, const gui::TextButton&> waveformClicked;

    touchgfx::Unicode::UnicodeChar reasonBuffer[24];
    touchgfx::Unicode::UnicodeChar infoBuffer[36];

    uint32_t tickCounter;
    bool measuringNow;
};

#endif // DASHBOARDVIEW_HPP
