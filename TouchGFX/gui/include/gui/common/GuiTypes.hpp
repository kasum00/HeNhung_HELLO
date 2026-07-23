#ifndef GUI_TYPES_HPP
#define GUI_TYPES_HPP

/**
 * @file    GuiTypes.hpp
 * @brief   Shared GUI enumerations, constants and small helpers.
 *
 * This header is the vocabulary of the presentation layer. It contains only
 * plain data definitions (enumerations, compile-time constants and pure inline
 * helpers). It intentionally has NO dependency on the TouchGFX framework, on
 * HAL, on any driver or on any DSP code, so that:
 *   - the data layer can be reasoned about and unit-tested in isolation, and
 *   - the same types are shared unchanged when the mock data source is later
 *     replaced by the real ApplicationGuiBridge.
 *
 * @note  Owner: user (non-generated). Safe to edit. Lives under gui/.
 */

#include <cstdint>

namespace gui
{
/**
 * @name Screen geometry
 * @brief Physical panel geometry. Every widget layout must respect these.
 * @{
 */
constexpr int16_t SCREEN_WIDTH  = 240; /**< Panel width in pixels.  */
constexpr int16_t SCREEN_HEIGHT = 320; /**< Panel height in pixels. */
/** @} */

/**
 * @name Waveform buffering
 * @{
 */
/** Number of plotted points held per waveform channel (one per pixel column). */
constexpr uint16_t WAVEFORM_POINTS = 240U;
/** Maximum number of peak markers reported for a waveform frame. */
constexpr uint8_t  WAVEFORM_MAX_PEAKS = 12U;
/** Canonical vertical range of a normalized waveform sample: [0 .. WAVEFORM_FS]. */
constexpr int16_t  WAVEFORM_FULL_SCALE = 1000;
/** @} */

/**
 * @name History
 * @{
 */
/** Records shown on a single history page (fits a 240x320 panel comfortably). */
constexpr uint8_t HISTORY_RECORDS_PER_PAGE = 5U;
/** Total number of mock history records available. */
constexpr uint16_t HISTORY_TOTAL_RECORDS = 23U;
/** @} */

/** @brief Measurement lifecycle reported to the dashboard. */
enum class MeasurementState : uint8_t
{
    Idle = 0,        /**< Not measuring; ready to start.            */
    WaitFinger,      /**< Started; waiting for a finger to be seen. */
    Stabilizing,     /**< Finger present; signal settling.          */
    Measuring,       /**< Actively estimating BPM/SpO2.             */
    ResultReady,     /**< A valid result is available.              */
    InvalidSignal,   /**< Signal present but not usable.            */
    SensorError,     /**< Sensor unavailable / comms failure.       */
    StorageError     /**< Storage subsystem failure.                */
};

/** @brief Technical reason a measurement is currently invalid. */
enum class MeasurementInvalidReason : uint8_t
{
    None = 0,        /**< Not invalid.                    */
    NoFinger,        /**< No finger detected.             */
    WeakSignal,      /**< Perfusion too low.              */
    MotionDetected,  /**< Motion artifact.                */
    Saturation,      /**< Optical saturation / clipping.  */
    LowSqi,          /**< Signal quality below threshold. */
    SensorUnavailable, /**< Sensor not responding.        */
    ResultExpired    /**< Last result is stale.           */
};

/** @brief Sensor availability. */
enum class SensorStatus : uint8_t
{
    Ok = 0,    /**< Sensor responsive.        */
    Warming,   /**< Sensor initializing.      */
    Error,     /**< Sensor comms failure.     */
    Absent     /**< Sensor not detected.      */
};

/** @brief Storage (MicroSD) availability. */
enum class StorageStatus : uint8_t
{
    Ready = 0, /**< Mounted and writable.     */
    Busy,      /**< Write in progress.        */
    Full,      /**< No free space.            */
    Error,     /**< Mount / write failure.    */
    Absent     /**< No card present.          */
};

/** @brief DSP filter chain selected for display/processing. */
enum class FilterMode : uint8_t
{
    Raw = 0,
    MovingAverage,
    Median,
    Lowpass,
    MedianLowpass   /**< Chuỗi: Median → Lowpass (loại spike + làm mượt). */
};


/** @brief Scenario driving the mock data source (test/preview only). */
enum class MockScenario : uint8_t
{
    Normal = 0,
    NoFinger,
    Stabilizing,
    WeakSignal,
    Motion,
    Saturation,
    SensorError,
    StorageError,
    LowSqi,
    StaleResult,
    Count            /**< Sentinel: number of scenarios. */
};

/** @brief Coarse signal-quality band derived from an SQI percentage. */
enum class SignalQuality : uint8_t
{
    Poor = 0,  /**< SQI below the "fair" threshold.  */
    Fair,      /**< Usable but noisy.                */
    Good       /**< Clean signal.                    */
};

/**
 * @name SQI band thresholds (percent)
 * @{
 */
constexpr float SQI_GOOD_THRESHOLD = 75.0F; /**< >= Good.  */
constexpr float SQI_FAIR_THRESHOLD = 45.0F; /**< >= Fair.  */
/** @} */

/**
 * @brief Maps an SQI percentage to a coarse quality band.
 * @param sqiPercent Signal quality index in [0, 100].
 * @return The matching SignalQuality band.
 */
inline SignalQuality classifySignalQuality(float sqiPercent)
{
    if (sqiPercent >= SQI_GOOD_THRESHOLD)
    {
        return SignalQuality::Good;
    }
    if (sqiPercent >= SQI_FAIR_THRESHOLD)
    {
        return SignalQuality::Fair;
    }
    return SignalQuality::Poor;
}

/**
 * @brief Returns a short English label for a measurement state.
 * @param state Measurement state.
 * @return Stable, null-terminated string literal (never null).
 */
inline const char* toText(MeasurementState state)
{
    switch (state)
    {
    case MeasurementState::Idle:          return "Idle";
    case MeasurementState::WaitFinger:    return "Place finger";
    case MeasurementState::Stabilizing:   return "Stabilizing";
    case MeasurementState::Measuring:     return "Measuring";
    case MeasurementState::ResultReady:   return "Result ready";
    case MeasurementState::InvalidSignal: return "Invalid signal";
    case MeasurementState::SensorError:   return "Sensor error";
    case MeasurementState::StorageError:  return "Storage error";
    default:                              return "Unknown";
    }
}

/**
 * @brief Returns a short technical reason string for an invalid measurement.
 * @param reason Invalid reason code.
 * @return Stable, null-terminated string literal (never null).
 */
inline const char* toText(MeasurementInvalidReason reason)
{
    switch (reason)
    {
    case MeasurementInvalidReason::None:              return "";
    case MeasurementInvalidReason::NoFinger:          return "No finger";
    case MeasurementInvalidReason::WeakSignal:        return "Weak signal";
    case MeasurementInvalidReason::MotionDetected:    return "Motion detected";
    case MeasurementInvalidReason::Saturation:        return "Saturation";
    case MeasurementInvalidReason::LowSqi:            return "Low signal quality";
    case MeasurementInvalidReason::SensorUnavailable: return "Sensor unavailable";
    case MeasurementInvalidReason::ResultExpired:     return "Result expired";
    default:                                          return "Unknown";
    }
}

/**
 * @brief Returns a short label for a sensor status.
 * @param status Sensor status.
 * @return Stable, null-terminated string literal (never null).
 */
inline const char* toText(SensorStatus status)
{
    switch (status)
    {
    case SensorStatus::Ok:      return "OK";
    case SensorStatus::Warming: return "Warming";
    case SensorStatus::Error:   return "Error";
    case SensorStatus::Absent:  return "Absent";
    default:                    return "Unknown";
    }
}

/**
 * @brief Returns a short label for a storage status.
 * @param status Storage status.
 * @return Stable, null-terminated string literal (never null).
 */
inline const char* toText(StorageStatus status)
{
    switch (status)
    {
    case StorageStatus::Ready:  return "Ready";
    case StorageStatus::Busy:   return "Busy";
    case StorageStatus::Full:   return "Full";
    case StorageStatus::Error:  return "Error";
    case StorageStatus::Absent: return "No card";
    default:                    return "Unknown";
    }
}


/**
 * @brief Returns a short label for a filter mode.
 * @param mode Filter mode.
 * @return Stable, null-terminated string literal (never null).
 */
inline const char* toText(FilterMode mode)
{
    switch (mode)
    {
    case FilterMode::Raw:            return "Raw";
    case FilterMode::MovingAverage:  return "MovAvg";
    case FilterMode::Median:         return "Median";
    case FilterMode::Lowpass:        return "LowPass";
    case FilterMode::MedianLowpass:  return "Med+LP";
    default:                         return "Unknown";
    }
}


/**
 * @brief Returns a short label for a mock scenario.
 * @param scenario Mock scenario.
 * @return Stable, null-terminated string literal (never null).
 */
inline const char* toText(MockScenario scenario)
{
    switch (scenario)
    {
    case MockScenario::Normal:       return "Normal";
    case MockScenario::NoFinger:     return "No finger";
    case MockScenario::Stabilizing:  return "Stabilizing";
    case MockScenario::WeakSignal:   return "Weak signal";
    case MockScenario::Motion:       return "Motion";
    case MockScenario::Saturation:   return "Saturation";
    case MockScenario::SensorError:  return "Sensor error";
    case MockScenario::StorageError: return "Storage error";
    case MockScenario::LowSqi:       return "Low SQI";
    case MockScenario::StaleResult:  return "Stale result";
    default:                         return "Unknown";
    }
}

/**
 * @brief Returns a short label for a signal-quality band.
 * @param quality Signal-quality band.
 * @return Stable, null-terminated string literal (never null).
 */
inline const char* toText(SignalQuality quality)
{
    switch (quality)
    {
    case SignalQuality::Poor: return "Poor";
    case SignalQuality::Fair: return "Fair";
    case SignalQuality::Good: return "Good";
    default:                  return "Unknown";
    }
}

} // namespace gui

#endif // GUI_TYPES_HPP
