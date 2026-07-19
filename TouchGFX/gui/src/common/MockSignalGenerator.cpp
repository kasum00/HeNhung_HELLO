/**
 * @file    MockSignalGenerator.cpp
 * @brief   Implementation of the mock PPG signal + metric synthesizer.
 * @note    Owner: user (non-generated).
 */

#include <gui/common/MockSignalGenerator.hpp>
#include <cmath>

namespace gui
{
namespace
{
/** Two systolic-shape gaussians approximate one PPG cardiac cycle. */
constexpr float SYSTOLIC_CENTER   = 0.16F;
constexpr float SYSTOLIC_WIDTH    = 0.10F;
constexpr float DICROTIC_CENTER   = 0.46F;
constexpr float DICROTIC_WIDTH    = 0.13F;
constexpr float DICROTIC_WEIGHT   = 0.38F;

constexpr float RED_AMPLITUDE_RATIO = 0.82F; /**< RED pulsatility vs IR. */
constexpr float BPM_MIN = 55.0F;
constexpr float BPM_MAX = 105.0F;

/** @brief One normalized PPG cycle value in roughly [0, 1] for phase in [0,1). */
float ppgCycle(float phase)
{
    const float s = (phase - SYSTOLIC_CENTER) / SYSTOLIC_WIDTH;
    const float d = (phase - DICROTIC_CENTER) / DICROTIC_WIDTH;
    return expf(-(s * s)) + (DICROTIC_WEIGHT * expf(-(d * d)));
}

float clampf(float v, float lo, float hi)
{
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}
} // namespace

MockSignalGenerator::MockSignalGenerator()
    : activeScenario(MockScenario::Normal),
      bpmBase(74.0F),
      phase(0.0F),
      rngState(0x1234ABCDU),
      writeIndex(0U),
      sampleCount(0U)
{
    for (uint16_t i = 0U; i < WAVEFORM_POINTS; ++i)
    {
        irWindow[i] = 0;
        redWindow[i] = 0;
    }
}

void MockSignalGenerator::setScenario(MockScenario scenario)
{
    activeScenario = scenario;
}

float MockSignalGenerator::nextNoise()
{
    /* xorshift32: deterministic, no global state, no allocation. */
    uint32_t x = rngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rngState = x;
    /* Map to [-1, 1]. */
    const float unit = static_cast<float>(x & 0xFFFFFFU) / static_cast<float>(0xFFFFFFU);
    return (unit * 2.0F) - 1.0F;
}

MockSignalGenerator::Shape MockSignalGenerator::shapeForScenario() const
{
    Shape s{0.70F, 0.02F, 0.34F, false, false};
    switch (activeScenario)
    {
    case MockScenario::Normal:       s = {0.70F, 0.02F, 0.34F, false, false}; break;
    case MockScenario::NoFinger:     s = {0.00F, 0.010F, 0.06F, false, true};  break;
    case MockScenario::Stabilizing:  s = {0.42F, 0.09F, 0.30F, false, false}; break;
    case MockScenario::WeakSignal:   s = {0.12F, 0.05F, 0.18F, false, false}; break;
    case MockScenario::Motion:       s = {0.70F, 0.34F, 0.34F, false, false}; break;
    case MockScenario::Saturation:   s = {1.30F, 0.03F, 0.86F, true,  false}; break;
    case MockScenario::SensorError:  s = {0.00F, 0.004F, 0.02F, false, true};  break;
    case MockScenario::StorageError: s = {0.70F, 0.02F, 0.34F, false, false}; break;
    case MockScenario::LowSqi:       s = {0.50F, 0.13F, 0.32F, false, false}; break;
    case MockScenario::StaleResult:  s = {0.66F, 0.03F, 0.34F, false, false}; break;
    default:                         break;
    }
    return s;
}

int16_t MockSignalGenerator::sampleAt(float p, const Shape& shape, float noise) const
{
    float value;
    if (shape.flat)
    {
        value = shape.baseline + (noise * shape.noise);
    }
    else
    {
        value = shape.baseline + (shape.amplitude * ppgCycle(p)) + (noise * shape.noise);
    }
    if (shape.clipped)
    {
        value = clampf(value, 0.0F, 1.0F);
    }
    value = clampf(value, 0.0F, 1.0F);
    return static_cast<int16_t>(value * static_cast<float>(WAVEFORM_FULL_SCALE));
}

void MockSignalGenerator::advance(uint16_t samples)
{
    const Shape shape = shapeForScenario();

    /* Slowly wander the base heart rate for a lifelike display. */
    bpmBase = clampf(bpmBase + (nextNoise() * 0.05F), BPM_MIN, BPM_MAX);
    const float phaseStep = bpmBase / (60.0F * static_cast<float>(SAMPLE_RATE_HZ));

    for (uint16_t i = 0U; i < samples; ++i)
    {
        phase += phaseStep;
        if (phase >= 1.0F)
        {
            phase -= 1.0F;
        }

        const float n = nextNoise();
        const int16_t ir = sampleAt(phase, shape, n);

        Shape redShape = shape;
        redShape.amplitude *= RED_AMPLITUDE_RATIO;
        const int16_t red = sampleAt(phase, redShape, n);

        irWindow[writeIndex] = ir;
        redWindow[writeIndex] = red;
        writeIndex = static_cast<uint16_t>((writeIndex + 1U) % WAVEFORM_POINTS);
        ++sampleCount;
    }
}

void MockSignalGenerator::fillWaveform(GuiWaveformSnapshot& snapshot, uint32_t generation) const
{
    snapshot.generation = generation;
    snapshot.sampleRateHz = SAMPLE_RATE_HZ;
    snapshot.sensorStatus = SensorStatus::Ok;

    const bool sensorDown = (activeScenario == MockScenario::SensorError);
    snapshot.irChannelValid = !sensorDown;
    snapshot.redChannelValid = !sensorDown;
    if (sensorDown)
    {
        snapshot.sensorStatus = SensorStatus::Error;
    }

    /* Dropped-sample count only accrues in the sample-loss-like scenarios. */
    snapshot.droppedSamples =
        (activeScenario == MockScenario::Motion) ? (sampleCount / 40U) : 0U;

    const uint16_t available =
        (sampleCount < WAVEFORM_POINTS) ? static_cast<uint16_t>(sampleCount)
                                        : WAVEFORM_POINTS;
    snapshot.count = available;

    /* Emit chronologically: oldest sample first. */
    const uint16_t start = (sampleCount < WAVEFORM_POINTS)
                               ? 0U
                               : writeIndex;
    for (uint16_t i = 0U; i < available; ++i)
    {
        const uint16_t src = static_cast<uint16_t>((start + i) % WAVEFORM_POINTS);
        snapshot.irSamples[i] = irWindow[src];
        snapshot.redSamples[i] = redWindow[src];
    }
    /* Zero the unused tail so consumers never read stale data. */
    for (uint16_t i = available; i < WAVEFORM_POINTS; ++i)
    {
        snapshot.irSamples[i] = 0;
        snapshot.redSamples[i] = 0;
    }

    /* Simple local-maximum peak detection on the IR channel. */
    snapshot.peakCount = 0U;
    if (!shapeForScenario().flat && available >= 3U)
    {
        const int16_t threshold = static_cast<int16_t>(
            static_cast<float>(WAVEFORM_FULL_SCALE) *
            (shapeForScenario().baseline + (shapeForScenario().amplitude * 0.5F)));
        for (uint16_t i = 1U; (i + 1U) < available; ++i)
        {
            const int16_t prev = snapshot.irSamples[i - 1U];
            const int16_t cur = snapshot.irSamples[i];
            const int16_t next = snapshot.irSamples[i + 1U];
            if ((cur > prev) && (cur >= next) && (cur > threshold))
            {
                if (snapshot.peakCount < WAVEFORM_MAX_PEAKS)
                {
                    snapshot.peakIndices[snapshot.peakCount] = i;
                    ++snapshot.peakCount;
                }
            }
        }
    }
    for (uint8_t i = snapshot.peakCount; i < WAVEFORM_MAX_PEAKS; ++i)
    {
        snapshot.peakIndices[i] = 0U;
    }
}

bool MockSignalGenerator::signalValid() const
{
    switch (activeScenario)
    {
    case MockScenario::Normal:
    case MockScenario::StorageError:
    case MockScenario::StaleResult:
        return true;
    default:
        return false;
    }
}

float MockSignalGenerator::sqiPercent() const
{
    switch (activeScenario)
    {
    case MockScenario::Normal:       return 88.0F;
    case MockScenario::NoFinger:     return 3.0F;
    case MockScenario::Stabilizing:  return 42.0F;
    case MockScenario::WeakSignal:   return 28.0F;
    case MockScenario::Motion:       return 22.0F;
    case MockScenario::Saturation:   return 18.0F;
    case MockScenario::SensorError:  return 0.0F;
    case MockScenario::StorageError: return 85.0F;
    case MockScenario::LowSqi:       return 36.0F;
    case MockScenario::StaleResult:  return 80.0F;
    default:                         return 0.0F;
    }
}

void MockSignalGenerator::fillMeasurement(GuiMeasurementSnapshot& snapshot) const
{
    const bool valid = signalValid();
    snapshot.sqiPercent = sqiPercent();
    snapshot.bpmValid = valid;
    snapshot.spo2Valid = valid;

    if (valid)
    {
        snapshot.bpm = bpmBase;
        /* SpO2 loosely anti-correlated with a fixed pleasant range. */
        snapshot.spo2Percent = clampf(99.0F - ((bpmBase - 70.0F) * 0.05F), 94.0F, 99.0F);
    }
    else
    {
        snapshot.bpm = 0.0F;
        snapshot.spo2Percent = 0.0F;
    }
}

} // namespace gui
