/**
 * @file    dsp_task.c
 * @brief   Cài đặt DSP task (target). Chạy engine PPG ngoài GUI tick.
 * @note    User-owned. Chủ sở hữu duy nhất trạng thái engine đo.
 */

#include "dsp_task.h"
#include "app_init.h"
#include "ppg_measurement.h"
#include "ppg_sample_queue.h"
#include "rtc_service.h"
#include "temporary_history_store.h"
#include "measurement_types.h"
#include "buzzer_driver.h"
#include "buzzer_melodies.h"
#include "medical_alert_service.h"
#include "telemetry_service.h"
#include "alert_config.h"
#include "hw_config.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "stm32f4xx_hal.h"

/* -------------------------------------------------------------------------- */
/* Kết quả công bố (một-ghi DSP thread, một-đọc GUI thread).                   */
/* Một seqlock: reader thử lại khi generation lẻ (đang ghi) hoặc thay đổi giữa  */
/* lần copy, nên không bao giờ thấy một PpgResult bị xé.                        */
/* -------------------------------------------------------------------------- */
static PpgResult s_pub;
static volatile uint32_t s_pubGen; /* chẵn = ổn định, lẻ = đang ghi */

static void publishResult(const PpgResult* r)
{
    s_pubGen++;              /* -> lẻ: đang ghi */
    __DMB();
    s_pub = *r;
    __DMB();
    s_pubGen++;              /* -> chẵn: ổn định */
}

void DspTask_GetResult(PpgResult* out)
{
    if (out == 0)
    {
        return;
    }
    uint32_t g0;
    uint32_t attempts = 0U;
    do
    {
        g0 = s_pubGen;
        __DMB();
        *out = s_pub;
        __DMB();
        ++attempts;
    } while (((g0 & 1U) != 0U || g0 != s_pubGen) && (attempts < 8U));
}

/* Yêu cầu từ GUI thread, áp dụng trên DSP thread (chủ duy nhất của engine). Một
   lần ghi/đọc một word là atomic trên Cortex-M4; -1 nghĩa là "không có gì chờ". */
static volatile int s_reqFilterMode = -1;
static volatile int s_reqMaWindow = -1;

void DspTask_SetFilterMode(PpgFilterMode mode)
{
    s_reqFilterMode = (int)mode;
}

void DspTask_SetMaWindow(uint8_t window)
{
    s_reqMaWindow = (int)window;
}

static void applyPendingRequests(void)
{
    const int fm = s_reqFilterMode;
    if (fm >= 0)
    {
        Ppg_SetFilterMode((PpgFilterMode)fm);
        s_reqFilterMode = -1;
    }
    const int mw = s_reqMaWindow;
    if (mw >= 0)
    {
        Ppg_SetMaWindow((uint8_t)mw);
        s_reqMaWindow = -1;
    }
}

/* -------------------------------------------------------------------------- */
/* Xử lý kết quả đã chốt (nhấc ngón tay -> bản ghi lịch sử)                     */
/* -------------------------------------------------------------------------- */
/* Store lịch sử tạm được ghi ở đây (DSP thread) và đọc bởi GUI thread, nên cả
   hai phía tuần tự hóa trên mutex này. */
static osMutexId_t s_histMutex = NULL;

void DspTask_HistoryLock(void)
{
    if (s_histMutex != NULL) { (void)osMutexAcquire(s_histMutex, osWaitForever); }
}

void DspTask_HistoryUnlock(void)
{
    if (s_histMutex != NULL) { (void)osMutexRelease(s_histMutex); }
}

static DateTime s_startDt;
static bool s_startCaptured;
static bool s_prevResultReady;
static bool s_resultSaved;

/* Trạng thái cảnh báo + telemetry (khai báo ở đây vì handleFinalize dùng
   s_sessionId; phần cài đặt nằm ở mục "Cảnh báo y tế + telemetry" bên dưới). */
static PpgState s_prevTelemetryState = PPG_STATE_IDLE;
static uint32_t s_prevAlertFlags = (uint32_t)MEDICAL_ALERT_NONE;
static uint32_t s_sessionId = 0U;
static uint32_t s_vitalDivider = 0U;
static uint32_t s_sampleSeq = 0U;

