#ifndef GUI_SNAPSHOTS_HPP
#define GUI_SNAPSHOTS_HPP

/**
 * @file    GuiSnapshots.hpp
 * @brief   Immutable-by-convention data snapshots handed to the GUI.
 *
 * A snapshot is a self-contained, plain-old-data value describing everything a
 * screen needs to render at one instant. The presentation layer only ever READS
 * snapshots; it never mutates them and never keeps a pointer into the data
 * source. Every snapshot carries a @c generation counter so a view can detect a
 * fresh value versus a repeated one, and (where relevant) a @c stale flag so
 * outdated results are never displayed as if current.
 *
 * All buffers are fixed-size: there is no dynamic allocation anywhere in the GUI
 * data path. Waveform snapshots are filled into a caller-owned struct to avoid
 * copying large arrays.
 *
 * @note  Owner: user (non-generated). No TouchGFX / HAL / driver dependency.
 */

#include <cstdint>
#include <gui/common/GuiTypes.hpp>

namespace gui
{
/** @brief Wall-clock time provided by the (mock) RTC source. */
struct GuiTime
{
    uint8_t hour;    /**< 0..23 */
    uint8_t minute;  /**< 0..59 */
    uint8_t second;  /**< 0..59 */
    uint8_t day;     /**< 1..31 */
    uint8_t month;   /**< 1..12 */
    uint16_t year;   /**< e.g. 2026 */
};

/** @brief Primary measurement values for the dashboard. */
struct GuiMeasurementSnapshot
{
    uint32_t generation;              /**< Increments on every content change. */

    float bpm;                        /**< Instant heart rate; valid if bpmValid. */
    float spo2Percent;                /**< Instant SpO2 %; valid if spo2Valid.    */
    float sqiPercent;                 /**< Signal quality index 0..100.        */

    bool bpmValid;                    /**< True when bpm may be shown.         */
    bool spo2Valid;                   /**< True when spo2Percent may be shown. */
    bool stale;                       /**< True when values are outdated.      */

    /* Session averages + progress. */
    float averageBpm;                 /**< Session average BPM.                */
    bool  averageBpmValid;            /**< True when averageBpm may be shown.  */
    float averageSpo2;                /**< Session average SpO2 %.             */
    bool  averageSpo2Valid;           /**< True when averageSpo2 may be shown. */
    uint32_t elapsedMeasurementMs;    /**< Time measured this session.         */
    uint32_t validPeakCount;          /**< Accepted RR intervals this session. */
    uint32_t validSpo2WindowCount;    /**< Valid SpO2 windows this session.    */

    /* Session min/max (empty khi chưa có giá trị hợp lệ). */
    float bpmMin;                     /**< Session min BPM.                    */
    float bpmMax;                     /**< Session max BPM.                    */
    float spo2Min;                    /**< Session min SpO2 %.                 */
    float spo2Max;                    /**< Session max SpO2 %.                 */
    float averageSqi;                 /**< Session average SQI %.              */

    bool resultReady;                 /**< Finalized result available.         */
    bool temporarilySaved;            /**< Added to the RAM history store.      */

    MeasurementState state;           /**< Measurement lifecycle state.        */
    MeasurementInvalidReason invalidReason; /**< Reason when signal invalid.   */

    SensorStatus sensorStatus;        /**< Sensor availability.                */
    StorageStatus storageStatus;      /**< Storage availability.               */

    /* Real-measurement fields (mock fills them synthetically). */
    bool fingerPresent;               /**< Finger detected on the sensor.      */
    bool signalStable;                /**< Signal ready / measuring.           */
    bool waveformVisible;             /**< Waveform may be shown.              */
    float stabilizationProgress;      /**< 0..100 % during stabilization.      */

    uint32_t redRaw;                  /**< Latest RAW RED count.               */
    uint32_t irRaw;                   /**< Latest RAW IR count.                */
    int32_t redCentered;              /**< Latest DC-centered RED.             */
    int32_t irCentered;               /**< Latest DC-centered IR.              */

    uint32_t acceptedPeakCount;
    uint32_t rejectedPeakCount;
    uint32_t droppedSampleCount;
    uint32_t fifoOverflowCount;

    bool rtcValid;                    /**< RTC time is valid/available.        */
    GuiTime time;                     /**< Timestamp of this snapshot.         */

    FilterMode filterMode;            /**< Active analysis/display signal.     */
    uint8_t maWindow;                 /**< Active moving-average window N.      */
};

/** @brief A window of waveform samples plus detected peaks. */
struct GuiWaveformSnapshot
{
    uint32_t generation;              /**< Increments each new frame.          */

    uint16_t count;                   /**< Valid samples in each channel array.*/
    int16_t irSamples[WAVEFORM_POINTS];  /**< IR channel, 0..WAVEFORM_FULL_SCALE. */
    int16_t redSamples[WAVEFORM_POINTS]; /**< RED channel, same normalization.    */

    uint8_t peakCount;                /**< Valid entries in peakIndices.       */
    uint16_t peakIndices[WAVEFORM_MAX_PEAKS]; /**< Sample indices of peaks.    */

    uint16_t sampleRateHz;            /**< Nominal acquisition rate.           */
    uint32_t droppedSamples;          /**< Cumulative dropped-sample count.    */
    bool irChannelValid;              /**< IR data is meaningful.              */
    bool redChannelValid;             /**< RED data is meaningful.             */
    SensorStatus sensorStatus;        /**< Sensor availability.                */
};

/** @brief One row of measurement history. */
struct GuiHistoryRecord
{
    GuiTime time;                     /**< When the measurement was taken.     */
    float bpm;                        /**< Recorded BPM.                       */
    float spo2Percent;                /**< Recorded SpO2 %.                    */
    float sqiPercent;                 /**< Recorded SQI %.                     */
    bool valid;                       /**< Whether the record is valid.        */
    MeasurementInvalidReason invalidReason; /**< Reason when invalid.          */
};

/** @brief Loading state of a history page request. */
enum class HistoryPageStatus : uint8_t
{
    Ok = 0,           /**< Records available in this page.          */
    Empty,            /**< No records exist at all.                 */
    Loading,          /**< Page is being fetched.                   */
    StorageUnavailable /**< Storage cannot be read.                 */
};

/** @brief A page of history records. */
struct GuiHistoryPageSnapshot
{
    uint32_t generation;
    HistoryPageStatus status;         /**< Page availability.                  */

    uint16_t pageIndex;               /**< 0-based page number.                */
    uint16_t pageCount;               /**< Total number of pages.              */
    uint16_t totalRecords;            /**< Total records across all pages.     */

    uint8_t recordCount;              /**< Valid entries in records[].         */
    GuiHistoryRecord records[HISTORY_RECORDS_PER_PAGE]; /**< Page contents.    */
};

/** @brief Active + draft configuration for the settings screen. */
struct GuiConfigurationSnapshot
{
    uint32_t generation;

    FilterMode filterMode;
    uint8_t minimumSqiPercent;        /**< 0..100 acceptance threshold.        */
    bool loggingEnabled;
    bool buzzerEnabled;
    bool adaptiveLedEnabled;
    uint8_t brightnessPercent;        /**< 0..100 backlight.                   */
    bool dirty;                       /**< Draft differs from active config.   */
};

/** @brief Static project / hardware information for the about screen. */
struct GuiSystemInfoSnapshot
{
    const char* projectName;
    const char* firmwareVersion;
    const char* buildProfile;
    const char* mcu;
    const char* displayResolution;
    const char* sensorName;
    const char* algorithmStatus;
};

} // namespace gui

#endif // GUI_SNAPSHOTS_HPP
