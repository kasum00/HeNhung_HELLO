/**
 * @file    alert_buzzer.c
 * @brief   Cài đặt còi cảnh báo lặp lại, đồng bộ với đèn cảnh báo.
 * @note    User-owned (ngoài các thư mục generated).
 */

#include "alert_buzzer.h"
#include "buzzer_driver.h"
#include "buzzer_melodies.h"

/* Trạng thái cạnh: chỉ khởi động/tắt còi ở chuyển tiếp, không mỗi sample (§9.2). */
static bool s_prevActive = false;

void AlertBuzzer_Init(void)
{
    s_prevActive = false;
    (void)Buzzer_StopLoop();   /* an toàn: không để lại giai điệu lặp nào */
}

void AlertBuzzer_Process(bool alertActive)
{
    if (alertActive && !s_prevActive)
    {
        /* Cạnh lên: bắt đầu giai điệu cảnh báo ở chế độ lặp (một lần). Melody
           rỗng/không hợp lệ -> driver trả lỗi và giữ còi tắt; đèn vẫn chạy. */
        (void)Buzzer_PlayMelodyRepeat(BUZZER_MELODY_ALERT, BUZZER_MELODY_ALERT_LEN);
    }
    else if (!alertActive && s_prevActive)
    {
        /* Cạnh xuống: dừng đúng giai điệu cảnh báo (không đụng âm báo một lần). */
        (void)Buzzer_StopLoop();
    }
    /* Khi vẫn đang cảnh báo: không làm gì — Buzzer_Process tự tiến và tự lặp. */

    s_prevActive = alertActive;
}
