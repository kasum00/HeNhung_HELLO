/**
 * @file    MockGuiDataProvider.cpp
 * @brief   Implementation of the mock GUI data source and its state machines.
 * @note    Owner: user (non-generated).
 */

#include <gui/common/MockGuiDataProvider.hpp>

namespace gui
{
namespace
{
/* Frame-rate assumptions and rate divisors (TouchGFX ticks at ~60 Hz). */
constexpr float ASSUMED_FPS = 60.0F;
constexpr uint32_t FRAMES_PER_SECOND = 60U;

/* Measurement timing (seconds after Start) for the Normal scenario. */
constexpr uint32_t T_WAIT_FINGER = 1U;
constexpr uint32_t T_STABILIZING = 3U;
constexpr uint32_t T_MEASURING   = 5U;

/* Configuration defaults. */
constexpr uint8_t DEFAULT_MIN_SQI = 45U;
constexpr uint8_t DEFAULT_BRIGHTNESS = 80U;

GuiConfigurationSnapshot makeDefaultConfig()
{
    GuiConfigurationSnapshot c{};
    c.generation = 0U;
    c.filterMode = FilterMode::MovingAverage;
    c.minimumSqiPercent = DEFAULT_MIN_SQI;
    c.loggingEnabled = true;
    c.buzzerEnabled = true;
    c.adaptiveLedEnabled = true;
    c.brightnessPercent = DEFAULT_BRIGHTNESS;
    c.dirty = false;
    return c;
}
} // namespace

MockGuiDataProvider::MockGuiDataProvider()
    : lastFrame(0U),
      seconds(0U),
      sampleDebt(0.0F),
      generationCounter(1U),
      measuring(false),
      measureStartSecond(0U),
      measurementState(MeasurementState::Idle),
      measurementReason(MeasurementInvalidReason::None),
      resultStale(false),
      activeConfig(makeDefaultConfig()),
      draftConfig(makeDefaultConfig())
{
}

void MockGuiDataProvider::tick(uint32_t frameCounter)
{
    const uint32_t delta = (frameCounter >= lastFrame) ? (frameCounter - lastFrame) : 1U;
    lastFrame = frameCounter;

    /* Advance the synthetic signal at ~100 Hz regardless of frame rate. */
    sampleDebt += (static_cast<float>(MockSignalGenerator::SAMPLE_RATE_HZ) / ASSUMED_FPS)
                  * static_cast<float>(delta);
    if (sampleDebt >= 1.0F)
    {
        const uint16_t whole = static_cast<uint16_t>(sampleDebt);
        signal.advance(whole);
        sampleDebt -= static_cast<float>(whole);
    }

    /* 1 Hz gate: clock + second-based state machines. */
    if ((frameCounter % FRAMES_PER_SECOND) == 0U)
    {
        ++seconds;
        clock.advanceOneSecond();
        resolveMeasurementState();
    }
}

void MockGuiDataProvider::selectScenario(MockScenario scenario)
{
    signal.setScenario(scenario);

    /* History edge-states are tied to the storage-error scenario for testing. */
    history.setStorageUnavailable(scenario == MockScenario::StorageError);
    history.setEmpty(false);

    resolveMeasurementState();
}

void MockGuiDataProvider::resolveMeasurementState()
{
    if (!measuring)
    {
        measurementState = MeasurementState::Idle;
        measurementReason = MeasurementInvalidReason::None;
        resultStale = false;
        return;
    }

    const uint32_t elapsed = seconds - measureStartSecond;
    resultStale = false;
    measurementReason = MeasurementInvalidReason::None;

    switch (signal.scenario())
    {
    case MockScenario::SensorError:
        measurementState = MeasurementState::SensorError;
        measurementReason = MeasurementInvalidReason::SensorUnavailable;
        break;
    case MockScenario::StorageError:
        measurementState = MeasurementState::StorageError;
        break;
    case MockScenario::NoFinger:
        measurementState = MeasurementState::WaitFinger;
        measurementReason = MeasurementInvalidReason::NoFinger;
        break;
    case MockScenario::Stabilizing:
        measurementState = MeasurementState::Stabilizing;
        break;
    case MockScenario::WeakSignal:
        measurementState = MeasurementState::InvalidSignal;
        measurementReason = MeasurementInvalidReason::WeakSignal;
        break;
    case MockScenario::Motion:
        measurementState = MeasurementState::InvalidSignal;
        measurementReason = MeasurementInvalidReason::MotionDetected;
        break;
    case MockScenario::Saturation:
        measurementState = MeasurementState::InvalidSignal;
        measurementReason = MeasurementInvalidReason::Saturation;
        break;
    case MockScenario::LowSqi:
        measurementState = MeasurementState::InvalidSignal;
        measurementReason = MeasurementInvalidReason::LowSqi;
        break;
    case MockScenario::StaleResult:
        measurementState = MeasurementState::ResultReady;
        measurementReason = MeasurementInvalidReason::ResultExpired;
        resultStale = true;
        break;
    case MockScenario::Normal:
    default:
        if (elapsed < T_WAIT_FINGER)
        {
            measurementState = MeasurementState::WaitFinger;
        }
        else if (elapsed < T_STABILIZING)
        {
            measurementState = MeasurementState::Stabilizing;
        }
        else if (elapsed < T_MEASURING)
        {
            measurementState = MeasurementState::Measuring;
        }
        else
        {
            measurementState = MeasurementState::ResultReady;
        }
        break;
    }
}

bool MockGuiDataProvider::draftDiffersFromActive() const
{
    return (draftConfig.filterMode != activeConfig.filterMode) ||
           (draftConfig.minimumSqiPercent != activeConfig.minimumSqiPercent) ||
           (draftConfig.loggingEnabled != activeConfig.loggingEnabled) ||
           (draftConfig.buzzerEnabled != activeConfig.buzzerEnabled) ||
           (draftConfig.adaptiveLedEnabled != activeConfig.adaptiveLedEnabled) ||
           (draftConfig.brightnessPercent != activeConfig.brightnessPercent);
}

void MockGuiDataProvider::postCommand(const GuiCommand& command)
{
    switch (command.type)
    {
    case GuiCommandType::StartMeasurement:
        measuring = true;
        measureStartSecond = seconds;
        resolveMeasurementState();
        break;
    case GuiCommandType::StopMeasurement:
        measuring = false;
        resolveMeasurementState();
        break;

    case GuiCommandType::SelectScenario:
        selectScenario(command.scenario);
        break;

    case GuiCommandType::SelectFilter:
        draftConfig.filterMode = command.filterMode;
        measFilterMode = command.filterMode;   /* waveform toggle: immediate */
        break;
    case GuiCommandType::SetFilterWindow:
        measWindow = command.filterWindow;
        break;
    case GuiCommandType::SetMinimumSqi:
        draftConfig.minimumSqiPercent = command.minimumSqiPercent;
        break;
    case GuiCommandType::SetLoggingEnabled:
        draftConfig.loggingEnabled = command.flag;
        break;
    case GuiCommandType::SetBuzzerEnabled:
        draftConfig.buzzerEnabled = command.flag;
        break;
    case GuiCommandType::SetAdaptiveLedEnabled:
        draftConfig.adaptiveLedEnabled = command.flag;
        break;
    case GuiCommandType::SetBrightness:
        draftConfig.brightnessPercent = command.brightnessPercent;
        break;
    case GuiCommandType::ApplySettings:
        activeConfig = draftConfig;
        activeConfig.dirty = false;
        draftConfig.dirty = false;
        break;
    case GuiCommandType::CancelSettings:
        draftConfig = activeConfig;
        break;
    case GuiCommandType::RestoreDefaults:
        draftConfig = makeDefaultConfig();
        break;

    case GuiCommandType::SetDateTime:
        clock.set(command.hour, command.minute, command.second,
                  command.day, command.month, command.year);
        break;

    case GuiCommandType::None:
    default:
        break;
    }
}

bool MockGuiDataProvider::getMeasurementSnapshot(GuiMeasurementSnapshot& snapshot)
{
    snapshot.generation = generationCounter++;
    signal.fillMeasurement(snapshot);

    snapshot.state = measurementState;
    snapshot.invalidReason = measurementReason;
    snapshot.stale = resultStale;
    snapshot.time = clock.now();

    const MockScenario sc = signal.scenario();
    snapshot.sensorStatus = (sc == MockScenario::SensorError) ? SensorStatus::Error
                                                              : SensorStatus::Ok;
    snapshot.storageStatus = (sc == MockScenario::StorageError) ? StorageStatus::Error
                                                                : StorageStatus::Ready;

    /* Synthetic values for the real-measurement fields so the GUI can be
       exercised in the simulator (the application bridge fills them for real). */
    const bool measuringLike = (measurementState == MeasurementState::Measuring) ||
                               (measurementState == MeasurementState::ResultReady);
    snapshot.fingerPresent = (measurementState != MeasurementState::Idle) &&
                             (measurementState != MeasurementState::WaitFinger) &&
                             (sc != MockScenario::SensorError);
    snapshot.signalStable = measuringLike;
    snapshot.waveformVisible = measuringLike;
    snapshot.stabilizationProgress =
        (measurementState == MeasurementState::Stabilizing) ? 55.0F
        : (measuringLike ? 100.0F : 0.0F);
    snapshot.irRaw = snapshot.fingerPresent ? 82000U : 6000U;
    snapshot.redRaw = snapshot.fingerPresent ? 61000U : 4000U;
    snapshot.irCentered = 0;
    snapshot.redCentered = 0;
    snapshot.acceptedPeakCount = signal.signalValid() ? 12U : 2U;
    snapshot.rejectedPeakCount = signal.signalValid() ? 1U : 8U;
    snapshot.droppedSampleCount = (sc == MockScenario::Motion) ? (seconds * 2U) : 0U;
    snapshot.fifoOverflowCount = (sc == MockScenario::Motion) ? 3U : 0U;
    snapshot.rtcValid = true;
    snapshot.filterMode = measFilterMode;
    snapshot.maWindow = measWindow;

    /* Session averages (synthetic): track the instant values while measuring. */
    const uint32_t measuredMs = measuringLike ? ((seconds - measureStartSecond) * 1000U) : 0U;
    snapshot.elapsedMeasurementMs = measuredMs;
    snapshot.averageBpm = snapshot.bpm;
    snapshot.averageBpmValid = snapshot.bpmValid && (measuredMs >= 10000U);
    snapshot.averageSpo2 = snapshot.spo2Percent;
    snapshot.averageSpo2Valid = snapshot.spo2Valid && (measuredMs >= 10000U);
    snapshot.validPeakCount = snapshot.acceptedPeakCount;
    snapshot.validSpo2WindowCount = measuringLike ? (measuredMs / 1000U) : 0U;
    snapshot.bpmMin = measuringLike ? snapshot.bpm : 0.0F;
    snapshot.bpmMax = measuringLike ? snapshot.bpm : 0.0F;
    snapshot.spo2Min = measuringLike ? snapshot.spo2Percent : 0.0F;
    snapshot.spo2Max = measuringLike ? snapshot.spo2Percent : 0.0F;
    snapshot.averageSqi = snapshot.sqiPercent;
    snapshot.resultReady = (measurementState == MeasurementState::ResultReady);
    snapshot.temporarilySaved = snapshot.resultReady;

    /* When not producing a fresh valid result, do not present stale numbers. */
    if (snapshot.stale)
    {
        /* Keep numbers but flag invalid so views render them as outdated. */
        snapshot.bpmValid = false;
        snapshot.spo2Valid = false;
    }
    return true;
}

bool MockGuiDataProvider::getWaveformSnapshot(GuiWaveformSnapshot& snapshot)
{
    signal.fillWaveform(snapshot, generationCounter++);
    return true;
}

bool MockGuiDataProvider::getHistoryPage(uint16_t pageIndex, GuiHistoryPageSnapshot& snapshot)
{
    return history.getPage(pageIndex, snapshot, generationCounter++);
}

bool MockGuiDataProvider::getConfigurationSnapshot(GuiConfigurationSnapshot& snapshot)
{
    snapshot = draftConfig;
    snapshot.generation = generationCounter++;
    snapshot.dirty = draftDiffersFromActive();
    return true;
}

bool MockGuiDataProvider::getSystemInfoSnapshot(GuiSystemInfoSnapshot& snapshot)
{
    snapshot.projectName = "PPG Signal Analyzer";
    snapshot.firmwareVersion = "v1.0.0";
    snapshot.buildProfile = "Prototype";
    snapshot.mcu = "STM32F429ZIT6";
    snapshot.displayResolution = "240 x 320";
    snapshot.sensorName = "MAX30102";
    snapshot.algorithmStatus = "Simulated";
    return true;
}

} // namespace gui
