/**
 * @file    telemetry_service.c
 * @brief   Cài đặt telemetry: hàng đợi tĩnh + TelemetryTask + USART1 TX ngắt.
 * @note    User-owned. Chủ sở hữu duy nhất của USART1 TX.
 */

#include "telemetry_service.h"
#include "telemetry_formatter.h"
#include "hw_config.h"
#include "stm32f4xx_hal.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include <stddef.h>
#include <string.h>

/* Handle UART do CubeMX sinh trong main.c (HAL HANDLE RULE: chỉ tham chiếu). */
extern UART_HandleTypeDef HW_TELEMETRY_UART;

/* -------------------------------------------------------------------------- */
/* Kích thước tài nguyên (cấp phát tĩnh: không đụng FreeRTOS heap)              */
/* -------------------------------------------------------------------------- */
/** Hàng sự kiện: ưu tiên cao, không được mất kết quả/cảnh báo/lịch sử.        */
#define TELEMETRY_EVENT_QUEUE_LEN     24U
/** Hàng waveform: được phép bỏ khi quá tải (§40).                             */
#define TELEMETRY_WAVEFORM_QUEUE_LEN  64U
/** Buffer một dòng text (§38); dòng dài hơn bị coi là lỗi format, không gửi.  */
#define TELEMETRY_TX_BUFFER_SIZE      256U
/** Số sample waveform gửi liên tiếp trước khi kiểm lại hàng sự kiện.          */
#define TELEMETRY_WAVEFORM_BURST      8U
/** Chờ tối đa một lần truyền hoàn tất (ms) trước khi coi là kẹt.              */
#define TELEMETRY_TX_TIMEOUT_MS       200U
/** Nhịp nghỉ khi không có gì để gửi (ms).                                     */
#define TELEMETRY_IDLE_DELAY_MS       2U
/** Cờ báo TX hoàn tất từ ISR.                                                 */
#define TELEMETRY_TX_DONE_FLAG        0x01U

/* -------------------------------------------------------------------------- */
/* Trạng thái                                                                  */
/* -------------------------------------------------------------------------- */
static TelemetryConfiguration s_cfg;
static TelemetryCounters s_counters;
static uint16_t s_decimationCount;      /**< Bộ đếm chia tần waveform.        */

static osMessageQueueId_t s_eventQ = NULL;
static osMessageQueueId_t s_waveQ  = NULL;
static osThreadId_t s_thread = NULL;

static char s_txBuf[TELEMETRY_TX_BUFFER_SIZE];

/* Bộ nhớ tĩnh cho hàng đợi + thread. */
static StaticQueue_t s_eventQCb;
static uint8_t s_eventQBuf[TELEMETRY_EVENT_QUEUE_LEN * sizeof(TelemetryMessage)];
static StaticQueue_t s_waveQCb;
static uint8_t s_waveQBuf[TELEMETRY_WAVEFORM_QUEUE_LEN * sizeof(TelemetryMessage)];

static StaticTask_t s_threadCb;
static uint32_t s_threadStack[512];     /**< 2 KB: snprintf + copy thông điệp. */

/* -------------------------------------------------------------------------- */
/* Đưa vào hàng đợi (non-blocking, thất bại nhanh khi đầy)                     */
/* -------------------------------------------------------------------------- */
static TelemetryStatus enqueue(osMessageQueueId_t q, const TelemetryMessage* msg,
                               uint32_t* dropCounter)
{
    if (!s_cfg.enabled)
    {
        return TELEMETRY_STATUS_DISABLED;
    }
    if ((q == NULL) || (msg == NULL))
    {
        return TELEMETRY_STATUS_INVALID_ARGUMENT;
    }
    /* timeout 0: publisher (SensorTask/DspTask/GUI) không bao giờ bị chặn. */
    if (osMessageQueuePut(q, msg, 0U, 0U) != osOK)
    {
        ++(*dropCounter);
        return TELEMETRY_STATUS_QUEUE_FULL;
    }
    return TELEMETRY_STATUS_OK;
}

