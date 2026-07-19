#ifndef HW_CONFIG_H
#define HW_CONFIG_H

/**
 * @file    hw_config.h
 * @brief   Ánh xạ phần cứng tập trung (chân, ngoại vi, địa chỉ, timing).
 *
 * Theo CONFIG RULE của dự án, không driver nào hard-code chân, địa chỉ hay tốc
 * độ; mọi thứ cấu hình được nằm ở đây. Header này là cấu hình hướng board, không
 * phải driver. Nó là user-owned (ngoài các thư mục generated).
 *
 * Board: STM32F429I-DISCO REV D01 (STM32F429ZIT6).
 * Chỉ I2C3 còn trống trên board này (chân I2C1/I2C2 bị LTDC/FMC dùng hết), nên
 * MAX30102 và DS1307 DÙNG CHUNG I2C3 ở 100 kHz phía sau một mutex.
 */

#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Bus I2C (dùng chung)                                                        */
/* -------------------------------------------------------------------------- */
/** Tên handle bus cảm biến dùng chung (định nghĩa trong main.c là hi2c3).     */
#define HW_SENSOR_I2C            hi2c3
/** Chân I2C3 (cấu hình trong CubeMX): SCL=PA8, SDA=PC9, 100 kHz.              */

/** Địa chỉ 7-bit MAX30102 0x57, đã dịch cho API HAL 8-bit của STM32.          */
#define MAX30102_I2C_ADDRESS     (0x57U << 1U)
/** Địa chỉ 7-bit DS1307 0x68, đã dịch cho API HAL 8-bit của STM32.            */
#define DS1307_I2C_ADDRESS       (0x68U << 1U)

/** Timeout mặc định cho giao dịch HAL I2C (ms).                               */
#define HW_I2C_TIMEOUT_MS        100U

/* -------------------------------------------------------------------------- */
/* Buzzer (passive) - TIM10_CH1 PWM trên PF6 (AF3)                             */
/* -------------------------------------------------------------------------- */
/** Instance timer dùng cho PWM buzzer.                                        */
#define HW_BUZZER_TIM            TIM10
/** Kênh timer.                                                                */
#define HW_BUZZER_TIM_CHANNEL    TIM_CHANNEL_1
/** Port/chân ngõ ra PWM và alternate function.                               */
#define HW_BUZZER_GPIO_PORT      GPIOF
#define HW_BUZZER_GPIO_PIN       GPIO_PIN_6
#define HW_BUZZER_GPIO_AF        GPIO_AF3_TIM10
/**
 * Tần số đếm của timer. TIM10 nằm trên APB2 (clock timer 180 MHz), nên prescaler
 * (180 - 1) cho tick 1 MHz, làm phép tính note đơn giản:
 *   ARR = HW_BUZZER_TIMER_HZ / noteFrequencyHz - 1
 */
#define HW_BUZZER_TIMER_HZ       1000000UL
#define HW_BUZZER_TIM_PRESCALER  (179U)   /* 180 MHz / (179+1) = 1 MHz */

/* -------------------------------------------------------------------------- */
/* Lấy mẫu MAX30102                                                            */
/* -------------------------------------------------------------------------- */
/** Chu kỳ poll cảm biến trong sensor task (ms). Ngắn hơn thời điểm FIFO đầy.  */
#define MAX30102_POLL_PERIOD_MS  20U

/* -------------------------------------------------------------------------- */
/* Xử lý lỗi bus dùng chung                                                    */
/* -------------------------------------------------------------------------- */
/**
 * Số lần đọc cảm biến lỗi liên tiếp được dung thứ trước khi tuyên bố cảm biến
 * lỗi. Một trục trặc lẻ (ví dụ bus I2C dùng chung đang ổn định lại sau một giao
 * dịch DS1307) KHÔNG được làm trắng phép đo, nên một lần lỗi đơn lẻ bị bỏ qua. Ở
 * MAX30102_POLL_PERIOD_MS đây là cửa sổ ~100 ms.
 */
#define HW_SENSOR_FAULT_STREAK   5U
/** Số lần đọc lỗi liên tiếp trước khi thử phục hồi bus I2C (re-init).          */
#define HW_I2C_RECOVER_STREAK    10U

/* -------------------------------------------------------------------------- */
/* Nút nhấn vật lý B1 - PA0 (EXTI0, active-high, sườn lên)                     */
/* -------------------------------------------------------------------------- */
/* Chân được CubeMX cấu hình GPIO_MODE_IT_RISING + pull-down (xem .ioc / main.h
   B1_BUTTON_Pin). Các macro dưới đây giữ ánh xạ ở tầng Config để driver không
   phụ thuộc main.h generated; giá trị phải khớp .ioc. NVIC EXTI0 được bật trong
   PhysicalInput_Init vì CubeMX headless không giữ ô "enable" NVIC cho EXTI. */
#define HW_B1_PORT               GPIOA
#define HW_B1_PIN                GPIO_PIN_0
#define HW_B1_EXTI_IRQN          EXTI0_IRQn
/** Số ưu tiên NVIC cho EXTI0 (>= 5 để an toàn gọi API FreeRTOS ...FromISR).    */
#define HW_B1_EXTI_PRIORITY      5U
/** Thời gian chống dội B1 (ms); dội trong cửa sổ này bị bỏ qua.               */
#define HW_B1_DEBOUNCE_MS        200U

/* -------------------------------------------------------------------------- */
/* LED trạng thái / cảnh báo - PG13 (LD3) và PG14 (LD4), active-high           */
/* -------------------------------------------------------------------------- */
/* GPIO output do CubeMX cấu hình (ALERT_LED_1/2 trong .ioc / main.h), boot OFF.
   Trên board này LED active-high: SET = sáng, RESET = tắt. Mức OFF/ON tập
   trung ở đây, không hard-code SET/RESET rải rác trong driver. */
#define HW_STATUS_LED_1_PORT     GPIOG
#define HW_STATUS_LED_1_PIN      GPIO_PIN_13
#define HW_STATUS_LED_2_PORT     GPIOG
#define HW_STATUS_LED_2_PIN      GPIO_PIN_14
#define HW_STATUS_LED_ON_STATE   GPIO_PIN_SET
#define HW_STATUS_LED_OFF_STATE  GPIO_PIN_RESET

/* -------------------------------------------------------------------------- */
/* UART telemetry - USART1 (PA9 TX / PA10 RX), 921600 8N1                       */
/* -------------------------------------------------------------------------- */
/** Handle UART telemetry (định nghĩa trong main.c là huart1). TelemetryTask là
    chủ sở hữu duy nhất của TX; truyền non-blocking bằng HAL_UART_Transmit_IT.  */
#define HW_TELEMETRY_UART        huart1

#endif /* HW_CONFIG_H */
