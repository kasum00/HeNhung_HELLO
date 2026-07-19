#ifndef MOCK_GUI_DATA_PROVIDER_HPP
#define MOCK_GUI_DATA_PROVIDER_HPP

/**
 * @file    MockGuiDataProvider.hpp
 * @brief   Mock implementation of @ref gui::IGuiDataProvider.
 *
 * Aggregates the mock sub-sources (signal generator, history, clock) and hosts
 * the simulated measurement, configuration and calibration state machines. It is
 * the ONLY place that knows the data is synthetic; presenters and views see only
 * @ref gui::IGuiDataProvider, so replacing this with an application-backed bridge
 * requires no view changes.
 *
 * All state is fixed-size and value-owned: no dynamic allocation, no blocking.
 *
 * @note  Owner: user (non-generated). No TouchGFX / HAL / driver dependency.
 */

#include <gui/common/IGuiDataProvider.hpp>
#include <gui/common/MockSignalGenerator.hpp>
#include <gui/common/MockHistoryProvider.hpp>
#include <gui/common/MockClock.hpp>

namespace gui
{
/** @brief Synthetic data source used during the UI-foundation phase. */
class MockGuiDataProvider : public IGuiDataProvider
{
public:
    MockGuiDataProvider();

    /* IGuiDataProvider */
    void tick(uint32_t frameCounter) override;
    void postCommand(const GuiCommand& command) override;
    void notifyScreenTransition() override {}
    bool getMeasurementSnapshot(GuiMeasurementSnapshot& snapshot) override;
    bool getWaveformSnapshot(GuiWaveformSnapshot& snapshot) override;
    bool getHistoryPage(uint16_t pageIndex, GuiHistoryPageSnapshot& snapshot) override;
    bool getConfigurationSnapshot(GuiConfigurationSnapshot& snapshot) override;
    bool getSystemInfoSnapshot(GuiSystemInfoSnapshot& snapshot) override;

    /** @brief Returns the active test scenario. */
    MockScenario scenario() const { return signal.scenario(); }

private:
    void selectScenario(MockScenario scenario);
    void resolveMeasurementState();
    bool draftDiffersFromActive() const;

    /* Sub-sources. */
    MockSignalGenerator signal;
    MockHistoryProvider history;
    MockClock clock;

    /* Time base. */
    uint32_t lastFrame;        /**< Last frame index seen by tick().        */
    uint32_t seconds;          /**< Elapsed seconds (1 Hz gate).            */
    float sampleDebt;          /**< Fractional 100 Hz samples owed.         */
    uint32_t generationCounter;/**< Monotonic snapshot generation source.   */

    /* Measurement state. */
    bool measuring;            /**< True between Start and Stop.            */
    uint32_t measureStartSecond;
    MeasurementState measurementState;
    MeasurementInvalidReason measurementReason;
    bool resultStale;

    /* Configuration (active + draft). */
    GuiConfigurationSnapshot activeConfig;
    GuiConfigurationSnapshot draftConfig;

    /* Waveform/measurement filter selection (applied immediately, mirrors the
       real engine's global filter mode + window). */
    FilterMode measFilterMode = FilterMode::MovingAverage;
    uint8_t measWindow = 5U;

};

} // namespace gui

#endif // MOCK_GUI_DATA_PROVIDER_HPP