/** Khởi tạo phần chung của một thông điệp. */
static void initMsg(TelemetryMessage* m, TelemetryMessageType type)
{
    memset(m, 0, sizeof(*m));
    m->type = type;
    m->timestampMs = HAL_GetTick();
}

/* -------------------------------------------------------------------------- */
/* API cấu hình / khởi tạo                                                     */
/* -------------------------------------------------------------------------- */
void Telemetry_Init(void)
{
    memset(&s_counters, 0, sizeof(s_counters));
    s_decimationCount = 0U;

    s_cfg.enabled              = true;
    s_cfg.streamWaveform       = true;
    s_cfg.streamUserActions    = true;
    s_cfg.streamScreenChanges  = true;
    s_cfg.streamSettingChanges = true;
    s_cfg.streamVitalResults   = true;
    s_cfg.streamAlerts         = true;
    s_cfg.waveformDecimation   = 1U;   /* gửi mọi sample */
}

void Telemetry_SetConfiguration(const TelemetryConfiguration* cfg)
{
    if (cfg == NULL)
    {
        return;
    }
    s_cfg = *cfg;
    if (s_cfg.waveformDecimation == 0U)
    {
        s_cfg.waveformDecimation = 1U;   /* 0 vô nghĩa -> coi như gửi mọi sample */
    }
}

void Telemetry_GetConfiguration(TelemetryConfiguration* out)
{
    if (out != NULL)
    {
        *out = s_cfg;
    }
}

/* -------------------------------------------------------------------------- */
/* Publish (gọi từ SensorTask / DspTask / luồng GUI — luôn non-blocking)       */
/* -------------------------------------------------------------------------- */
TelemetryStatus Telemetry_PublishPpgSample(const TelemetryPpgSample* sample)
{
    if (sample == NULL)
    {
        return TELEMETRY_STATUS_INVALID_ARGUMENT;
    }
    if (!s_cfg.streamWaveform)
    {
        return TELEMETRY_STATUS_DISABLED;
    }
    /* Chia tần: chỉ gửi mỗi waveformDecimation sample. */
    ++s_decimationCount;
    if (s_decimationCount < s_cfg.waveformDecimation)
    {
        return TELEMETRY_STATUS_OK;
    }
    s_decimationCount = 0U;

    TelemetryMessage m;
    initMsg(&m, TELEMETRY_MSG_PPG_SAMPLE);
    m.payload.ppg = *sample;
    return enqueue(s_waveQ, &m, &s_counters.droppedWaveform);
}

TelemetryStatus Telemetry_PublishVitalResult(const TelemetryVitalResult* result)
{
    if (result == NULL)
    {
        return TELEMETRY_STATUS_INVALID_ARGUMENT;
    }
    if (!s_cfg.streamVitalResults)
    {
        return TELEMETRY_STATUS_DISABLED;
    }
    TelemetryMessage m;
    initMsg(&m, TELEMETRY_MSG_VITAL_RESULT);
    m.payload.vital = *result;
    return enqueue(s_eventQ, &m, &s_counters.droppedEvent);
}

TelemetryStatus Telemetry_PublishMeasurementState(PpgState state)
{
    TelemetryMessage m;
    initMsg(&m, TELEMETRY_MSG_MEASUREMENT_STATE);
    m.payload.measurementState = state;
    return enqueue(s_eventQ, &m, &s_counters.droppedEvent);
}

TelemetryStatus Telemetry_PublishScreenChange(ApplicationScreen from, ApplicationScreen to)
{
    if (!s_cfg.streamScreenChanges)
    {
        return TELEMETRY_STATUS_DISABLED;
    }
    TelemetryMessage m;
    initMsg(&m, TELEMETRY_MSG_SCREEN_CHANGED);
    m.payload.screen.from = from;
    m.payload.screen.to = to;
    return enqueue(s_eventQ, &m, &s_counters.droppedEvent);
}

TelemetryStatus Telemetry_PublishUserAction(TelemetryUserActionId id, ApplicationScreen screen)
{
    if (!s_cfg.streamUserActions)
    {
        return TELEMETRY_STATUS_DISABLED;
    }
    TelemetryMessage m;
    initMsg(&m, TELEMETRY_MSG_USER_ACTION);
    m.payload.action.id = id;
    m.payload.action.screen = screen;
    return enqueue(s_eventQ, &m, &s_counters.droppedEvent);
}

