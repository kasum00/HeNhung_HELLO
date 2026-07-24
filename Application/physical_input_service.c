/**
 * @file    physical_input_service.c
 * @brief   Cài đặt dịch vụ nút B1 + callback/handler EXTI0 (strong-symbol).
 * @note    User-owned (ngoài các thư mục generated).
 */

#include "physical_input_service.h"
#include "stm32f4xx_hal.h"
#include "hw_config.h"

/* -------------------------------------------------------------------------- */
/* Trạng thái                                                                  */
/* -------------------------------------------------------------------------- */
/* Bộ đếm SPSC: ISR chỉ ghi s_pressCount; consumer chỉ ghi s_consumedCount. */
static volatile uint32_t s_pressCount     = 0U;   /**< Producer (ISR).        */
static uint32_t          s_consumedCount  = 0U;   /**< Consumer (luồng GUI).  */
static volatile uint32_t s_lastAcceptedMs = 0U;   /**< Mốc dội gần nhất (ISR).*/

void PhysicalInput_Init(void)
{
    s_pressCount = 0U;
    s_consumedCount = 0U;
    s_lastAcceptedMs = 0U;

    /* Bật ngắt đường EXTI0 cho B1 (chân + sườn đã do CubeMX cấu hình). */
    HAL_NVIC_SetPriority(HW_B1_EXTI_IRQN, HW_B1_EXTI_PRIORITY, 0U);
    HAL_NVIC_EnableIRQ(HW_B1_EXTI_IRQN);
}

void PhysicalInput_OnB1Interrupt(uint32_t timestampMs)
{
    /* Trừ không dấu -> an toàn khi HAL_GetTick tràn. */
    if ((uint32_t)(timestampMs - s_lastAcceptedMs) >= HW_B1_DEBOUNCE_MS)
    {
        s_lastAcceptedMs = timestampMs;
        ++s_pressCount;            /* công bố một lần nhấn hợp lệ */
    }
}

bool PhysicalInput_GetEvent(PhysicalInputEvent* event)
{
    if (event == NULL)
    {
        return false;
    }
    const uint32_t count = s_pressCount;   /* đọc nguyên tử 32-bit */
    if (count != s_consumedCount)
    {
        ++s_consumedCount;                 /* tiêu thụ đúng một lần nhấn */
        *event = PHYSICAL_INPUT_EVENT_B1_PRESSED;
        return true;
    }
    *event = PHYSICAL_INPUT_EVENT_NONE;
    return false;
}

/* -------------------------------------------------------------------------- */
/* ISR EXTI (strong-symbol override, an toàn khi regenerate)                    */
/* -------------------------------------------------------------------------- */
/* startup chỉ định nghĩa EXTI0_IRQHandler yếu (weak) trỏ Default_Handler, và
   HAL_GPIO_EXTI_Callback là weak trong HAL. Định nghĩa mạnh ở đây, trong file
   user-owned, nên CubeMX regenerate không ghi đè (không cần sửa
   stm32f4xx_it.c). */

void EXTI0_IRQHandler(void)
{
    HAL_GPIO_EXTI_IRQHandler(HW_B1_PIN);   /* xóa cờ pending + gọi callback */
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == HW_B1_PIN)
    {
        PhysicalInput_OnB1Interrupt(HAL_GetTick());
    }
}
