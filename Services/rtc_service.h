#ifndef RTC_SERVICE_H
#define RTC_SERVICE_H

/**
 * @file    rtc_service.h
 * @brief   RTC service: đọc DS1307 định kỳ + yêu cầu cài giờ từ GUI.
 *
 * Sensor/application task gọi các hàm *_Poll / *_ProcessPendingSet (khi đang giữ
 * I2C mutex). GUI thread gọi *_GetSnapshot / *_RequestSet / *_GetLastSetResult
 * (không I2C). Snapshot công bố được double-buffer để đọc chéo luồng an toàn.
 * Target-only.
 */

#include "stm32f4xx_hal.h"
#include "datetime.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Gắn handle I2C và dò tìm RTC. */
RtcStatus RtcService_Init(I2C_HandleTypeDef* hi2c);

/** @brief Đọc RTC và công bố snapshot (task, đang giữ I2C mutex). */
void RtcService_Poll(void);

/** @brief Xử lý yêu cầu cài giờ đang chờ (task, đang giữ I2C mutex). */
void RtcService_ProcessPendingSet(void);

/**
 * @brief GUI: đọc thời gian công bố mới nhất.
 * @param[out] dateTime Thời gian mới nhất.
 * @param[out] valid    True nếu thời gian đáng tin.
 */
void RtcService_GetSnapshot(DateTime* dateTime, bool* valid);

/**
 * @brief GUI: yêu cầu ghi một ngày/giờ mới.
 * @param dateTime Thời gian cần ghi (driver kiểm hợp lệ trước khi ghi).
 */
void RtcService_RequestSet(const DateTime* dateTime);

/**
 * @brief GUI: đọc kết quả của yêu cầu cài giờ gần nhất.
 * @param[out] status     Trạng thái kết quả của lần cài hoàn tất gần nhất.
 * @param[out] generation Tăng mỗi khi một lần cài hoàn tất (poll để phát hiện đổi).
 */
void RtcService_GetLastSetResult(RtcStatus* status, uint32_t* generation);

#ifdef __cplusplus
}
#endif

#endif /* RTC_SERVICE_H */
