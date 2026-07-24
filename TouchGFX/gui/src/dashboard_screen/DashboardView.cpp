/**
 * @file    DashboardView.cpp
 * @brief   Measurement dashboard implementation.
 * @note    Owner: user (non-generated).
 */

#include <gui/dashboard_screen/DashboardView.hpp>
#include <gui/common/GuiTheme.hpp>
#include <gui/common/FrontendApplication.hpp>
#include <gui/common/GuiSnapshots.hpp>
#include <touchgfx/TypedText.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <cstdio>

using namespace touchgfx;
using namespace gui;

namespace
{
constexpr uint32_t REFRESH_DIVISOR = 12U; /**< ~5 Hz metric refresh at 60 Hz. */

int32_t roundTo(float v)
{
    return static_cast<int32_t>(v + 0.5F);
}

/** @brief Colour representing a measurement lifecycle state. */
colortype stateColor(MeasurementState state)
{
    switch (state)
    {
    case MeasurementState::ResultReady:   return theme::ok();
    case MeasurementState::Measuring:
    case MeasurementState::Stabilizing:   return theme::primary();
    case MeasurementState::WaitFinger:    return theme::warning();
    case MeasurementState::InvalidSignal:
    case MeasurementState::SensorError:
    case MeasurementState::StorageError:  return theme::error();
    case MeasurementState::Idle:
    default:                              return theme::neutral();
    }
}
}

DashboardView::DashboardView()
    : startStopClicked(this, &DashboardView::onStartStop),
      waveformClicked(this, &DashboardView::onWaveform),
      tickCounter(0U),
      measuringNow(false)
{
    reasonBuffer[0] = 0;
    infoBuffer[0] = 0;
}

