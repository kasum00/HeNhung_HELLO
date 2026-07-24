#ifndef DS1307_DRIVER_H
#define DS1307_DRIVER_H

/**
 * @file    ds1307_driver.h
 * @brief   Driver real-time-clock DS1307 (I2C, 100 kHz, 24 giờ, không SQW).
 *
 * Tự viết từ datasheet. Đọc/ghi thời gian lịch dạng BCD, kiểm tra hợp lệ ngày/giờ
 * (gồm năm nhuận), xử lý bit clock-halt (CH) và xác nhận lần ghi bằng cách đọc
 * lại. Handle I2C được truyền vào qua @ref DS1307_Init (HAL HANDLE RULE). Thuần
 * C, không bộ nhớ động, không delay blocking, không GUI.
 */

#include "stm32f4xx_hal.h"
#include "datetime.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Gắn handle I2C và xác nhận thiết bị có phản hồi.
 * @param hi2c Handle I2C mà RTC được nối vào (I2C3 trên board này).
 * @return RTC_STATUS_OK nếu thành công; RTC_STATUS_I2C_ERROR / _INVALID_ARGUMENT.
 */
RtcStatus DS1307_Init(I2C_HandleTypeDef* hi2c);

/**
 * @brief Đọc ngày/giờ hiện tại.
 * @param[out] dateTime Đích (trường nhị phân; năm dạng 20xx).
 * @return RTC_STATUS_OK, RTC_STATUS_OSCILLATOR_STOPPED (vẫn điền thời gian),
 *         hoặc một mã lỗi.
 */
RtcStatus DS1307_ReadDateTime(DateTime* dateTime);

/**
 * @brief Kiểm hợp lệ, ghi và đọc lại ngày/giờ; khởi động oscillator.
 * @param dateTime Thời gian cần ghi.
 * @return RTC_STATUS_OK chỉ sau khi đọc lại khớp; nếu không thì một mã INVALID,
 *         I2C_ERROR hoặc READBACK_MISMATCH.
 */
RtcStatus DS1307_SetDateTime(const DateTime* dateTime);

/**
 * @brief Báo oscillator có đang chạy không (bit CH đã xóa).
 * @param[out] running True nếu đồng hồ đang chạy.
 * @return RTC_STATUS_OK nếu thành công.
 */
RtcStatus DS1307_IsOscillatorRunning(bool* running);

/**
 * @brief Xóa bit CH để khởi động oscillator (giữ nguyên thời gian hiện tại).
 * @return RTC_STATUS_OK nếu thành công.
 */
RtcStatus DS1307_StartOscillator(void);

#ifdef __cplusplus
}
#endif

#endif /* DS1307_DRIVER_H */
