/**
 * @file    buzzer_driver.c
 * @brief   Cài đặt driver buzzer thụ động (PWM TIM10, giai điệu non-blocking).
 * @note    User-owned (ngoài các thư mục generated).
 */

#include "buzzer_driver.h"
#include "hw_config.h"

/* -------------------------------------------------------------------------- */
/* State                                                                       */
/* -------------------------------------------------------------------------- */
static TIM_HandleTypeDef s_htim;   /**< Handle TIM10, do driver này sở hữu. */
static bool s_initialized = false;

/** Trạng thái trình phát giai điệu. */
typedef enum
{
    PHASE_IDLE = 0,
    PHASE_TONE,     /**< Đang phát tông của nốt hiện tại.    */
    PHASE_PAUSE     /**< Khoảng lặng sau nốt hiện tại.       */
} MelodyPhase;

static const BuzzerNote* s_notes = NULL;
static size_t s_noteCount = 0U;
static size_t s_noteIndex = 0U;
static MelodyPhase s_phase = PHASE_IDLE;
static uint32_t s_phaseStartMs = 0U;
static bool s_toneActive = false;
static bool s_loop = false;      /**< Lặp lại giai điệu từ đầu khi phát hết.     */

/* -------------------------------------------------------------------------- */
/* Điều khiển PWM mức thấp                                                     */
/* -------------------------------------------------------------------------- */
static void toneOff(void)
{
    if (s_toneActive)
    {
        (void)HAL_TIM_PWM_Stop(&s_htim, HW_BUZZER_TIM_CHANNEL);
        s_toneActive = false;
    }
}

static BuzzerStatus toneOn(uint16_t frequencyHz)
{
    if (frequencyHz == 0U)
    {
        toneOff();
        return BUZZER_STATUS_OK;
    }

    const uint32_t arr = (HW_BUZZER_TIMER_HZ / (uint32_t)frequencyHz) - 1U;
    if ((arr == 0U) || (arr > 0xFFFFUL))
    {
        return BUZZER_STATUS_INVALID_ARGUMENT; /* ngoài dải timer 16-bit */
    }
    const uint32_t ccr = (arr + 1U) / 2U;  /* duty ~50% */

    __HAL_TIM_SET_AUTORELOAD(&s_htim, arr);
    __HAL_TIM_SET_COMPARE(&s_htim, HW_BUZZER_TIM_CHANNEL, ccr);
    __HAL_TIM_SET_COUNTER(&s_htim, 0U);

    if (!s_toneActive)
    {
        if (HAL_TIM_PWM_Start(&s_htim, HW_BUZZER_TIM_CHANNEL) != HAL_OK)
        {
            return BUZZER_STATUS_ERROR;
        }
        s_toneActive = true;
    }
    return BUZZER_STATUS_OK;
}

/* -------------------------------------------------------------------------- */
/* Init                                                                        */
/* -------------------------------------------------------------------------- */
BuzzerStatus Buzzer_Init(void)
{
    /* Cấu hình PF6 làm alternate function TIM10_CH1. Buzzer sở hữu chân và timer
       này hoàn toàn bằng code, nên .ioc của board không cần mục buzzer nào và an
       toàn khi regenerate. */
    __HAL_RCC_GPIOF_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = HW_BUZZER_GPIO_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = HW_BUZZER_GPIO_AF;
    HAL_GPIO_Init(HW_BUZZER_GPIO_PORT, &gpio);

    __HAL_RCC_TIM10_CLK_ENABLE();

    s_htim.Instance = HW_BUZZER_TIM;
    s_htim.Init.Prescaler = HW_BUZZER_TIM_PRESCALER;
    s_htim.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_htim.Init.Period = 999U;                 /* tạm; đặt theo từng nốt */
    s_htim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_htim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&s_htim) != HAL_OK)
    {
        return BUZZER_STATUS_ERROR;
    }

    TIM_OC_InitTypeDef oc = {0};
    oc.OCMode = TIM_OCMODE_PWM1;
    oc.Pulse = 0U;                              /* im lặng lúc boot */
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&s_htim, &oc, HW_BUZZER_TIM_CHANNEL) != HAL_OK)
    {
        return BUZZER_STATUS_ERROR;
    }

    s_initialized = true;
    s_phase = PHASE_IDLE;
    s_toneActive = false;
    return BUZZER_STATUS_OK;
}

