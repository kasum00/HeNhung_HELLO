#ifndef MEDICAL_ALERT_SERVICE_H
#define MEDICAL_ALERT_SERVICE_H

/**
 * @file    medical_alert_service.h
 * @brief   Đánh giá ngưỡng BPM/SpO2 -> cờ cảnh báo (điều khiển LED PG13/PG14).
 *
 * KHÔNG PHẢI THIẾT BỊ Y TẾ — chỉ phục vụ học tập. Service này KHÔNG chạm GPIO,
 * TouchGFX hay UART; nó chỉ nhận kết quả đo và xuất ra tập cờ cảnh báo. Tầng LED
 * (AlertLedPattern + StatusLedDriver) đọc cờ để nháy LED; tầng telemetry đọc cờ
 * để log.
 *
 * Chống nhiễu: cảnh báo chỉ bật khi giá trị HỢP LỆ, đang ĐO trực tiếp, và vượt
 * ngưỡng LIÊN TỤC đủ @c confirmationTimeMs; chỉ tắt khi trở lại bình thường liên
 * tục đủ @c clearConfirmationTimeMs. Không cảnh báo trên BPM/SpO2 invalid, trong
 * STABILIZING, hay trên giá trị cũ sau khi nhấc ngón tay (@ref MedicalAlert_Reset).
 * User-owned (ngoài các thư mục generated).
 */

#include <stdint.h>
#include <stdbool.h>
#include "ppg_types.h"   /* PpgState */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Cờ cảnh báo (có thể tồn tại đồng thời nhiều cờ). */
typedef enum
{
    MEDICAL_ALERT_NONE     = 0U,
    MEDICAL_ALERT_BPM_LOW  = 1U << 0,
    MEDICAL_ALERT_BPM_HIGH = 1U << 1,
    MEDICAL_ALERT_SPO2_LOW = 1U << 2
} MedicalAlertFlags;

/** @brief Ngưỡng + định thời hysteresis (tập trung ở alert_config.h). */
typedef struct
{
    float    bpmLowThreshold;
    float    bpmHighThreshold;
    float    spo2LowThreshold;
    uint32_t confirmationTimeMs;       /**< Thời gian vượt ngưỡng để bật.  */
    uint32_t clearConfirmationTimeMs;  /**< Thời gian bình thường để tắt.  */
} MedicalAlertThresholds;

/** @brief Ảnh chụp phép đo đưa vào bộ đánh giá cảnh báo. */
typedef struct
{
    float    currentBpm;
    float    currentSpo2;
    bool     bpmValid;
    bool     spo2Valid;
    bool     signalValid;        /**< Tín hiệu đủ tốt để tin giá trị.   */
    PpgState measurementState;   /**< Chỉ MEASURING mới đủ điều kiện.    */
    uint32_t timestampMs;
} MedicalMeasurementUpdate;

/**
 * @brief Khởi tạo service.
 * @param thresholds Ngưỡng để dùng; NULL -> lấy mặc định từ alert_config.h.
 */
void MedicalAlert_Init(const MedicalAlertThresholds* thresholds);

/**
 * @brief Đưa một kết quả đo mới vào; cập nhật máy trạng thái hysteresis.
 * @param update Ảnh chụp phép đo (không NULL).
 */
void MedicalAlert_Update(const MedicalMeasurementUpdate* update);

/** @brief Trả về tập cờ cảnh báo đang hoạt động (OR các @ref MedicalAlertFlags). */
MedicalAlertFlags MedicalAlert_GetActiveFlags(void);

/** @brief true nếu có bất kỳ cảnh báo nào đang hoạt động. */
bool MedicalAlert_IsActive(void);

/**
 * @brief Xóa mọi cảnh báo và trạng thái tức thời (ví dụ khi nhấc ngón tay).
 *
 * Sau lệnh này caller nên gọi StatusLed_AllOff() để tắt LED ngay.
 */
void MedicalAlert_Reset(void);

#ifdef __cplusplus
}
#endif

#endif /* MEDICAL_ALERT_SERVICE_H */
