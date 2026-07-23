#ifndef PPG_TYPES_H
#define PPG_TYPES_H

/**
 * @file    ppg_types.h
 * @brief   Các kiểu PPG plain-data dùng chung giữa sensor task, engine đo và GUI
 *          bridge. Không HAL, không bộ nhớ động.
 */

#include <stdint.h>
#include <stdbool.h>
#include "ppg_config.h"
#include "measurement_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Một sample raw từ cảm biến (RED/IR 18-bit, không bao giờ bị sửa). */
typedef struct
{
    uint32_t sequence;     /**< Chỉ số sample tăng đơn điệu.   */
    uint32_t timestampMs;  /**< Thời điểm thu (HAL_GetTick).   */
    uint32_t redRaw;       /**< Giá trị RED 18-bit.            */
    uint32_t irRaw;        /**< Giá trị IR 18-bit.             */
} PpgRawSample;

/** @brief Vòng đời phép đo (bám sát luồng yêu cầu). */
typedef enum
{
    PPG_STATE_IDLE = 0,
    PPG_STATE_WAIT_FINGER,
    PPG_STATE_STABILIZING,
    PPG_STATE_MEASURING,
    PPG_STATE_INVALID_SIGNAL,
    PPG_STATE_SENSOR_ERROR,
    PPG_STATE_RESULT_READY    /**< Đã nhấc ngón tay: kết quả đã chốt, đóng băng. */
} PpgState;

/** @brief Lý do tín hiệu không dùng được. */
typedef enum
{
    PPG_REASON_NONE = 0,
    PPG_REASON_NO_FINGER,
    PPG_REASON_UNSTABLE,
    PPG_REASON_SATURATION,
    PPG_REASON_WEAK_SIGNAL,
    PPG_REASON_SENSOR_ERROR
} PpgInvalidReason;

/**
 * @brief Tín hiệu IR nào điều khiển phát hiện peak, BPM và waveform.
 *
 * Lựa chọn này là toàn cục: ảnh hưởng cả waveform hiển thị lẫn các giá trị tính
 * cho dashboard. Mở rộng được (median/band-pass sau này). SpO2 luôn dùng RAW
 * RED/IR bất kể mode này.
 */
/* Nguồn tín hiệu phân tích/hiển thị chọn được (toàn cục).
   SpO2 luôn dùng RAW RED/IR bất kể mode này. */
typedef enum
{
    PPG_FILTER_RAW = 0,           /* Tín hiệu RAW đã centered (không lọc). */
    PPG_FILTER_MOVING_AVERAGE,    /* Moving average (làm mượt). */
    PPG_FILTER_MEDIAN,            /* Median filter (loại spike). */
    PPG_FILTER_LOWPASS,           /* Low-pass Butterworth (loại nhiễu cao tần). */
    PPG_FILTER_MEDIAN_LOWPASS     /* Chuỗi: Median → Lowpass (loại spike + làm mượt). */
} PpgFilterMode;

/**
 * @brief Mọi thứ GUI bridge cần từ một snapshot phép đo.
 *
 * Waveform là IR đã centered, ánh xạ vào [0, PPG_WAVE_FULL_SCALE] với
 * PPG_WAVE_ZERO là đường zero; các trường RAW không bao giờ bị giá trị centered
 * ghi đè.
 */
typedef struct
{
    PpgState state;
    PpgInvalidReason reason;

    bool  fingerPresent;
    bool  signalStable;
    bool  waveformVisible;
    bool  bpmValid;

    float stabilizationProgress; /**< 0..100 %.                 */
    float bpm;                   /**< Instant BPM (median of recent RR).      */

    /* Thống kê theo phiên (cả lần chạm ngón tay). */
    float    averageBpm;         /**< BPM trung bình phiên từ RR hợp lệ (§13). */
    bool     averageBpmValid;    /**< True khi đủ thời lượng + số RR.          */
    float    bpmMin;             /**< BPM tức thời nhỏ nhất trong phiên.       */
    float    bpmMax;             /**< BPM tức thời lớn nhất trong phiên.       */
    uint32_t elapsedMeasurementMs;/**< Thời gian đã đo trong phiên.           */
    uint32_t validRrCount;       /**< Số RR interval hợp lệ trong phiên.       */

    /* SpO2 (RAW ratio-of-ratios) + chất lượng tín hiệu. */
    float    spo2;               /**< SpO2 %% hợp lệ mới nhất (nếu không thì 0). */
    bool     spo2Valid;          /**< True khi được phép hiển thị spo2.        */
    float    averageSpo2;        /**< Trung bình phiên của các cửa sổ SpO2 hợp lệ. */
    bool     averageSpo2Valid;   /**< True khi đủ cửa sổ + thời lượng.         */
    float    spo2Min;
    float    spo2Max;
    uint32_t validSpo2Windows;   /**< Số cửa sổ SpO2 hợp lệ trong phiên.       */
    float    sqiPercent;         /**< Chỉ số chất lượng tín hiệu 0..100.       */
    float    averageSqi;         /**< SQI trung bình phiên.                    */
    float    ratioOfRatios;      /**< R gần nhất (chẩn đoán).                  */
    float    dcRed;              /**< DC RED cửa sổ gần nhất (chẩn đoán).      */
    float    dcIr;
    float    acRed;
    float    acIr;

    /* Cờ kết quả đã chốt (khi nhấc ngón tay). */
    bool                    resultReady;   /**< Có kết quả đã chốt, đóng băng.   */
    bool                    resultSaved;   /**< Đã thêm vào lịch sử (DSP task set).*/
    MeasurementResultStatus resultStatus;  /**< VALID / PARTIAL / INVALID.        */
    MeasurementEndReason    endReason;     /**< Phiên kết thúc kiểu gì.           */

    uint32_t redRaw;             /**< RAW RED mới nhất.         */
    uint32_t irRaw;              /**< RAW IR mới nhất.          */
    int32_t  redCentered;        /**< RED đã centered mới nhất. */
    int32_t  irCentered;         /**< IR đã centered mới nhất.  */
    int32_t  redFiltered;        /**< RED đã moving-average (centered) mới nhất. */
    int32_t  irFiltered;         /**< IR đã moving-average (centered) mới nhất.  */

    uint32_t acceptedPeaks;
    uint32_t rejectedPeaks;
    uint32_t droppedSamples;
    uint32_t fifoOverflows;

    PpgFilterMode filterMode;    /**< Nguồn tín hiệu phân tích/hiển thị đang dùng. */
    uint8_t  maWindow;           /**< Cửa sổ moving-average N đang dùng.       */

    /* Cửa sổ waveform IR đã centered (cũ nhất..mới nhất). */
    int16_t  waveform[PPG_WAVE_POINTS];
    uint16_t waveformCount;

    /* Cửa sổ waveform RED đã centered (cũ nhất..mới nhất). */
    int16_t  redWaveform[PPG_WAVE_POINTS];
    uint16_t redWaveformCount;
    uint16_t peakIndices[PPG_MAX_PEAKS]; /**< Chỉ số trong waveform[] của các peak đã chấp nhận. */
    uint8_t  peakCount;
} PpgResult;

#ifdef __cplusplus
}
#endif

#endif /* PPG_TYPES_H */