/* -------------------------------------------------------------------------- */
/* API công khai                                                               */
/* -------------------------------------------------------------------------- */
BuzzerStatus Buzzer_PlayFrequency(uint16_t frequencyHz)
{
    if (!s_initialized)
    {
        return BUZZER_STATUS_NOT_INITIALIZED;
    }
    /* Dùng tông đơn thủ công sẽ hủy mọi giai điệu đang phát. */
    s_phase = PHASE_IDLE;
    s_notes = NULL;
    return toneOn(frequencyHz);
}

BuzzerStatus Buzzer_Stop(void)
{
    if (!s_initialized)
    {
        return BUZZER_STATUS_NOT_INITIALIZED;
    }
    s_phase = PHASE_IDLE;
    s_notes = NULL;
    s_noteCount = 0U;
    s_loop = false;
    toneOff();
    return BUZZER_STATUS_OK;
}

BuzzerStatus Buzzer_StopLoop(void)
{
    /* Chỉ dừng khi đang phát ở chế độ lặp; không đụng giai điệu một lần (ví dụ
       âm báo hoàn tất) do module khác khởi động. */
    if (s_initialized && s_loop)
    {
        return Buzzer_Stop();
    }
    return BUZZER_STATUS_OK;
}

/** Khởi động một giai điệu (một lần hoặc lặp). */
static BuzzerStatus startMelody(const BuzzerNote* notes, size_t noteCount, bool loop)
{
    if (!s_initialized)
    {
        return BUZZER_STATUS_NOT_INITIALIZED;
    }
    if ((notes == NULL) || (noteCount == 0U))
    {
        return BUZZER_STATUS_INVALID_ARGUMENT;   /* melody rỗng: giữ nguyên trạng thái */
    }

    s_notes = notes;
    s_noteCount = noteCount;
    s_noteIndex = 0U;
    s_loop = loop;
    s_phase = PHASE_TONE;
    s_phaseStartMs = HAL_GetTick();
    return toneOn(s_notes[0].frequencyHz);
}

BuzzerStatus Buzzer_PlayMelody(const BuzzerNote* notes, size_t noteCount)
{
    return startMelody(notes, noteCount, false);
}

BuzzerStatus Buzzer_PlayMelodyRepeat(const BuzzerNote* notes, size_t noteCount)
{
    return startMelody(notes, noteCount, true);
}

void Buzzer_Process(void)
{
    if ((!s_initialized) || (s_phase == PHASE_IDLE) || (s_notes == NULL))
    {
        return;
    }

    const uint32_t now = HAL_GetTick();
    const uint32_t elapsed = now - s_phaseStartMs;
    const BuzzerNote* note = &s_notes[s_noteIndex];

    if (s_phase == PHASE_TONE)
    {
        if (elapsed < (uint32_t)note->durationMs)
        {
            return;                 /* tông vẫn đang kêu */
        }
        toneOff();
        if (note->pauseAfterMs > 0U)
        {
            s_phase = PHASE_PAUSE;  /* lặng trước nốt kế tiếp */
            s_phaseStartMs = now;
            return;
        }
        /* không có khoảng lặng: tiến ngay (rơi xuống dưới) */
    }
    else /* PHASE_PAUSE */
    {
        if (elapsed < (uint32_t)note->pauseAfterMs)
        {
            return;                 /* vẫn đang lặng */
        }
    }

    /* Tiến sang nốt kế tiếp (hoặc kết thúc / lặp lại). */
    ++s_noteIndex;
    if (s_noteIndex >= s_noteCount)
    {
        if (s_loop)
        {
            s_noteIndex = 0U;       /* phát lại từ đầu (alert còn hiệu lực) */
        }
        else
        {
            (void)Buzzer_Stop();
            return;
        }
    }
    s_phase = PHASE_TONE;
    s_phaseStartMs = now;
    (void)toneOn(s_notes[s_noteIndex].frequencyHz);
}

bool Buzzer_IsPlaying(void)
{
    return s_initialized && (s_toneActive || (s_phase != PHASE_IDLE));
}
