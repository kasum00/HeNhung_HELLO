/**
 * @file    medical_alert_service.c
 * @brief   Cài đặt đánh giá ngưỡng BPM/SpO2 với hysteresis theo thời gian.
 * @note    User-owned (ngoài các thư mục generated).
 */

#include "medical_alert_service.h"
#include "alert_config.h"
#include <stddef.h>   /* NULL */

/* -------------------------------------------------------------------------- */
/* Trạng thái một điều kiện cảnh báo (hysteresis theo thời gian)                */
/* -------------------------------------------------------------------------- */
typedef struct
{
    bool     rawExceeded;   /**< Điều kiện thô ở lần cập nhật gần nhất.        */
    bool     active;        /**< Cảnh báo đã được xác nhận đang hoạt động.     */
    uint32_t sinceMs;       /**< Mốc rawExceeded đổi giá trị lần cuối.         */
} AlertCondition;

static MedicalAlertThresholds s_cfg;
static AlertCondition s_bpmLow;
static AlertCondition s_bpmHigh;
static AlertCondition s_spo2Low;

/** Chỉ trạng thái ĐO trực tiếp mới đủ điều kiện kích hoạt cảnh báo. */
static bool eligibleState(PpgState s)
{
    return (s == PPG_STATE_MEASURING);
}

static void conditionReset(AlertCondition* c)
{
    c->rawExceeded = false;
    c->active = false;
    c->sinceMs = 0U;
}

/**
 * @brief Tiến một điều kiện: raw -> active theo confirmation/clear time.
 * @param c        Trạng thái điều kiện.
 * @param rawNow   Điều kiện thô có đúng ở thời điểm này không.
 * @param nowMs    Mốc thời gian hiện tại.
 */
static void conditionUpdate(AlertCondition* c, bool rawNow, uint32_t nowMs)
{
    if (rawNow != c->rawExceeded)
    {
        c->rawExceeded = rawNow;
        c->sinceMs = nowMs;                       /* bắt đầu tính lại thời gian */
    }

    const uint32_t held = (uint32_t)(nowMs - c->sinceMs);
    if (c->active)
    {
        if (!c->rawExceeded && (held >= s_cfg.clearConfirmationTimeMs))
        {
            c->active = false;                    /* bình thường đủ lâu -> tắt */
        }
    }
    else
    {
        if (c->rawExceeded && (held >= s_cfg.confirmationTimeMs))
        {
            c->active = true;                     /* vượt ngưỡng đủ lâu -> bật */
        }
    }
}

void MedicalAlert_Init(const MedicalAlertThresholds* thresholds)
{
    if (thresholds != NULL)
    {
        s_cfg = *thresholds;
    }
    else
    {
        s_cfg.bpmLowThreshold        = ALERT_BPM_LOW_THRESHOLD;
        s_cfg.bpmHighThreshold       = ALERT_BPM_HIGH_THRESHOLD;
        s_cfg.spo2LowThreshold       = ALERT_SPO2_LOW_THRESHOLD;
        s_cfg.confirmationTimeMs     = ALERT_CONFIRMATION_MS;
        s_cfg.clearConfirmationTimeMs= ALERT_CLEAR_MS;
    }
    MedicalAlert_Reset();
}

void MedicalAlert_Update(const MedicalMeasurementUpdate* update)
{
    if (update == NULL)
    {
        return;
    }

    const bool eligible = eligibleState(update->measurementState) && update->signalValid;
    const bool bpmOk    = eligible && update->bpmValid;
    const bool spo2Ok   = eligible && update->spo2Valid;

    /* Điều kiện thô: chỉ true khi đủ điều kiện + metric hợp lệ + vượt ngưỡng. */
    const bool rawBpmLow  = bpmOk  && (update->currentBpm  < s_cfg.bpmLowThreshold);
    const bool rawBpmHigh = bpmOk  && (update->currentBpm  > s_cfg.bpmHighThreshold);
    const bool rawSpo2Low = spo2Ok && (update->currentSpo2 < s_cfg.spo2LowThreshold);

    conditionUpdate(&s_bpmLow,  rawBpmLow,  update->timestampMs);
    conditionUpdate(&s_bpmHigh, rawBpmHigh, update->timestampMs);
    conditionUpdate(&s_spo2Low, rawSpo2Low, update->timestampMs);
}

MedicalAlertFlags MedicalAlert_GetActiveFlags(void)
{
    uint32_t flags = (uint32_t)MEDICAL_ALERT_NONE;
    if (s_bpmLow.active)  { flags |= (uint32_t)MEDICAL_ALERT_BPM_LOW;  }
    if (s_bpmHigh.active) { flags |= (uint32_t)MEDICAL_ALERT_BPM_HIGH; }
    if (s_spo2Low.active) { flags |= (uint32_t)MEDICAL_ALERT_SPO2_LOW; }
    return (MedicalAlertFlags)flags;
}

bool MedicalAlert_IsActive(void)
{
    return MedicalAlert_GetActiveFlags() != MEDICAL_ALERT_NONE;
}

void MedicalAlert_Reset(void)
{
    conditionReset(&s_bpmLow);
    conditionReset(&s_bpmHigh);
    conditionReset(&s_spo2Low);
}
