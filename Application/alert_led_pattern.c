/**
 * @file    alert_led_pattern.c
 * @brief   Cài đặt mẫu nháy LED cảnh báo luân phiên (non-blocking).
 * @note    User-owned (ngoài các thư mục generated).
 */

#include "alert_led_pattern.h"
#include "status_led_driver.h"
#include "alert_config.h"

/** Trạng thái mẫu nháy. */
typedef struct
{
    bool     active;         /**< Có đang nháy hay không.               */
    bool     firstPhase;     /**< true: PG13 sáng; false: PG14 sáng.    */
    uint32_t lastToggleMs;   /**< Mốc lần đổi pha gần nhất.             */
} AlertLedPattern;

static AlertLedPattern s_pat;

/** Áp mức LED cho một pha (ghi tường minh cả hai LED). */
static void applyPhase(bool firstPhase)
{
    StatusLed_Set(STATUS_LED_1, firstPhase ? STATUS_LED_ON  : STATUS_LED_OFF);
    StatusLed_Set(STATUS_LED_2, firstPhase ? STATUS_LED_OFF : STATUS_LED_ON);
}

void AlertLed_Init(void)
{
    s_pat.active = false;
    s_pat.firstPhase = true;
    s_pat.lastToggleMs = 0U;
    StatusLed_AllOff();
}

void AlertLed_Process(bool alertActive, uint32_t nowMs)
{
    if (!alertActive)
    {
        if (s_pat.active)
        {
            s_pat.active = false;
            StatusLed_AllOff();
        }
        return;
    }

    if (!s_pat.active)
    {
        /* Bắt đầu chu kỳ nháy: pha 1 ngay lập tức. */
        s_pat.active = true;
        s_pat.firstPhase = true;
        s_pat.lastToggleMs = nowMs;
        applyPhase(s_pat.firstPhase);
        return;
    }

    if ((uint32_t)(nowMs - s_pat.lastToggleMs) >= ALERT_LED_STEP_MS)
    {
        s_pat.firstPhase = !s_pat.firstPhase;
        s_pat.lastToggleMs = nowMs;
        applyPhase(s_pat.firstPhase);
    }
}
