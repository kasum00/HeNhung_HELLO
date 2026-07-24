/**
 * @file    status_led_driver.c
 * @brief   Cài đặt driver LED trạng thái (PG13/PG14, chỉ điều khiển GPIO).
 * @note    User-owned (ngoài các thư mục generated).
 */

#include "status_led_driver.h"
#include "stm32f4xx_hal.h"
#include "hw_config.h"

/** Ánh xạ tĩnh id LED -> port/chân GPIO (thứ tự theo @ref StatusLedId). */
static GPIO_TypeDef* const s_ledPort[2] = { HW_STATUS_LED_1_PORT, HW_STATUS_LED_2_PORT };
static const uint16_t     s_ledPin[2]  = { HW_STATUS_LED_1_PIN,  HW_STATUS_LED_2_PIN  };

void StatusLed_Init(void)
{
    StatusLed_AllOff();
}

void StatusLed_Set(StatusLedId led, StatusLedState state)
{
    if ((led != STATUS_LED_1) && (led != STATUS_LED_2))
    {
        return;
    }
    const GPIO_PinState level =
        (state == STATUS_LED_ON) ? HW_STATUS_LED_ON_STATE : HW_STATUS_LED_OFF_STATE;
    HAL_GPIO_WritePin(s_ledPort[led], s_ledPin[led], level);
}

void StatusLed_AllOff(void)
{
    HAL_GPIO_WritePin(HW_STATUS_LED_1_PORT, HW_STATUS_LED_1_PIN, HW_STATUS_LED_OFF_STATE);
    HAL_GPIO_WritePin(HW_STATUS_LED_2_PORT, HW_STATUS_LED_2_PIN, HW_STATUS_LED_OFF_STATE);
}
