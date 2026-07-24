#ifndef TELEMETRY_SERVICE_H
#define TELEMETRY_SERVICE_H

/**
 * @file    telemetry_service.h
 * @brief   Telemetry thời gian thực qua USART1 (hàng đợi + TelemetryTask + IT-TX).
 *
 * Chỉ phục vụ học tập / debug, KHÔNG phải dữ liệu y tế được chứng nhận. Các hàm
 * Telemetry_Publish* là NON-BLOCKING: chúng chỉ đóng gói một @ref TelemetryMessage
 * và bỏ vào hàng đợi (không format chuỗi, không gọi UART/DMA). Đúng một task —
 * TelemetryTask — là chủ sở hữu USART1 TX; nó rút hàng đợi, format CSV và truyền
 * bằng HAL_UART_Transmit_IT (non-blocking).
 *
 * Hai hàng đợi (§40): hàng sự kiện ưu tiên cao (không mất kết quả/cảnh báo/lịch
 * sử) và hàng waveform (được phép bỏ khi quá tải). SensorTask, DspTask và luồng
 * GUI có thể publish; chúng KHÔNG bao giờ gọi UART trực tiếp. User-owned.
 */

#include <stdint.h>
#include <stdbool.h>
#include "ppg_types.h"           /* PpgState, PpgFilterMode */
#include "measurement_types.h"   /* MeasurementHistoryRecord, ...Status/Reason */

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/* Màn hình ứng dụng (§7) — độc lập với tên screen generated của TouchGFX       */
/* -------------------------------------------------------------------------- */
typedef enum
{
    APP_SCREEN_UNKNOWN = 0,
    APP_SCREEN_BOOT,
    APP_SCREEN_HOME,
    APP_SCREEN_DASHBOARD,
    APP_SCREEN_WAVEFORM,
    APP_SCREEN_HISTORY,
    APP_SCREEN_SETTINGS,
    APP_SCREEN_DATETIME_SETTINGS,
    APP_SCREEN_ABOUT
} ApplicationScreen;

/* -------------------------------------------------------------------------- */
/* Trạng thái trả về (§37)                                                      */
/* -------------------------------------------------------------------------- */
typedef enum
{
    TELEMETRY_STATUS_OK = 0,
    TELEMETRY_STATUS_DISABLED,
    TELEMETRY_STATUS_QUEUE_FULL,
    TELEMETRY_STATUS_INVALID_ARGUMENT
} TelemetryStatus;

/* -------------------------------------------------------------------------- */
/* Thao tác người dùng (§33)                                                    */
/* -------------------------------------------------------------------------- */
typedef enum
{
    TELEMETRY_ACTION_BUTTON_B1 = 0,
    TELEMETRY_ACTION_START_MEASUREMENT,
    TELEMETRY_ACTION_STOP_MEASUREMENT,
    TELEMETRY_ACTION_VIEW_HISTORY_DETAIL,
    TELEMETRY_ACTION_CLEAR_HISTORY,
    TELEMETRY_ACTION_RESTORE_DEFAULTS,
    TELEMETRY_ACTION_CONFIRM_POPUP,
    TELEMETRY_ACTION_CANCEL_POPUP,
    TELEMETRY_ACTION_B1_IGNORED   /**< B1 nhấn ở màn hình không hỗ trợ toggle.  */
} TelemetryUserActionId;

/* -------------------------------------------------------------------------- */
/* Định danh cài đặt bị thay đổi (§33)                                          */
/* -------------------------------------------------------------------------- */
typedef enum
{
    TELEMETRY_SETTING_WAVEFORM_CHANNEL = 0, /**< intValue: 0=RED, 1=IR.         */
    TELEMETRY_SETTING_SIGNAL_TYPE,          /**< intValue: 0=RAW, 1=FILTERED.   */
    TELEMETRY_SETTING_FILTER_MODE,          /**< intValue: PpgFilterMode.       */
    TELEMETRY_SETTING_FILTER_WINDOW,        /**< intValue: cửa sổ MA N.         */
    TELEMETRY_SETTING_BUZZER_ENABLED,       /**< intValue: 0/1.                 */
    TELEMETRY_SETTING_BPM_LOW_THRESHOLD,    /**< floatValue.                    */
    TELEMETRY_SETTING_BPM_HIGH_THRESHOLD,   /**< floatValue.                    */
    TELEMETRY_SETTING_SPO2_LOW_THRESHOLD    /**< floatValue.                    */
} TelemetrySettingId;

/* -------------------------------------------------------------------------- */
/* Loại thông điệp + payload (§26, §30)                                         */
/* -------------------------------------------------------------------------- */
typedef enum
{
    TELEMETRY_MSG_SYSTEM = 0,
    TELEMETRY_MSG_USER_ACTION,
    TELEMETRY_MSG_SCREEN_CHANGED,
    TELEMETRY_MSG_SETTING_CHANGED,
    TELEMETRY_MSG_MEASUREMENT_STATE,
    TELEMETRY_MSG_PPG_SAMPLE,
    TELEMETRY_MSG_VITAL_RESULT,
    TELEMETRY_MSG_ALERT,
    TELEMETRY_MSG_SESSION_START,
    TELEMETRY_MSG_SESSION_END,
    TELEMETRY_MSG_HISTORY
} TelemetryMessageType;

/** @brief Một sample PPG realtime (§30). */
typedef struct
{
    uint32_t sequence;
    uint32_t redRaw;
    uint32_t irRaw;
    int32_t  redCentered;
    int32_t  irCentered;
    int32_t  redFiltered;
    int32_t  irFiltered;
    float    bpm;
    float    spo2;
    float    sqi;
    PpgState state;
    PpgFilterMode filterMode;
    uint8_t  maWindow;
    bool     bpmValid;
    bool     spo2Valid;
} TelemetryPpgSample;

