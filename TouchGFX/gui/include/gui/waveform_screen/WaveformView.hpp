#ifndef WAVEFORMVIEW_HPP
#define WAVEFORMVIEW_HPP

/**
 * @file    WaveformView.hpp
 * @brief   Waveform screen: mock PPG trace (IR/RED), raw/filtered, peak markers.
 *
 * The signal comes from the waveform snapshot (never generated in the view).
 * The plot updates at ~20 Hz: a line trace over faint horizontal grid lines
 * (oscilloscope / Arduino-plotter style, uses the Canvas Widget Renderer).
 * Peak markers are a fixed pool of small boxes positioned from the snapshot.
 *
 * @note  Owner: user (non-generated).
 */

#include <gui_generated/waveform_screen/WaveformViewBase.hpp>
#include <gui/waveform_screen/WaveformPresenter.hpp>
#include <gui/widgets/TopBar.hpp>
#include <gui/widgets/TextButton.hpp>
#include <gui/common/GuiTypes.hpp>
#include <touchgfx/widgets/Box.hpp>
#include <touchgfx/widgets/TextAreaWithWildcard.hpp>
#include <touchgfx/widgets/graph/GraphWrapAndOverwrite.hpp>
#include <touchgfx/widgets/graph/GraphElements.hpp>
#include <touchgfx/widgets/canvas/PainterRGB565.hpp>
#include <touchgfx/Unicode.hpp>
#include <touchgfx/Callback.hpp>

class WaveformView : public WaveformViewBase
{
public:
    WaveformView();
    virtual ~WaveformView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    virtual void handleTickEvent();

protected:
    void refresh();
    void onChannel(const gui::TextButton& button);
    void onFilterMode(const gui::TextButton& button);
    void onWindow(const gui::TextButton& button);
    void updateControlLabels(gui::FilterMode mode, uint8_t window);

    touchgfx::Box background;
    gui::TopBar topBar;
    touchgfx::Box plotBackground;
    touchgfx::GraphWrapAndOverwrite<gui::WAVEFORM_POINTS> graph;
    touchgfx::GraphElementGridY gridY;       /* faint minor grid lines          */
    touchgfx::GraphElementGridY gridCenter;  /* brighter zero/centre line       */
    touchgfx::GraphElementLine signalLine;   /* Arduino-plotter style line trace */
    touchgfx::PainterRGB565 linePainter;     /* colour for the line (CWR)       */
    touchgfx::Box peakMarkers[gui::WAVEFORM_MAX_PEAKS];
    gui::TextButton channelButton;
    gui::TextButton modeButton;    /* filter mode: Raw <-> Moving Avg */
    gui::TextButton windowButton;  /* moving-average window N cycle    */
    touchgfx::TextAreaWithOneWildcard infoText;

    touchgfx::Callback<WaveformView, const gui::TextButton&> channelClicked;
    touchgfx::Callback<WaveformView, const gui::TextButton&> filterModeClicked;
    touchgfx::Callback<WaveformView, const gui::TextButton&> windowClicked;

    touchgfx::Unicode::UnicodeChar infoBuffer[36];

    uint32_t tickCounter;
    bool showRed;
    gui::FilterMode filterMode;    /* last-known engine filter mode  */
    uint8_t maWindow;              /* last-known MA window N          */
};

#endif // WAVEFORMVIEW_HPP