TelemetryStatus Telemetry_PublishSettingChange(const TelemetrySettingEvent* change)
{
    if (change == NULL)
    {
        return TELEMETRY_STATUS_INVALID_ARGUMENT;
    }
    if (!s_cfg.streamSettingChanges)
    {
        return TELEMETRY_STATUS_DISABLED;
    }
    TelemetryMessage m;
    initMsg(&m, TELEMETRY_MSG_SETTING_CHANGED);
    m.payload.setting = *change;
    return enqueue(s_eventQ, &m, &s_counters.droppedEvent);
}

TelemetryStatus Telemetry_PublishAlert(const TelemetryAlertEvent* alert)
{
    if (alert == NULL)
    {
        return TELEMETRY_STATUS_INVALID_ARGUMENT;
    }
    if (!s_cfg.streamAlerts)
    {
        return TELEMETRY_STATUS_DISABLED;
    }
    TelemetryMessage m;
    initMsg(&m, TELEMETRY_MSG_ALERT);
    m.payload.alert = *alert;
    return enqueue(s_eventQ, &m, &s_counters.droppedEvent);
}

TelemetryStatus Telemetry_PublishSessionStart(uint32_t sessionId)
{
    TelemetryMessage m;
    initMsg(&m, TELEMETRY_MSG_SESSION_START);
    m.payload.session.sessionId = sessionId;
    return enqueue(s_eventQ, &m, &s_counters.droppedEvent);
}

TelemetryStatus Telemetry_PublishSessionEnd(const TelemetrySessionSummary* summary)
{
    if (summary == NULL)
    {
        return TELEMETRY_STATUS_INVALID_ARGUMENT;
    }
    TelemetryMessage m;
    initMsg(&m, TELEMETRY_MSG_SESSION_END);
    m.payload.session = *summary;
    return enqueue(s_eventQ, &m, &s_counters.droppedEvent);
}

TelemetryStatus Telemetry_PublishHistoryRecord(uint32_t sessionId,
                                               const MeasurementHistoryRecord* record)
{
    if (record == NULL)
    {
        return TELEMETRY_STATUS_INVALID_ARGUMENT;
    }
    TelemetryMessage m;
    initMsg(&m, TELEMETRY_MSG_HISTORY);
    m.payload.session.sessionId  = sessionId;
    m.payload.session.durationMs = record->durationMs;
    m.payload.session.averageBpm = record->averageBpm;
    m.payload.session.averageSpo2= record->averageSpo2;
    m.payload.session.status     = record->status;
    m.payload.session.endReason  = record->endReason;
    return enqueue(s_eventQ, &m, &s_counters.droppedEvent);
}

TelemetryStatus Telemetry_PublishSystem(uint32_t code)
{
    TelemetryMessage m;
    initMsg(&m, TELEMETRY_MSG_SYSTEM);
    m.payload.systemCode = code;
    return enqueue(s_eventQ, &m, &s_counters.droppedEvent);
}

uint32_t Telemetry_GetDroppedMessageCount(void)
{
    return s_counters.droppedWaveform + s_counters.droppedEvent;
}

void Telemetry_GetCounters(TelemetryCounters* out)
{
    if (out != NULL)
    {
        *out = s_counters;
    }
}

/* -------------------------------------------------------------------------- */
/* Truyền (chỉ TelemetryTask chạy phần này)                                    */
/* -------------------------------------------------------------------------- */
void Telemetry_OnTxCompleteFromIsr(void)
{
    if (s_thread != NULL)
    {
        (void)osThreadFlagsSet(s_thread, TELEMETRY_TX_DONE_FLAG);
    }
}

