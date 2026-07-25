#ifndef MEASUREMENT_TYPES_H
#define MEASUREMENT_TYPES_H

/**
 * @file    measurement_types.h
 * @brief   Kiểu dữ liệu kết quả đo đã chốt + bản ghi lịch sử.
 *
 * Các kiểu plain-data dùng chung giữa engine đo/DSP task và history service.
 * Độc lập với TouchGFX. Một bản ghi lịch sử là bản TÓM TẮT gọn của một phiên
 * (không có waveform raw) để chứa được nhiều bản trong buffer RAM cố định.
 *
 * @note  User-owned. Thuần C, không HAL / bộ nhớ động.
 */

#include <stdint.h>
#include <stdbool.h>
#include "datetime.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Phân loại chất lượng tổng thể của một phiên đã chốt. */
typedef enum
{
    MEASUREMENT_RESULT_VALID = 0,  /**< Cả BPM và SpO2 đều hợp lệ.          */
    MEASUREMENT_RESULT_PARTIAL,    /**< Một chỉ số hợp lệ (ví dụ chỉ BPM).  */
    MEASUREMENT_RESULT_INVALID     /**< Không dùng được (quá ngắn / không có nhịp). */
} MeasurementResultStatus;

/** @brief Lý do một phiên kết thúc. */
typedef enum
{
    MEASUREMENT_END_FINGER_REMOVED = 0,
    MEASUREMENT_END_USER_STOPPED,
    MEASUREMENT_END_TIMEOUT,
    MEASUREMENT_END_SENSOR_ERROR,
    MEASUREMENT_END_SIGNAL_LOST
} MeasurementEndReason;

/** @brief Một phiên đo đã tóm tắt, lưu trong lịch sử. */
typedef struct
{
    uint32_t recordId;

    DateTime startDateTime;
    DateTime endDateTime;

    uint32_t durationMs;

    float averageBpm;
    float minimumBpm;
    float maximumBpm;

    float averageSpo2;
    float minimumSpo2;
    float maximumSpo2;

    float averageSqi;

    uint32_t acceptedPeakCount;
    uint32_t rejectedPeakCount;
    uint32_t droppedSampleCount;
    uint32_t fifoOverflowCount;

    bool bpmValid;
    bool spo2Valid;

    MeasurementResultStatus status;
    MeasurementEndReason endReason;
} MeasurementHistoryRecord;

#ifdef __cplusplus
}
#endif

#endif /* MEASUREMENT_TYPES_H */
