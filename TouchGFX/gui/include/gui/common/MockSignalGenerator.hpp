#ifndef MOCK_SIGNAL_GENERATOR_HPP
#define MOCK_SIGNAL_GENERATOR_HPP

/**
 * @file    MockSignalGenerator.hpp
 * @brief   Synthesizes a plausible PPG waveform and derived metrics per scenario.
 *
 * This is the stand-in for the sensor + DSP pipeline during the UI-foundation
 * phase. It is NOT signal processing of real data: it generates a display-only
 * signal so screens can be exercised without hardware. It owns a fixed circular
 * history of samples (no dynamic allocation) and derives waveform, measurement
 * and DSP snapshots from the active @ref gui::MockScenario.
 *
 * @note  Owner: user (non-generated). No TouchGFX / HAL / driver dependency.
 */

#include <gui/common/GuiSnapshots.hpp>

namespace gui
{
/** @brief Generates mock PPG waveforms and metrics for a given scenario. */
class MockSignalGenerator
{
public:
    /** Internal synthesis sample rate (samples per second). */
    static constexpr uint16_t SAMPLE_RATE_HZ = 100U;

    MockSignalGenerator();

    /**
     * @brief Selects the scenario driving subsequent synthesis.
     * @param scenario Scenario to activate.
     */
    void setScenario(MockScenario scenario);

    /** @brief Returns the active scenario. */
    MockScenario scenario() const { return activeScenario; }

    /**
     * @brief Advances the generator by @p samples internal samples.
     * @param samples Number of 100 Hz samples to synthesize and buffer.
     *
     * Called from the provider tick, sized so the average rate matches
     * @ref SAMPLE_RATE_HZ. Keeps a rolling window sufficient to fill a waveform
     * snapshot.
     */
    void advance(uint16_t samples);

    /**
     * @brief Fills a waveform snapshot from the current rolling window.
     * @param[out] snapshot Destination snapshot (buffers filled in place).
     * @param generation    Generation counter to stamp into the snapshot.
     */
    void fillWaveform(GuiWaveformSnapshot& snapshot, uint32_t generation) const;

    /**
     * @brief Fills measurement values (BPM/SpO2/SQI/validity) for the scenario.
     * @param[out] snapshot Destination measurement snapshot (partially filled:
     *                      only signal-derived fields; caller sets time/state).
     */
    void fillMeasurement(GuiMeasurementSnapshot& snapshot) const;

    /** @brief True when the scenario represents a usable (valid) signal. */
    bool signalValid() const;

    /** @brief Instantaneous signal quality index in [0, 100] for the scenario. */
    float sqiPercent() const;

private:
    /** @brief Parameters describing how a scenario shapes the signal. */
    struct Shape
    {
        float amplitude;   /**< Pulse amplitude (fraction of full scale). */
        float noise;       /**< Noise amplitude (fraction of full scale). */
        float baseline;    /**< DC baseline (fraction of full scale).     */
        bool  clipped;     /**< Whether the waveform saturates.           */
        bool  flat;        /**< Whether there is no pulsatile component.  */
    };

    Shape shapeForScenario() const;
    float nextNoise();          /**< Deterministic pseudo-random noise in [-1, 1]. */
    int16_t sampleAt(float phase, const Shape& shape, float noise) const;

    MockScenario activeScenario;
    float bpmBase;              /**< Slowly wandering base heart rate. */
    float phase;               /**< Current pulse phase in [0, 1).    */
    uint32_t rngState;         /**< xorshift PRNG state.              */

    int16_t irWindow[WAVEFORM_POINTS];  /**< Rolling IR samples.  */
    int16_t redWindow[WAVEFORM_POINTS]; /**< Rolling RED samples. */
    uint16_t writeIndex;                /**< Next write position (circular). */
    uint32_t sampleCount;               /**< Total samples synthesized.      */
};

} // namespace gui

#endif // MOCK_SIGNAL_GENERATOR_HPP
