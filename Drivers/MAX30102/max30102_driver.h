#ifndef MAX30102_DRIVER_H
#define MAX30102_DRIVER_H

/**
 * @file    max30102_driver.h
 * @brief   Driver pulse-oximeter MAX30102 (I2C, polling — không dùng chân INT).
 *
 * Phạm vi bring-up: dò thiết bị, đọc part id, reset, áp cấu hình SpO2-mode cơ bản
 * và đọc sample RED/IR từ FIFO bằng polling. Không tính BPM / SpO2 / SQI / DSP ở
 * đây (phần đó thuộc phần DSP sau này).
 *
 * Handle I2C được truyền vào qua @ref MAX30102_Init (HAL HANDLE RULE); driver
 * không bao giờ tham chiếu một handle toàn cục. User-owned (ngoài generated).
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
    MAX30102_OK = 0,
    MAX30102_ERR_ARG,       /**< Tham số không hợp lệ.            */
    MAX30102_ERR_NOT_INIT,  /**< Chưa gọi Init / handle null.     */
    MAX30102_ERR_I2C,       /**< Truyền I2C thất bại.             */
    MAX30102_ERR_ID         /**< Part id không như mong đợi.      */
} Max30102Status;

/** @brief Giá trị kỳ vọng của thanh ghi PART_ID (0xFF). */
#define MAX30102_PART_ID        0x15U

/** @brief Một sample FIFO: RED và IR 18-bit (căn phải). */
typedef struct
{
    uint32_t red;
    uint32_t ir;
} Max30102Sample;

/**
 * @brief Gắn handle I2C và cấu hình cảm biến sang SpO2 mode.
 * @param hi2c Handle I2C mà cảm biến được nối vào (I2C3 trên board này).
 * @return MAX30102_OK nếu thành công; nếu không thì mã lỗi.
 *
 * Dò bus, xác nhận part id, soft reset, rồi áp cấu hình FIFO/SpO2/LED cơ bản.
 * Không khởi động task nào.
 */
Max30102Status MAX30102_Init(I2C_HandleTypeDef* hi2c);

/**
 * @brief Đọc thanh ghi PART_ID.
 * @param[out] partId Đích (kỳ vọng MAX30102_PART_ID).
 * @return MAX30102_OK nếu thành công.
 */
Max30102Status MAX30102_ReadPartId(uint8_t* partId);

/** @brief Phát một soft reset và chờ nó tự xóa. */
Max30102Status MAX30102_Reset(void);

/**
 * @brief Trả về số sample chưa đọc hiện có trong FIFO.
 * @param[out] count Đích, số sample (0..32).
 * @return MAX30102_OK nếu thành công.
 */
Max30102Status MAX30102_GetFifoSampleCount(uint8_t* count);

/**
 * @brief Đọc tối đa @p maxSamples sample đang chờ trong FIFO.
 * @param[out] samples  Mảng đích.
 * @param      maxSamples Sức chứa của @p samples.
 * @param[out] readCount Số sample thực sự đã đọc.
 * @return MAX30102_OK nếu thành công.
 *
 * Đọc tất cả sample đang chờ trong một burst (không chỉ đọc một khi có nhiều).
 */
Max30102Status MAX30102_ReadFifo(Max30102Sample* samples, uint8_t maxSamples, uint8_t* readCount);

/**
 * @brief Đọc và xóa bộ đếm FIFO overflow.
 * @param[out] overflow Số sample mất từ lần đọc trước (0..31).
 * @return MAX30102_OK nếu thành công.
 */
Max30102Status MAX30102_ReadOverflowCounter(uint8_t* overflow);

#ifdef __cplusplus
}
#endif

#endif /* MAX30102_DRIVER_H */