/** Truyền một dòng bằng ngắt rồi chờ hoàn tất (buffer tĩnh nên an toàn). */
static void sendLine(size_t len)
{
    if (len == 0U)
    {
        ++s_counters.formatError;
        return;
    }
    (void)osThreadFlagsClear(TELEMETRY_TX_DONE_FLAG);
    if (HAL_UART_Transmit_IT(&HW_TELEMETRY_UART, (uint8_t*)s_txBuf, (uint16_t)len) != HAL_OK)
    {
        ++s_counters.uartBusy;
        return;
    }
    const uint32_t r = osThreadFlagsWait(TELEMETRY_TX_DONE_FLAG, osFlagsWaitAny,
                                         TELEMETRY_TX_TIMEOUT_MS);
    if ((r & osFlagsError) != 0U)
    {
        ++s_counters.uartBusy;   /* quá hạn: bỏ dòng này, không treo task */
    }
    else
    {
        ++s_counters.sent;
    }
}

/** Format rồi gửi một thông điệp. */
static void formatAndSend(const TelemetryMessage* m)
{
    const size_t n = TelemetryFormatter_Format(m, s_txBuf, sizeof s_txBuf);
    sendLine(n);
}

static void telemetryLoop(void)
{
    /* Header CSV gửi đúng một lần khi bắt đầu stream (§29). */
    sendLine(TelemetryFormatter_Header(s_txBuf, sizeof s_txBuf));

    TelemetryMessage msg;
    for (;;)
    {
        bool didWork = false;

        /* Ưu tiên hàng sự kiện: rút hết trước khi đụng waveform (§40). */
        while (osMessageQueueGet(s_eventQ, &msg, NULL, 0U) == osOK)
        {
            formatAndSend(&msg);
            didWork = true;
        }

        /* Rồi gửi một cụm waveform, sau đó quay lại kiểm hàng sự kiện. */
        for (uint32_t i = 0U; i < TELEMETRY_WAVEFORM_BURST; ++i)
        {
            if (osMessageQueueGet(s_waveQ, &msg, NULL, 0U) != osOK)
            {
                break;
            }
            formatAndSend(&msg);
            didWork = true;
        }

        if (!didWork)
        {
            osDelay(TELEMETRY_IDLE_DELAY_MS);
        }
    }
}

static void telemetryThreadEntry(void* argument)
{
    (void)argument;
    telemetryLoop();
}

/* Ưu tiên thấp hơn Sensor/DSP/GUI: telemetry không bao giờ làm chậm pipeline đo. */
static const osThreadAttr_t s_threadAttr = {
    .name = "telemetryTask",
    .cb_mem = &s_threadCb,
    .cb_size = sizeof(s_threadCb),
    .stack_mem = s_threadStack,
    .stack_size = sizeof(s_threadStack),
    .priority = (osPriority_t)osPriorityBelowNormal,
};

static const osMessageQueueAttr_t s_eventQAttr = {
    .name = "telemetryEventQ",
    .cb_mem = &s_eventQCb,
    .cb_size = sizeof(s_eventQCb),
    .mq_mem = s_eventQBuf,
    .mq_size = sizeof(s_eventQBuf),
};

static const osMessageQueueAttr_t s_waveQAttr = {
    .name = "telemetryWaveQ",
    .cb_mem = &s_waveQCb,
    .cb_size = sizeof(s_waveQCb),
    .mq_mem = s_waveQBuf,
    .mq_size = sizeof(s_waveQBuf),
};

void Telemetry_Start(void)
{
    if (s_eventQ == NULL)
    {
        s_eventQ = osMessageQueueNew(TELEMETRY_EVENT_QUEUE_LEN,
                                     sizeof(TelemetryMessage), &s_eventQAttr);
    }
    if (s_waveQ == NULL)
    {
        s_waveQ = osMessageQueueNew(TELEMETRY_WAVEFORM_QUEUE_LEN,
                                    sizeof(TelemetryMessage), &s_waveQAttr);
    }
    if (s_thread == NULL)
    {
        s_thread = osThreadNew(telemetryThreadEntry, NULL, &s_threadAttr);
    }
}

/* -------------------------------------------------------------------------- */
/* Callback HAL (strong-symbol override, an toàn khi regenerate)                */
/* -------------------------------------------------------------------------- */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef* huart)
{
    if (huart->Instance == USART1)
    {
        Telemetry_OnTxCompleteFromIsr();
    }
}