/** @brief Kết quả sinh hiệu (BPM/SpO2) tức thời + trung bình phiên. */
typedef struct
{
    float bpm;
    float averageBpm;
    float spo2;
    float averageSpo2;
    float sqi;
    PpgState state;
    bool  bpmValid;
    bool  spo2Valid;
} TelemetryVitalResult;

/** @brief Sự kiện đổi màn hình. */
typedef struct
{
    ApplicationScreen from;
    ApplicationScreen to;
} TelemetryScreenEvent;

/** @brief Sự kiện đổi cài đặt. */
typedef struct
{
    TelemetrySettingId id;
    int32_t intValue;
    float   floatValue;
} TelemetrySettingEvent;

/** @brief Sự kiện thao tác người dùng. */
typedef struct
{
    TelemetryUserActionId id;
    ApplicationScreen screen;   /**< Màn hình khi thao tác (ngữ cảnh).         */
} TelemetryUserActionEvent;

/** @brief Sự kiện cảnh báo (§36). */
typedef struct
{
    uint32_t flag;        /**< Một @ref MedicalAlertFlags bit.                 */
    float    value;       /**< Giá trị đo lúc đổi trạng thái.                  */
    float    threshold;   /**< Ngưỡng liên quan.                              */
    bool     active;      /**< true = kích hoạt, false = gỡ.                  */
} TelemetryAlertEvent;

/** @brief Tóm tắt kết thúc phiên / bản ghi lịch sử (§35). */
typedef struct
{
    uint32_t sessionId;
    uint32_t durationMs;
    float    averageBpm;
    float    averageSpo2;
    MeasurementResultStatus status;
    MeasurementEndReason endReason;
} TelemetrySessionSummary;

/** @brief Thông điệp telemetry đưa vào hàng đợi (kích thước cố định, no malloc). */
typedef struct
{
    TelemetryMessageType type;
    uint32_t timestampMs;
    union
    {
        uint32_t                 systemCode;
        TelemetryPpgSample       ppg;
        TelemetryVitalResult     vital;
        TelemetryScreenEvent     screen;
        TelemetrySettingEvent    setting;
        TelemetryUserActionEvent action;
        TelemetryAlertEvent      alert;
        TelemetrySessionSummary  session;
        PpgState                 measurementState;
    } payload;
} TelemetryMessage;

/* -------------------------------------------------------------------------- */
/* Cấu hình stream (§32)                                                        */
/* -------------------------------------------------------------------------- */
typedef struct
{
    bool     enabled;             /**< Tắt toàn bộ telemetry nếu false.        */
    bool     streamWaveform;      /**< Bật/tắt stream sample PPG.              */
    bool     streamUserActions;
    bool     streamScreenChanges;
    bool     streamSettingChanges;
    bool     streamVitalResults;
    bool     streamAlerts;
    uint16_t waveformDecimation;  /**< 1 = mọi sample; N = mỗi N sample.       */
} TelemetryConfiguration;

/* -------------------------------------------------------------------------- */
/* Bộ đếm chẩn đoán (§40)                                                       */
/* -------------------------------------------------------------------------- */
typedef struct
{
    uint32_t droppedWaveform;
    uint32_t droppedEvent;
    uint32_t uartBusy;
    uint32_t formatError;
    uint32_t sent;
} TelemetryCounters;

/* -------------------------------------------------------------------------- */
/* API                                                                          */
/* -------------------------------------------------------------------------- */
/** @brief Khởi tạo cấu hình + bộ đếm (gọi trước scheduler). */
void Telemetry_Init(void);

/** @brief Tạo hàng đợi + khởi động TelemetryTask (gọi sau khi scheduler chạy). */
void Telemetry_Start(void);

/** @brief Ghi đè cấu hình stream. NULL -> giữ nguyên. */
void Telemetry_SetConfiguration(const TelemetryConfiguration* cfg);

/** @brief Lấy cấu hình hiện tại. */
void Telemetry_GetConfiguration(TelemetryConfiguration* out);

TelemetryStatus Telemetry_PublishPpgSample(const TelemetryPpgSample* sample);
TelemetryStatus Telemetry_PublishVitalResult(const TelemetryVitalResult* result);
TelemetryStatus Telemetry_PublishMeasurementState(PpgState state);
TelemetryStatus Telemetry_PublishScreenChange(ApplicationScreen from, ApplicationScreen to);
TelemetryStatus Telemetry_PublishUserAction(TelemetryUserActionId id, ApplicationScreen screen);
TelemetryStatus Telemetry_PublishSettingChange(const TelemetrySettingEvent* change);
TelemetryStatus Telemetry_PublishAlert(const TelemetryAlertEvent* alert);
TelemetryStatus Telemetry_PublishSessionStart(uint32_t sessionId);
TelemetryStatus Telemetry_PublishSessionEnd(const TelemetrySessionSummary* summary);
TelemetryStatus Telemetry_PublishHistoryRecord(uint32_t sessionId, const MeasurementHistoryRecord* record);
TelemetryStatus Telemetry_PublishSystem(uint32_t code);

/** @brief Tổng số thông điệp bị bỏ (waveform + event). */
uint32_t Telemetry_GetDroppedMessageCount(void);

/** @brief Lấy bản sao bộ đếm chẩn đoán. */
void Telemetry_GetCounters(TelemetryCounters* out);

/** @brief Báo hoàn tất truyền UART từ ISR (gọi trong HAL_UART_TxCpltCallback). */
void Telemetry_OnTxCompleteFromIsr(void);

#ifdef __cplusplus
}
#endif

#endif /* TELEMETRY_SERVICE_H */