/** Ghi lại timestamp phiên và, ở cạnh lên của finger-off, xây và lưu một bản ghi
    lịch sử đúng một lần (§24, §43). Ghi đè resultSaved cho GUI. */
static void handleFinalize(PpgResult* r)
{
    /* Ghi lại thời điểm bắt đầu phiên tại sample MEASURING đầu tiên. */
    if ((r->state == PPG_STATE_MEASURING) && !s_startCaptured)
    {
        bool v = false;
        RtcService_GetSnapshot(&s_startDt, &v);
        s_startCaptured = true;
    }
    if ((r->state == PPG_STATE_WAIT_FINGER) || (r->state == PPG_STATE_IDLE))
    {
        s_startCaptured = false;   /* sẵn sàng cho phiên kế tiếp */
    }

    /* Cạnh lên của resultReady: finalize một lần. */
    if (r->resultReady && !s_prevResultReady)
    {
        s_resultSaved = false;
        DateTime endDt;
        bool ev = false;
        RtcService_GetSnapshot(&endDt, &ev);

        if (r->resultStatus != MEASUREMENT_RESULT_INVALID)
        {
            MeasurementHistoryRecord rec = {0};
            rec.startDateTime = s_startCaptured ? s_startDt : endDt;
            rec.endDateTime = endDt;
            rec.durationMs = r->elapsedMeasurementMs;
            rec.averageBpm = r->averageBpm;
            rec.minimumBpm = r->bpmMin;
            rec.maximumBpm = r->bpmMax;
            rec.averageSpo2 = r->averageSpo2;
            rec.minimumSpo2 = r->spo2Min;
            rec.maximumSpo2 = r->spo2Max;
            rec.averageSqi = r->averageSqi;
            rec.acceptedPeakCount = r->acceptedPeaks;
            rec.rejectedPeakCount = r->rejectedPeaks;
            rec.droppedSampleCount = r->droppedSamples;
            rec.fifoOverflowCount = r->fifoOverflows;
            rec.bpmValid = r->averageBpmValid;
            rec.spo2Valid = r->averageSpo2Valid;
            rec.status = r->resultStatus;
            rec.endReason = r->endReason;

            DspTask_HistoryLock();
            const HistoryStatus hs = TemporaryHistory_Add(&rec);
            DspTask_HistoryUnlock();

            if (hs == HISTORY_STATUS_OK)
            {
                s_resultSaved = true;
                (void)Buzzer_PlayMelody(BUZZER_MELODY_DONE, BUZZER_MELODY_DONE_LEN);
                (void)Telemetry_PublishHistoryRecord(s_sessionId, &rec);
            }
        }
        else
        {
            /* Quá ngắn / không hợp lệ: không lưu, âm báo khác. */
            (void)Buzzer_PlayMelody(BUZZER_MELODY_INVALID, BUZZER_MELODY_INVALID_LEN);
        }

        /* Tổng kết phiên cho máy tính. */
        TelemetrySessionSummary sum;
        sum.sessionId   = s_sessionId;
        sum.durationMs  = r->elapsedMeasurementMs;
        sum.averageBpm  = r->averageBpm;
        sum.averageSpo2 = r->averageSpo2;
        sum.status      = r->resultStatus;
        sum.endReason   = r->endReason;
        (void)Telemetry_PublishSessionEnd(&sum);

        /* Nhấc ngón tay -> xóa cảnh báo ngay: không giữ LED theo giá trị cũ.
           Sensor task sẽ tắt LED ở vòng kế tiếp (~20 ms). */
        MedicalAlert_Reset();
    }
    if (!r->resultReady)
    {
        s_resultSaved = false;
    }
    s_prevResultReady = r->resultReady;
    r->resultSaved = s_resultSaved;   /* ghi đè cho snapshot GUI */
}

