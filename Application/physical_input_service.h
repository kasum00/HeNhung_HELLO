#ifndef PHYSICAL_INPUT_SERVICE_H
#define PHYSICAL_INPUT_SERVICE_H

/**
 * @file    physical_input_service.h
 * @brief   Dịch vụ nút nhấn vật lý B1 (PA0/EXTI0) — chống dội, không chặn.
 *
 * ISR EXTI chỉ ghi nhận một lần nhấn đã lọc dội rồi thoát nhanh; nó KHÔNG gọi
 * TouchGFX, không chuyển màn hình, không gửi UART, không HAL_Delay, không gọi
 * queue có chờ. Luồng ứng dụng (GUI tick) rút sự kiện qua @ref
 * PhysicalInput_GetEvent rồi mới quyết định điều hướng.
 *
 * Producer (ISR) và consumer (một luồng) trao đổi qua bộ đếm nhấn 32-bit
 * (SPSC, đọc/ghi nguyên tử trên Cortex-M4) nên không mất và không trùng sự kiện.
 * User-owned (ngoài các thư mục generated).
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Sự kiện đầu vào vật lý mà consumer có thể nhận. */
typedef enum
{
    PHYSICAL_INPUT_EVENT_NONE = 0,
    PHYSICAL_INPUT_EVENT_B1_PRESSED
} PhysicalInputEvent;

/**
 * @brief Khởi tạo dịch vụ và bật ngắt EXTI0 cho B1.
 *
 * Chân PA0 đã được CubeMX cấu hình EXTI sườn lên + pull-down; hàm này bật NVIC
 * EXTI0 (CubeMX headless không giữ ô enable). Gọi một lần lúc khởi tạo ứng dụng.
 */
void PhysicalInput_Init(void);

/**
 * @brief Ghi nhận một ngắt B1 (gọi từ callback EXTI).
 * @param timestampMs Mốc thời gian ngắt (HAL_GetTick).
 *
 * Áp dụng chống dội @ref HW_B1_DEBOUNCE_MS: các cạnh trong cửa sổ chống dội bị
 * bỏ qua. An toàn gọi trong ngữ cảnh ISR.
 */
void PhysicalInput_OnB1Interrupt(uint32_t timestampMs);

/**
 * @brief Lấy sự kiện đầu vào đang chờ (không chặn).
 * @param[out] event Nhận @ref PHYSICAL_INPUT_EVENT_B1_PRESSED nếu có, hoặc
 *                   @ref PHYSICAL_INPUT_EVENT_NONE.
 * @return true nếu có một sự kiện được tiêu thụ trong lần gọi này.
 *
 * Gọi từ đúng một luồng consumer (luồng GUI). Mỗi lần nhấn hợp lệ trả về đúng
 * một lần.
 */
bool PhysicalInput_GetEvent(PhysicalInputEvent* event);

#ifdef __cplusplus
}
#endif

#endif /* PHYSICAL_INPUT_SERVICE_H */
