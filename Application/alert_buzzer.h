#ifndef ALERT_BUZZER_H
#define ALERT_BUZZER_H

/**
 * @file    alert_buzzer.h
 * @brief   Phát còi cảnh báo lặp lại, đồng bộ vòng đời với đèn PG13/PG14.
 *
 * Đây là "buzzer player" của lớp cảnh báo. Nó KHÔNG tự đánh giá ngưỡng: caller
 * truyền vào cùng một cờ @c alertActive dùng cho đèn (MedicalAlert_IsActive), nên
 * còi và đèn dùng CHUNG một vòng đời cảnh báo — cùng bắt đầu, cùng dừng.
 *
 * Cạnh lên (false->true): bắt đầu giai điệu cảnh báo ở chế độ lặp (một lần, không
 * restart mỗi sample). Cạnh xuống (true->false): dừng giai điệu lặp. Việc phát
 * từng nốt do buzzer driver tiến bằng state machine non-blocking (Buzzer_Process).
 * Giai điệu tự lặp suốt thời gian cảnh báo; melody cấu hình trong buzzer_melodies.h.
 * User-owned (ngoài các thư mục generated).
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Khởi tạo (đảm bảo còi cảnh báo ở trạng thái tắt). */
void AlertBuzzer_Init(void);

/**
 * @brief Tiến còi cảnh báo theo cờ alert.
 * @param alertActive Có cảnh báo hợp lệ đang hoạt động hay không (dùng đúng cờ
 *                    đã truyền cho AlertLed_Process để hai bên đồng bộ).
 *
 * Gọi định kỳ (cùng vòng lặp gọi AlertLed_Process + Buzzer_Process).
 */
void AlertBuzzer_Process(bool alertActive);

#ifdef __cplusplus
}
#endif

#endif /* ALERT_BUZZER_H */
