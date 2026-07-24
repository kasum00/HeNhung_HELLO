/**
 * @file    WaveformView.cpp
 * @brief   Waveform screen implementation (mock trace, ~20 Hz histogram plot).
 * @note    Owner: user (non-generated).
 */

#include <gui/waveform_screen/WaveformView.hpp>
#include <gui/common/GuiTheme.hpp>
#include <gui/common/GuiSnapshots.hpp>
#include <touchgfx/TypedText.hpp>
#include <touchgfx/canvas_widget_renderer/CanvasWidgetRenderer.hpp>
#include <texts/TextKeysAndLanguages.hpp>
#include <cstdio>

using namespace touchgfx;
using namespace gui;

namespace
{
constexpr int16_t PLOT_Y = layout::waveform::PLOT_Y;
constexpr int16_t PLOT_H = layout::waveform::PLOT_H;
constexpr int16_t PLOT_W = layout::waveform::PLOT_W;  /* full width, 1:1 index->pixel */
constexpr uint32_t REFRESH_DIVISOR = 3U;              /* ~20 Hz at 60 Hz tick */
constexpr int16_t PEAK_MARKER_W = layout::waveform::PEAK_MARKER_W;
constexpr int16_t PEAK_MARKER_H = layout::waveform::PEAK_MARKER_H;
constexpr int GRID_DIVISIONS = 4;                     /* horizontal grid lines */

/* Scratch buffer for the Canvas Widget Renderer (needed by the line trace).
   Shared globally by CWR; set up once via hasBuffer() guard. */
uint8_t canvasBuffer[6000];

int16_t indexToX(uint16_t index)
{
    /* Range 0..(N-1) maps across PLOT_W with no margins. */
    return static_cast<int16_t>((static_cast<int32_t>(index) * (PLOT_W - 1)) /
                                (WAVEFORM_POINTS - 1));
}
}

WaveformView::WaveformView()
    : channelClicked(this, &WaveformView::onChannel),
      filterModeClicked(this, &WaveformView::onFilterMode),
      windowClicked(this, &WaveformView::onWindow),
      tickCounter(0U),
      showRed(false),
      filterMode(FilterMode::MovingAverage),
      maWindow(5U)
{
    infoBuffer[0] = 0;
}