void DashboardView::setupScreen()
{
    DashboardViewBase::setupScreen();

    background.setPosition(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    background.setColor(theme::background());
    add(background);

    topBar.setup("Dashboard", ScreenId::Home);
    add(topBar);

    namespace L = layout::dashboard;
    stateBadge.setup(layout::MARGIN, L::BADGE_Y, layout::CONTENT_W, L::BADGE_H);
    add(stateBadge);

    /* Three metric cards with distinct accent colours. */
    bpmCard.setup(L::CARD1_X, L::CARD_Y, L::CARD_W, L::CARD_H, "BPM", "bpm");
    bpmCard.setAccentColor(theme::primary());
    add(bpmCard);
    spo2Card.setup(L::CARD2_X, L::CARD_Y, L::CARD_W, L::CARD_H, "SpO2", "%");
    spo2Card.setAccentColor(theme::ok());
    add(spo2Card);
    sqiCard.setup(L::CARD3_X, L::CARD_Y, L::CARD_W, L::CARD_H, "SQI", "%");
    sqiCard.setAccentColor(theme::warning());
    add(sqiCard);

    reasonText.setPosition(layout::MARGIN, L::REASON_Y, layout::CONTENT_W, L::REASON_H);
    reasonText.setColor(theme::warning());
    reasonText.setTypedText(TypedText(T_WCDEFAULTCENTER));
    reasonText.setWildcard1(reasonBuffer);
    add(reasonText);

    infoText.setPosition(layout::MARGIN, L::INFO_Y, layout::CONTENT_W, L::INFO_H);
    infoText.setColor(theme::textSecondary());
    infoText.setTypedText(TypedText(T_WCSMALLCENTER));
    infoText.setWildcard1(infoBuffer);
    add(infoText);

    startStopButton.setup(L::ACTION1_X, L::ACTION_Y, L::ACTION_W, L::ACTION_H);
    startStopButton.setColors(theme::ok(), theme::primaryDark(), theme::textOnPrimary());
    startStopButton.setLabel("Start");
    startStopButton.setAction(startStopClicked);
    add(startStopButton);

    waveformButton.setup(L::ACTION2_X, L::ACTION_Y, L::ACTION_W, L::ACTION_H);
    waveformButton.setColors(theme::surfaceAlt(), theme::primaryDark(), theme::textPrimary());
    waveformButton.setLabel("Waveform");
    waveformButton.setAction(waveformClicked);
    add(waveformButton);

    tickCounter = 0U;
    refresh();
}

void DashboardView::tearDownScreen()
{
    DashboardViewBase::tearDownScreen();
}

void DashboardView::refresh()
{
    GuiMeasurementSnapshot m;
    if (!presenter->data().getMeasurementSnapshot(m))
    {
        return;
    }

    stateBadge.set(stateColor(m.state), toText(m.state));

    const bool result = (m.state == MeasurementState::ResultReady);

    /* Cards show live values while measuring, the frozen session averages on the
       result screen. */
    const int bpmVal = result ? roundTo(m.averageBpm) : roundTo(m.bpm);
    const bool bpmOk = result ? m.averageBpmValid : m.bpmValid;
    const int spo2Val = result ? roundTo(m.averageSpo2) : roundTo(m.spo2Percent);
    const bool spo2Ok = result ? m.averageSpo2Valid : m.spo2Valid;

    bpmCard.setValue(bpmVal, bpmOk);
    bpmCard.setValueColor(bpmOk ? theme::textPrimary() : theme::neutral());
    spo2Card.setValue(spo2Val, spo2Ok);
    spo2Card.setValueColor(spo2Ok ? theme::textPrimary() : theme::neutral());

    sqiCard.setValue(roundTo(m.sqiPercent), true);
    sqiCard.setValueColor(theme::qualityColor(classifySignalQuality(m.sqiPercent)));

    /* Prominent status line. */
    if (m.stale)
    {
        Unicode::strncpy(reasonBuffer, "Result expired", 24);
    }
    else if (m.state == MeasurementState::WaitFinger)
    {
        Unicode::strncpy(reasonBuffer, "Place finger on sensor", 24);
    }
    else if (m.state == MeasurementState::Stabilizing)
    {
        Unicode::snprintf(reasonBuffer, 24, "Stabilizing %d%%",
                          static_cast<int>(m.stabilizationProgress + 0.5F));
    }
    else if (m.state == MeasurementState::Measuring)
    {
        Unicode::snprintf(reasonBuffer, 24, "Measuring %ds",
                          static_cast<int>(m.elapsedMeasurementMs / 1000U));
    }
    else if (result)
    {
        Unicode::strncpy(reasonBuffer,
                         m.temporarilySaved ? "Saved temporarily" : "Not saved (too short)", 24);
    }
    else if (m.invalidReason != MeasurementInvalidReason::None)
    {
        Unicode::strncpy(reasonBuffer, toText(m.invalidReason), 24);
    }
    else
    {
        Unicode::strncpy(reasonBuffer, "", 24);
    }
    reasonText.setWildcard1(reasonBuffer);
    reasonText.invalidate();

    /* Secondary line: session averages while measuring / on result, else status. */
    char line[40];
    if ((m.state == MeasurementState::Measuring) || result)
    {
        char bpmStr[8];
        char spo2Str[8];
        /* roundTo() trả int32_t (là long trên target) -> ép về int cho "%d". */
        if (m.averageBpmValid) { (void)snprintf(bpmStr, sizeof(bpmStr), "%d", static_cast<int>(roundTo(m.averageBpm))); }
        else { (void)snprintf(bpmStr, sizeof(bpmStr), "--"); }
        if (m.averageSpo2Valid) { (void)snprintf(spo2Str, sizeof(spo2Str), "%d", static_cast<int>(roundTo(m.averageSpo2))); }
        else { (void)snprintf(spo2Str, sizeof(spo2Str), "--"); }
        /* Hiển thị min-max khi có kết quả chốt, avg khi đang đo. */
        if (result && m.averageBpmValid)
        {
            (void)snprintf(line, sizeof(line), "%d-%d bpm  SpO2 %d-%d%%",
                           static_cast<int>(roundTo(m.bpmMin)),
                           static_cast<int>(roundTo(m.bpmMax)),
                           static_cast<int>(roundTo(m.spo2Min)),
                           static_cast<int>(roundTo(m.spo2Max)));
        }
        else
        {
            (void)snprintf(line, sizeof(line), "Avg %s bpm  SpO2 %s%%  n%u",
                           bpmStr, spo2Str, static_cast<unsigned>(m.validPeakCount));
        }
        Unicode::strncpy(infoBuffer, line, 36);
    }
    else
    {
        Unicode::UnicodeChar sensorU[10];
        Unicode::UnicodeChar storageU[10];
        Unicode::strncpy(sensorU, toText(m.sensorStatus), 10);
        Unicode::strncpy(storageU, toText(m.storageStatus), 10);
        Unicode::snprintf(infoBuffer, 36, "Sensor %s  SD %s", sensorU, storageU);
    }
    infoText.setWildcard1(infoBuffer);
    infoText.invalidate();

    const bool measuring = (m.state != MeasurementState::Idle);
    if (measuring != measuringNow)
    {
        measuringNow = measuring;
        startStopButton.setLabel(measuring ? "Stop" : "Start");
        startStopButton.setColors(measuring ? theme::error() : theme::ok(),
                                  theme::primaryDark(), theme::textOnPrimary());
    }
}

void DashboardView::onStartStop(const TextButton& /*button*/)
{
    presenter->postCommand(makeCommand(measuringNow ? GuiCommandType::StopMeasurement
                                                    : GuiCommandType::StartMeasurement));
    refresh();
}

void DashboardView::onWaveform(const TextButton& /*button*/)
{
    FrontendApplication* app = static_cast<FrontendApplication*>(Application::getInstance());
    app->requestScreen(ScreenId::Waveform);
}

void DashboardView::handleTickEvent()
{
    ++tickCounter;
    if ((tickCounter % REFRESH_DIVISOR) == 0U)
    {
        refresh();
    }
}
