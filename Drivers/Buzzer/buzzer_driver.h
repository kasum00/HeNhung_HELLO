#ifndef BUZZER_DRIVER_H
#define BUZZER_DRIVER_H

/**
 * @file    buzzer_driver.h
 * @brief   Driver buzzer thụ động: PWM tần số thay đổi + giai điệu non-blocking.
 *
 * Điều khiển một buzzer thụ động trên TIM10_CH1 (PF6). Timer đếm ở 1 MHz nên một
 * nốt tần số f dùng ARR = 1e6/f - 1 và duty ~50% (CCR = (ARR+1)/2). Giai điệu
 * phát qua một state machine được @ref Buzzer_Process tiến (không HAL_Delay,
 * không blocking). Alternate function của chân cấu hình trong CubeMX; driver này
 * sở hữu ngoại vi timer. User-owned (ngoài generated).
 */

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Trạng thái trả về của driver. */
typedef enum
{
    BUZZER_STATUS_OK = 0,
    BUZZER_STATUS_INVALID_ARGUMENT,
    BUZZER_STATUS_NOT_INITIALIZED,
    BUZZER_STATUS_BUSY,
    BUZZER_STATUS_ERROR
} BuzzerStatus;

/** @brief Một nốt trong giai điệu. Tần số 0 nghĩa là một khoảng lặng. */
typedef struct
{
    uint16_t frequencyHz;   /**< Tần số tông (0 = lặng).             */
    uint16_t durationMs;    /**< Thời lượng tông.                    */
    uint16_t pauseAfterMs;  /**< Khoảng lặng sau tông.               */
} BuzzerNote;

/**
 * @brief Khởi tạo PWM TIM10 cho buzzer; để im lặng.
 * @return BUZZER_STATUS_OK nếu thành công.
 */
BuzzerStatus Buzzer_Init(void);

/**
 * @brief Phát một tông liên tục ở @p frequencyHz.
 * @param frequencyHz Tần số tông (điển hình 20..20000 Hz); 0 dừng ngõ ra.
 * @return BUZZER_STATUS_OK nếu thành công.
 */
BuzzerStatus Buzzer_PlayFrequency(uint16_t frequencyHz);

/** @brief Dừng mọi tông và mọi giai điệu đang phát. */
BuzzerStatus Buzzer_Stop(void);

/**
 * @brief Dừng buzzer CHỈ khi đang phát ở chế độ lặp (@ref Buzzer_PlayMelodyRepeat).
 *
 * Dùng cho lớp cảnh báo: kết thúc giai điệu alert mà không hủy một giai điệu một
 * lần (ví dụ âm báo hoàn tất phép đo) do module khác vừa khởi động.
 */
BuzzerStatus Buzzer_StopLoop(void);

/**
 * @brief Bắt đầu phát một giai điệu (non-blocking).
 * @param notes     Con trỏ tới mảng nốt (phải sống lâu hơn lúc phát).
 * @param noteCount Số nốt.
 * @return BUZZER_STATUS_OK nếu đã bắt đầu phát.
 *
 * Caller phải gọi @ref Buzzer_Process định kỳ để tiến việc phát.
 */
BuzzerStatus Buzzer_PlayMelody(const BuzzerNote* notes, size_t noteCount);

/**
 * @brief Như @ref Buzzer_PlayMelody nhưng LẶP LẠI từ đầu khi phát hết.
 * @param notes     Con trỏ tới mảng nốt (phải sống lâu hơn lúc phát).
 * @param noteCount Số nốt (0 -> không phát, trả INVALID_ARGUMENT).
 * @return BUZZER_STATUS_OK nếu đã bắt đầu phát.
 *
 * Dùng cho giai điệu cảnh báo: tự lặp cho tới khi @ref Buzzer_Stop /
 * @ref Buzzer_StopLoop. Caller phải gọi @ref Buzzer_Process định kỳ.
 */
BuzzerStatus Buzzer_PlayMelodyRepeat(const BuzzerNote* notes, size_t noteCount);

/**
 * @brief Tiến việc phát giai điệu; gọi định kỳ (ví dụ mỗi 1-5 ms).
 *
 * An toàn khi gọi lúc rảnh (không làm gì). Dùng HAL_GetTick để định thời.
 */
void Buzzer_Process(void);

/** @brief Trả về true khi một tông hoặc giai điệu đang được phát. */
bool Buzzer_IsPlaying(void);

#ifdef __cplusplus
}
#endif

#endif /* BUZZER_DRIVER_H */