/* -------------------------------------------------------------------------- */
/* Cảnh báo y tế + telemetry                                                   */
/* -------------------------------------------------------------------------- */
/* DSP thread là nơi có kết quả đo, nên nó cập nhật cờ cảnh báo và publish
   telemetry. Nó KHÔNG chạm GPIO (LED do sensor task nháy theo cờ) và KHÔNG gọi
   UART (TelemetryTask sở hữu USART1); mọi publish đều non-blocking. */
#define DSP_VITAL_PERIOD_TICKS  100U   /* ~1 Hz ở nhịp 10 ms */

/** Đẩy một sample waveform (đã chia tần bên trong telemetry service). */
static void publishSample(const PpgResult* r)
{
    TelemetryPpgSample s;
    s.sequence     = s_sampleSeq++;
    s.redRaw       = r->redRaw;
    s.irRaw        = r->irRaw;
    s.redCentered  = r->redCentered;
    s.irCentered   = r->irCentered;
    s.redFiltered  = r->redFiltered;
    s.irFiltered   = r->irFiltered;
    s.bpm          = r->bpm;
    s.spo2         = r->spo2;
    s.sqi          = r->sqiPercent;
    s.state        = r->state;
    s.filterMode   = r->filterMode;
    s.maWindow     = r->maWindow;
    s.bpmValid     = r->bpmValid;
    s.spo2Valid    = r->spo2Valid;
    (void)Telemetry_PublishPpgSample(&s);
}

/** Gửi ALERT khi một cờ bật/tắt (không log từng nhịp nháy LED, §36). */
static void publishAlertChanges(const PpgResult* r, uint32_t nowFlags)
{
    const uint32_t changed = nowFlags ^ s_prevAlertFlags;
    if (changed == 0U)
    {
        return;
    }
    const uint32_t bits[3] = { (uint32_t)MEDICAL_ALERT_BPM_LOW,
                               (uint32_t)MEDICAL_ALERT_BPM_HIGH,
                               (uint32_t)MEDICAL_ALERT_SPO2_LOW };
    const float values[3]     = { r->bpm, r->bpm, r->spo2 };
    const float thresholds[3] = { ALERT_BPM_LOW_THRESHOLD,
                                  ALERT_BPM_HIGH_THRESHOLD,
                                  ALERT_SPO2_LOW_THRESHOLD };
    for (uint8_t i = 0U; i < 3U; ++i)
    {
        if ((changed & bits[i]) != 0U)
        {
            TelemetryAlertEvent e;
            e.flag      = bits[i];
            e.value     = values[i];
            e.threshold = thresholds[i];
            e.active    = ((nowFlags & bits[i]) != 0U);
            (void)Telemetry_PublishAlert(&e);
        }
    }
    s_prevAlertFlags = nowFlags;
}

