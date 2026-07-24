#ifndef STATUS_LED_DRIVER_H
#define STATUS_LED_DRIVER_H

/**
 * @file    status_led_driver.h
 * @brief   Driver LED trạng thái/cảnh báo trên PG13 (LD3) và PG14 (LD4).
 *
 * Driver này CHỈ điều khiển hai chân GPIO. Nó không biết gì về BPM, SpO2, màn
 * hình TouchGFX, ngưỡng y tế, trạng thái đo hay UART — mọi logic đó nằm ở tầng
 * trên (MedicalAlertService + AlertLedPattern). Chân GPIO đã được CubeMX cấu
 * hình (output push-pull, boot OFF); driver chỉ ghi mức. Mức active lấy từ
 * hw_config.h. User-owned (ngoài các thư mục generated).
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Định danh LED. */
typedef enum
{
    STATUS_LED_1 = 0,   /**< PG13 (LD3). */
    STATUS_LED_2 = 1    /**< PG14 (LD4). */
} StatusLedId;

/** @brief Trạng thái mong muốn của một LED (độc lập với active-high/low). */
typedef enum
{
    STATUS_LED_OFF = 0,
    STATUS_LED_ON  = 1
} StatusLedState;

/**
 * @brief Đưa cả hai LED về trạng thái tắt đã biết.
 *
 * GPIO đã do CubeMX khởi tạo; hàm này chỉ đảm bảo mức OFF lúc bắt đầu.
 */
void StatusLed_Init(void);

/**
 * @brief Đặt một LED ON hoặc OFF.
 * @param led   Định danh LED (@ref STATUS_LED_1 hoặc @ref STATUS_LED_2).
 * @param state Trạng thái mong muốn (@ref STATUS_LED_ON / @ref STATUS_LED_OFF).
 *
 * Ghi mức mong muốn một cách tường minh (không toggle theo mức hiện tại) để
 * tránh nhầm với LED active-low.
 */
void StatusLed_Set(StatusLedId led, StatusLedState state);

/** @brief Tắt cả hai LED. */
void StatusLed_AllOff(void);

#ifdef __cplusplus
}
#endif

#endif /* STATUS_LED_DRIVER_H */
