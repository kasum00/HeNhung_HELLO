#ifndef ALERT_LED_PATTERN_H
#define ALERT_LED_PATTERN_H

/**
 * @file    alert_led_pattern.h
 * @brief   Nháy luân phiên PG13/PG14 khi có cảnh báo (non-blocking).
 *
 * Máy trạng thái theo timestamp, KHÔNG dùng HAL_Delay. Chỉ phụ thuộc
 * StatusLedDriver + hằng thời gian; nó KHÔNG biết BPM/SpO2 — caller truyền vào cờ
 * "có cảnh báo hay không" (thường là MedicalAlert_IsActive()). Gọi
 * @ref AlertLed_Process định kỳ (ví dụ trong vòng lặp sensor task 20 ms).
 * User-owned (ngoài các thư mục generated).
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Khởi tạo mẫu nháy (tắt cả hai LED). */
void AlertLed_Init(void);

/**
 * @brief Tiến mẫu nháy LED.
 * @param alertActive Có cảnh báo hợp lệ đang hoạt động hay không.
 * @param nowMs       Mốc thời gian hiện tại (HAL_GetTick).
 *
 * Khi @p alertActive: PG13/PG14 nháy luân phiên, đổi pha mỗi
 * @ref ALERT_LED_STEP_MS. Khi không: cả hai LED tắt. Ghi mức mong muốn tường
 * minh (không toggle theo mức hiện tại).
 */
void AlertLed_Process(bool alertActive, uint32_t nowMs);

#ifdef __cplusplus
}
#endif

#endif /* ALERT_LED_PATTERN_H */