void WaveformView::setupScreen()
{
    WaveformViewBase::setupScreen();

    background.setPosition(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    background.setColor(theme::background());
    add(background);

    /* Control row: channel (IR/RED) | filter mode (Raw/Mov Avg) | window (N). */
    const int16_t cy = layout::waveform::CTRL_Y;
    const int16_t ch = layout::waveform::CTRL_H;
    channelButton.setup(8, cy, 52, ch);
    channelButton.setColors(theme::surface(), theme::primaryDark(), theme::textPrimary());
    channelButton.setLabel("IR");
    channelButton.setAction(channelClicked);
    add(channelButton);

    modeButton.setup(64, cy, 100, ch);
    modeButton.setColors(theme::surface(), theme::primaryDark(), theme::textPrimary());
    modeButton.setAction(filterModeClicked);
    add(modeButton);

    windowButton.setup(168, cy, 64, ch);
    windowButton.setColors(theme::surface(), theme::primaryDark(), theme::textPrimary());
    windowButton.setAction(windowClicked);
    add(windowButton);

    updateControlLabels(filterMode, maWindow);

    /* The line trace is a canvas widget; make sure CWR has a buffer. */
    if (!CanvasWidgetRenderer::hasBuffer())
    {
        CanvasWidgetRenderer::setupBuffer(canvasBuffer, sizeof(canvasBuffer));
    }

    plotBackground.setPosition(0, PLOT_Y, PLOT_W, PLOT_H);
    plotBackground.setColor(theme::statusBar());
    add(plotBackground);

    graph.setPosition(0, PLOT_Y, PLOT_W, PLOT_H);
    graph.setGraphAreaMargin(0, 0, 0, 0);
    graph.setGraphRange(0, WAVEFORM_POINTS - 1, 0, WAVEFORM_FULL_SCALE);

    /* Oscilloscope backdrop: faint horizontal divisions plus a brighter centre
       (zero) line, drawn behind the trace. */
    gridY.setColor(theme::waveGrid());
    gridY.setLineWidth(1);
    gridY.setInterval(WAVEFORM_FULL_SCALE / GRID_DIVISIONS);
    graph.addGraphElement(gridY);

    gridCenter.setColor(theme::neutral());
    gridCenter.setLineWidth(1);
    gridCenter.setInterval(WAVEFORM_FULL_SCALE / 2);
    graph.addGraphElement(gridCenter);

    linePainter.setColor(theme::waveIr());
    signalLine.setPainter(linePainter);
    signalLine.setLineWidth(2);
    graph.addGraphElement(signalLine);
    add(graph);

    for (uint8_t i = 0U; i < WAVEFORM_MAX_PEAKS; ++i)
    {
        peakMarkers[i].setPosition(0, PLOT_Y, PEAK_MARKER_W, PEAK_MARKER_H);
        peakMarkers[i].setColor(theme::wavePeak());
        peakMarkers[i].setVisible(false);
        add(peakMarkers[i]);
    }

    infoText.setPosition(layout::MARGIN, layout::waveform::INFO_Y, SCREEN_WIDTH - 16,
                         layout::waveform::INFO_H);
    infoText.setColor(theme::textSecondary());
    infoText.setTypedText(TypedText(T_WCSMALLLEFT));
    infoText.setWildcard1(infoBuffer);
    add(infoText);

    topBar.setup("Waveform", ScreenId::Dashboard);
    add(topBar);

    tickCounter = 0U;
    refresh();
}

void WaveformView::tearDownScreen()
{
    WaveformViewBase::tearDownScreen();
}

void WaveformView::onChannel(const TextButton& /*button*/)
{
    showRed = !showRed;
    channelButton.setLabel(showRed ? "RED" : "IR");
    linePainter.setColor(showRed ? theme::waveRed() : theme::waveIr());
    signalLine.invalidate();
    refresh();
}

void WaveformView::updateControlLabels(FilterMode mode, uint8_t window)
{
    modeButton.setLabel((mode == FilterMode::Raw) ? "Raw" : "Mov Avg");
    char buf[8];
    (void)snprintf(buf, sizeof(buf), "N %u", (unsigned)window);
    windowButton.setLabel(buf);
    modeButton.invalidate();
    windowButton.invalidate();
}

void WaveformView::onFilterMode(const TextButton& /*button*/)
{
    /* Toggle the GLOBAL filter mode: affects both this plot and the dashboard
       values (the engine recomputes on the selected signal). */
    const FilterMode next = (filterMode == FilterMode::Raw) ? FilterMode::MovingAverage
                                                            : FilterMode::Raw;
    presenter->postCommand(makeSelectFilter(next));
    filterMode = next;
    updateControlLabels(filterMode, maWindow);
    refresh();
}

void WaveformView::onWindow(const TextButton& /*button*/)
{
    /* Cycle the moving-average window through a few useful sizes. */
    static const uint8_t steps[] = {3U, 5U, 7U, 9U, 11U};
    uint8_t idx = 0U;
    for (uint8_t i = 0U; i < 5U; ++i) { if (steps[i] == maWindow) { idx = i; break; } }
    maWindow = steps[(idx + 1U) % 5U];
    presenter->postCommand(makeSetFilterWindow(maWindow));
    updateControlLabels(filterMode, maWindow);
}

void WaveformView::refresh()
{
    GuiWaveformSnapshot w;
    if (!presenter->data().getWaveformSnapshot(w))
    {
        return;
    }

    /* Keep the control labels in sync with the engine's actual filter state. */
    GuiMeasurementSnapshot m;
    if (presenter->data().getMeasurementSnapshot(m) &&
        ((m.filterMode != filterMode) || (m.maWindow != maWindow)))
    {
        filterMode = m.filterMode;
        maWindow = m.maWindow;
        updateControlLabels(filterMode, maWindow);
    }

    /* The trace already reflects the selected filter mode (the engine maps raw or
       moving-average into the waveform); the view does not filter. */
    const int16_t* src = showRed ? w.redSamples : w.irSamples;

    graph.clear();
    for (uint16_t i = 0U; i < w.count; ++i)
    {
        /* Invert around the centre so the heartbeat reads as an upward peak
           (raw IR dips at systole; flipping makes the beat point up). */
        const int value = WAVEFORM_FULL_SCALE - src[i];
        graph.addDataPoint(value);
    }

    /* Position peak markers from the snapshot; hide unused ones. */
    for (uint8_t i = 0U; i < WAVEFORM_MAX_PEAKS; ++i)
    {
        if (i < w.peakCount)
        {
            const int16_t x = indexToX(w.peakIndices[i]);
            peakMarkers[i].setX(x);
            peakMarkers[i].setVisible(true);
        }
        else
        {
            peakMarkers[i].setVisible(false);
        }
        peakMarkers[i].invalidate();
    }

    Unicode::UnicodeChar sensorU[10];
    Unicode::strncpy(sensorU, toText(w.sensorStatus), 10);
    Unicode::snprintf(infoBuffer, 36, "%u Hz  Sensor %s  Drop %u",
                      w.sampleRateHz, sensorU, static_cast<unsigned int>(w.droppedSamples));
    infoText.setWildcard1(infoBuffer);
    infoText.invalidate();
}

void WaveformView::handleTickEvent()
{
    ++tickCounter;
    if ((tickCounter % REFRESH_DIVISOR) == 0U)
    {
        refresh();
    }
}
