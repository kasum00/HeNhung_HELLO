#ifndef APP_INIT_H
#define APP_INIT_H

/**
 * @file    app_init.h
 * @brief   Khởi tạo ứng dụng + sensor/RTC task (target).
 *
 * main.c chỉ gọi @ref App_Init (USER CODE) trước scheduler; default thread chạy
 * @ref App_DefaultTask, hàm này poll FIFO MAX30102 vào sample queue lock-free,
 * đọc/ghi DS1307 qua RTC service (phía sau I2C mutex) và phục vụ buzzer. Bản thân
 * engine đo chạy trong DSP task (do DspTask rút queue ra).
 */

#include "stm32f4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Giao tiếp cảm biến khỏe (1) hay lỗi (0). Bridge đọc. */
extern volatile int g_sensorOk;
/** @brief Part id MAX30102 đọc lúc init (kỳ vọng 0x15). */
extern volatile uint8_t g_max30102PartId;
/** @brief Tổng số lần FIFO overflow. Bridge đọc. */
extern volatile uint32_t g_fifoOverflowTotal;

/**
 * @brief Khởi tạo driver, RTC service và buzzer (trước scheduler).
 */
void App_Init(void);

/** @brief Vòng lặp task sensor + RTC + buzzer (không bao giờ trả về). */
void App_DefaultTask(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_INIT_H */