/** Cập nhật cảnh báo + phát telemetry cho một kết quả mới. */
static void updateAlertsAndTelemetry(const PpgResult* r)
{
    /* Đổi trạng thái đo -> event; vào MEASURING -> phiên mới. */
    if (r->state != s_prevTelemetryState)
    {
        if ((r->state == PPG_STATE_MEASURING) &&
            (s_prevTelemetryState != PPG_STATE_MEASURING))
        {
            ++s_sessionId;
            (void)Telemetry_PublishSessionStart(s_sessionId);
        }
        (void)Telemetry_PublishMeasurementState(r->state);
        s_prevTelemetryState = r->state;
    }

    /* Đánh giá ngưỡng: chỉ MEASURING + tín hiệu ổn định + metric hợp lệ. */
    MedicalMeasurementUpdate u;
    u.currentBpm       = r->bpm;
    u.currentSpo2      = r->spo2;
    u.bpmValid         = r->bpmValid;
    u.spo2Valid        = r->spo2Valid;
    u.signalValid      = r->signalStable;
    u.measurementState = r->state;
    u.timestampMs      = HAL_GetTick();
    MedicalAlert_Update(&u);
    publishAlertChanges(r, (uint32_t)MedicalAlert_GetActiveFlags());

    /* CHỈ stream giá trị đo (waveform + vital) khi đang đo trực tiếp và tín hiệu
       ổn định. Trước ổn định (WAIT_FINGER / STABILIZING) hoặc khi mất ổn định
       giữa chừng thì KHÔNG gửi RED/IR/BPM/SpO2 — chỉ event/state/alert (đã publish
       ở trên) vẫn đi qua. Mất ổn định làm state != MEASURING ở nhịp sau nên stream
       tự dừng, và event đổi trạng thái vẫn được gửi. */
    const bool streamAllowed = r->fingerPresent && r->signalStable &&
                               (r->state == PPG_STATE_MEASURING);
    if (streamAllowed)
    {
        publishSample(r);

        /* BPM/SpO2 tóm tắt ~1 Hz (không gửi kèm mỗi sample waveform, §31). */
        if (++s_vitalDivider >= DSP_VITAL_PERIOD_TICKS)
        {
            s_vitalDivider = 0U;
            TelemetryVitalResult v;
            v.bpm         = r->bpm;
            v.averageBpm  = r->averageBpm;
            v.spo2        = r->spo2;
            v.averageSpo2 = r->averageSpo2;
            v.sqi         = r->sqiPercent;
            v.state       = r->state;
            v.bpmValid    = r->bpmValid;
            v.spo2Valid   = r->spo2Valid;
            (void)Telemetry_PublishVitalResult(&v);
        }
    }
    else
    {
        s_vitalDivider = 0U;   /* reset nhịp vital để lần đo sau bắt đầu sạch */
    }
}

/* -------------------------------------------------------------------------- */
/* Thread                                                                      */
/* -------------------------------------------------------------------------- */
#define DSP_TASK_PERIOD_MS   10U   /* nhịp rút + công bố (~100 Hz)             */
#define DSP_DRAIN_GUARD      512U  /* số sample tối đa xử lý mỗi lần thức      */

static void dspLoop(void)
{
    Ppg_Init();
    (void)TemporaryHistory_Init();

    uint32_t lastDropped = 0U;
    uint32_t lastOverflow = 0U;

    /* Công bố một kết quả idle ban đầu để GUI có dữ liệu nhất quán. */
    PpgResult r;
    Ppg_GetResult(&r);
    publishResult(&r);

    for (;;)
    {
        applyPendingRequests();
        Ppg_SetSensorError(g_sensorOk == 0);

        const uint32_t dropped = PpgQueue_DroppedCount();
        const uint32_t overflow = g_fifoOverflowTotal;
        Ppg_ReportLoss(dropped - lastDropped, overflow - lastOverflow);
        lastDropped = dropped;
        lastOverflow = overflow;

        PpgRawSample s;
        uint32_t guard = 0U;
        while ((guard < DSP_DRAIN_GUARD) && PpgQueue_Pop(&s))
        {
            Ppg_PushSample(&s);
            ++guard;
        }

        Ppg_GetResult(&r);
        handleFinalize(&r);
        publishResult(&r);
        updateAlertsAndTelemetry(&r);

        osDelay(DSP_TASK_PERIOD_MS);
    }
}

static void dspThreadEntry(void* argument)
{
    (void)argument;
    dspLoop();
}

/* Cấp phát tĩnh (configSUPPORT_STATIC_ALLOCATION = 1): giữ DSP thread ngoài
   FreeRTOS heap. 1024 word = 4 KB stack (phép tính float + copy kết quả). */
static StaticTask_t s_dspCb;
static uint32_t s_dspStack[1024];
static osThreadId_t s_dspThread = NULL;

static const osThreadAttr_t s_dspAttr = {
    .name = "dspTask",
    .cb_mem = &s_dspCb,
    .cb_size = sizeof(s_dspCb),
    .stack_mem = s_dspStack,
    .stack_size = sizeof(s_dspStack),
    .priority = (osPriority_t)osPriorityNormal,
};

void DspTask_Start(void)
{
    if (s_histMutex == NULL)
    {
        s_histMutex = osMutexNew(NULL);
    }
    if (s_dspThread == NULL)
    {
        s_dspThread = osThreadNew(dspThreadEntry, NULL, &s_dspAttr);
    }
}
