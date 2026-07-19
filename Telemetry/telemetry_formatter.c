/**
 * @file    telemetry_formatter.c
 * @brief   Cài đặt formatter CSV cho telemetry (snprintf có kiểm biên).
 * @note    User-owned (ngoài các thư mục generated).
 */

#include "telemetry_formatter.h"
#include "medical_alert_service.h"   /* MedicalAlertFlags */
#include "rtc_service.h"
#include "datetime.h"
#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------- */
/* Ánh xạ enum -> chuỗi                                                         */
/* -------------------------------------------------------------------------- */
static const char* stateStr(PpgState s)
{
    switch (s)
    {
        case PPG_STATE_IDLE:           return "IDLE";
        case PPG_STATE_WAIT_FINGER:    return "WAIT_FINGER";
        case PPG_STATE_STABILIZING:    return "STABILIZING";
        case PPG_STATE_MEASURING:      return "MEASURING";
        case PPG_STATE_INVALID_SIGNAL: return "INVALID_SIGNAL";
        case PPG_STATE_SENSOR_ERROR:   return "SENSOR_ERROR";
        case PPG_STATE_RESULT_READY:   return "RESULT_READY";
        default:                       return "UNKNOWN";
    }
}

static const char* screenStr(ApplicationScreen s)
{
    switch (s)
    {
        case APP_SCREEN_BOOT:              return "BOOT";
        case APP_SCREEN_HOME:              return "HOME";
        case APP_SCREEN_DASHBOARD:         return "DASHBOARD";
        case APP_SCREEN_WAVEFORM:          return "WAVEFORM";
        case APP_SCREEN_HISTORY:           return "HISTORY";
        case APP_SCREEN_SETTINGS:          return "SETTINGS";
        case APP_SCREEN_DATETIME_SETTINGS: return "DATETIME_SETTINGS";
        case APP_SCREEN_ABOUT:             return "ABOUT";
        default:                           return "UNKNOWN";
    }
}

static const char* actionStr(TelemetryUserActionId a)
{
    switch (a)
    {
        case TELEMETRY_ACTION_BUTTON_B1:          return "BUTTON_B1";
        case TELEMETRY_ACTION_START_MEASUREMENT:  return "START_MEASUREMENT";
        case TELEMETRY_ACTION_STOP_MEASUREMENT:   return "STOP_MEASUREMENT";
        case TELEMETRY_ACTION_VIEW_HISTORY_DETAIL:return "VIEW_HISTORY_DETAIL";
        case TELEMETRY_ACTION_CLEAR_HISTORY:      return "CLEAR_HISTORY";
        case TELEMETRY_ACTION_RESTORE_DEFAULTS:   return "RESTORE_DEFAULTS";
        case TELEMETRY_ACTION_CONFIRM_POPUP:      return "CONFIRM_POPUP";
        case TELEMETRY_ACTION_CANCEL_POPUP:       return "CANCEL_POPUP";
        case TELEMETRY_ACTION_B1_IGNORED:         return "B1_IGNORED";
        default:                                  return "UNKNOWN";
    }
}

static const char* settingStr(TelemetrySettingId s)
{
    switch (s)
    {
        case TELEMETRY_SETTING_WAVEFORM_CHANNEL:  return "WAVEFORM_CHANNEL";
        case TELEMETRY_SETTING_SIGNAL_TYPE:       return "SIGNAL_TYPE";
        case TELEMETRY_SETTING_FILTER_MODE:       return "FILTER_MODE";
        case TELEMETRY_SETTING_FILTER_WINDOW:     return "FILTER_WINDOW";
        case TELEMETRY_SETTING_BUZZER_ENABLED:    return "BUZZER_ENABLED";
        case TELEMETRY_SETTING_BPM_LOW_THRESHOLD: return "BPM_LOW_THRESHOLD";
        case TELEMETRY_SETTING_BPM_HIGH_THRESHOLD:return "BPM_HIGH_THRESHOLD";
        case TELEMETRY_SETTING_SPO2_LOW_THRESHOLD:return "SPO2_LOW_THRESHOLD";
        default:                                  return "UNKNOWN";
    }
}

static const char* alertStr(uint32_t flag)
{
    switch (flag)
    {
        case (uint32_t)MEDICAL_ALERT_BPM_LOW:  return "BPM_LOW";
        case (uint32_t)MEDICAL_ALERT_BPM_HIGH: return "BPM_HIGH";
        case (uint32_t)MEDICAL_ALERT_SPO2_LOW: return "SPO2_LOW";
        default:                               return "UNKNOWN";
    }
}

static const char* statusStr(MeasurementResultStatus s)
{
    switch (s)
    {
        case MEASUREMENT_RESULT_VALID:   return "VALID";
        case MEASUREMENT_RESULT_PARTIAL: return "PARTIAL";
        case MEASUREMENT_RESULT_INVALID: return "INVALID";
        default:                         return "UNKNOWN";
    }
}

static const char* reasonStr(MeasurementEndReason r)
{
    switch (r)
    {
        case MEASUREMENT_END_FINGER_REMOVED: return "FINGER_REMOVED";
        case MEASUREMENT_END_USER_STOPPED:   return "USER_STOPPED";
        case MEASUREMENT_END_TIMEOUT:        return "TIMEOUT";
        case MEASUREMENT_END_SENSOR_ERROR:   return "SENSOR_ERROR";
        case MEASUREMENT_END_SIGNAL_LOST:    return "SIGNAL_LOST";
        default:                             return "UNKNOWN";
    }
}

/** Ghi tên filter kèm cửa sổ: "NONE" (RAW) hoặc "MOVING_AVERAGE_<N>". */
static void filterStr(char* dst, size_t size, PpgFilterMode mode, uint8_t maWindow)
{
    if (mode == PPG_FILTER_MOVING_AVERAGE)
    {
        (void)snprintf(dst, size, "MOVING_AVERAGE_%u", (unsigned)maWindow);
    }
    else
    {
        (void)snprintf(dst, size, "NONE");
    }
}

/** Ghi thời gian RTC hiện tại (cache) dạng ISO ngắn, hoặc "NO_RTC". */
static void rtcStr(char* dst, size_t size)
{
    DateTime dt;
    bool valid = false;
    RtcService_GetSnapshot(&dt, &valid);
    if (valid)
    {
        /* Chặn dải từng trường để bề rộng tối đa là hằng số biết trước
           (4+2+2+2+2+2 + dấu phân cách = 19 ký tự < size). Nếu không, compiler
           phải giả định %02u có thể in tới 10 chữ số -> cảnh báo truncation. */
        (void)snprintf(dst, size, "%04u-%02u-%02uT%02u:%02u:%02u",
                       (unsigned)(dt.year   % 10000U), (unsigned)(dt.month  % 100U),
                       (unsigned)(dt.day    % 100U),   (unsigned)(dt.hour   % 100U),
                       (unsigned)(dt.minute % 100U),   (unsigned)(dt.second % 100U));
    }
    else
    {
        (void)snprintf(dst, size, "NO_RTC");
    }
}

/* --------------------------------------------------------------------------
   Định dạng số thực bằng SỐ NGUYÊN CÓ TỈ LỆ (BUILD_AND_RELEASE §17).
   Build dùng -specs=nano.specs mà KHÔNG có -u _printf_float, nên newlib-nano
   ánh xạ vfprintf -> vfiprintf (bản chỉ số nguyên): "%f" sẽ in ra rác. Tự tách
   phần nguyên/phần thập phân vừa đúng, vừa tốn 0 byte flash thêm, vừa nhanh hơn
   float-printf (formatter chạy tới ~100 dòng/giây).
   -------------------------------------------------------------------------- */

/** Làm tròn về số nguyên gần nhất (nửa lẻ làm tròn ra xa 0). */
static long roundToLong(float v)
{
    return (long)(v + ((v >= 0.0F) ? 0.5F : -0.5F));
}

/** Ghi @p v với đúng 1 chữ số thập phân, ví dụ "74.2" hoặc "-0.3". */
static void fmt1(char* dst, size_t size, float v)
{
    const long scaled = roundToLong(v * 10.0F);
    long ip = scaled / 10L;
    long fp = scaled % 10L;
    if (fp < 0L)
    {
        fp = -fp;
    }
    if ((scaled < 0L) && (ip == 0L))
    {
        (void)snprintf(dst, size, "-0.%ld", fp);   /* giữ dấu âm cho (-1,0) */
    }
    else
    {
        (void)snprintf(dst, size, "%ld.%ld", ip, fp);
    }
}

/** Ghi float nếu hợp lệ, ngược lại để trống (ô CSV rỗng). */
static void fmtOptFloat(char* dst, size_t size, float v, bool valid)
{
    if (valid)
    {
        fmt1(dst, size, v);
    }
    else if (size > 0U)
    {
        dst[0] = '\0';
    }
}

/* -------------------------------------------------------------------------- */
/* API                                                                          */
/* -------------------------------------------------------------------------- */
size_t TelemetryFormatter_Header(char* buf, size_t bufSize)
{
    const int n = snprintf(buf, bufSize,
        "type,timestamp_ms,rtc,seq,red_raw,ir_raw,red_centered,ir_centered,"
        "red_filtered,ir_filtered,filter,ma_window,bpm,spo2,sqi,state\n");
    return (n > 0 && (size_t)n < bufSize) ? (size_t)n : 0U;
}

size_t TelemetryFormatter_Format(const TelemetryMessage* msg, char* buf, size_t bufSize)
{
    if ((msg == NULL) || (buf == NULL) || (bufSize == 0U))
    {
        return 0U;
    }

    char rtc[24];
    rtcStr(rtc, sizeof rtc);

    const unsigned long ts = (unsigned long)msg->timestampMs;
    int n = 0;

    switch (msg->type)
    {
        case TELEMETRY_MSG_PPG_SAMPLE:
        {
            const TelemetryPpgSample* p = &msg->payload.ppg;
            char filt[24];
            char bpmS[12];
            char spo2S[12];
            filterStr(filt, sizeof filt, p->filterMode, p->maWindow);
            fmtOptFloat(bpmS, sizeof bpmS, p->bpm, p->bpmValid);
            fmtOptFloat(spo2S, sizeof spo2S, p->spo2, p->spo2Valid);
            n = snprintf(buf, bufSize,
                "DATA,%lu,%s,%lu,%lu,%lu,%ld,%ld,%ld,%ld,%s,%u,%s,%s,%ld,%s\n",
                ts, rtc,
                (unsigned long)p->sequence,
                (unsigned long)p->redRaw, (unsigned long)p->irRaw,
                (long)p->redCentered, (long)p->irCentered,
                (long)p->redFiltered, (long)p->irFiltered,
                filt, (unsigned)p->maWindow,
                bpmS, spo2S, roundToLong(p->sqi), stateStr(p->state));
            break;
        }
        case TELEMETRY_MSG_VITAL_RESULT:
        {
            const TelemetryVitalResult* v = &msg->payload.vital;
            char bpmS[12];
            char abpmS[12];
            char spo2S[12];
            char aspo2S[12];
            fmtOptFloat(bpmS, sizeof bpmS, v->bpm, v->bpmValid);
            fmtOptFloat(abpmS, sizeof abpmS, v->averageBpm, v->bpmValid);
            fmtOptFloat(spo2S, sizeof spo2S, v->spo2, v->spo2Valid);
            fmtOptFloat(aspo2S, sizeof aspo2S, v->averageSpo2, v->spo2Valid);
            n = snprintf(buf, bufSize,
                "VITAL,%lu,%s,bpm=%s,avg_bpm=%s,spo2=%s,avg_spo2=%s,sqi=%ld,%s\n",
                ts, rtc, bpmS, abpmS, spo2S, aspo2S, roundToLong(v->sqi),
                stateStr(v->state));
            break;
        }
        case TELEMETRY_MSG_MEASUREMENT_STATE:
            n = snprintf(buf, bufSize, "STATE,%lu,%s,%s\n",
                ts, rtc, stateStr(msg->payload.measurementState));
            break;

        case TELEMETRY_MSG_SCREEN_CHANGED:
            n = snprintf(buf, bufSize, "EVENT,%lu,%s,SCREEN_CHANGED,%s,%s\n",
                ts, rtc, screenStr(msg->payload.screen.from),
                screenStr(msg->payload.screen.to));
            break;

        case TELEMETRY_MSG_USER_ACTION:
            n = snprintf(buf, bufSize, "EVENT,%lu,%s,USER_ACTION,%s,%s\n",
                ts, rtc, actionStr(msg->payload.action.id),
                screenStr(msg->payload.action.screen));
            break;

        case TELEMETRY_MSG_SETTING_CHANGED:
        {
            const TelemetrySettingEvent* s = &msg->payload.setting;
            /* Ngưỡng dùng floatValue; các cài đặt khác dùng intValue. */
            if ((s->id == TELEMETRY_SETTING_BPM_LOW_THRESHOLD) ||
                (s->id == TELEMETRY_SETTING_BPM_HIGH_THRESHOLD) ||
                (s->id == TELEMETRY_SETTING_SPO2_LOW_THRESHOLD))
            {
                char valS[16];
                fmt1(valS, sizeof valS, s->floatValue);
                n = snprintf(buf, bufSize, "EVENT,%lu,%s,SETTING_CHANGED,%s,%s\n",
                    ts, rtc, settingStr(s->id), valS);
            }
            else
            {
                n = snprintf(buf, bufSize, "EVENT,%lu,%s,SETTING_CHANGED,%s,%ld\n",
                    ts, rtc, settingStr(s->id), (long)s->intValue);
            }
            break;
        }
        case TELEMETRY_MSG_ALERT:
        {
            const TelemetryAlertEvent* a = &msg->payload.alert;
            char valS[16];
            char thrS[16];
            fmt1(valS, sizeof valS, a->value);
            fmt1(thrS, sizeof thrS, a->threshold);
            n = snprintf(buf, bufSize,
                "ALERT,%lu,%s,%s,VALUE=%s,THRESHOLD=%s,%s\n",
                ts, rtc, alertStr(a->flag), valS, thrS,
                a->active ? "ACTIVE" : "CLEARED");
            break;
        }
        case TELEMETRY_MSG_SESSION_START:
            n = snprintf(buf, bufSize, "SESSION_START,%lu,%s,%lu\n",
                ts, rtc, (unsigned long)msg->payload.session.sessionId);
            break;

        case TELEMETRY_MSG_SESSION_END:
        {
            const TelemetrySessionSummary* s = &msg->payload.session;
            char abpmS[16];
            char aspo2S[16];
            fmt1(abpmS, sizeof abpmS, s->averageBpm);
            fmt1(aspo2S, sizeof aspo2S, s->averageSpo2);
            n = snprintf(buf, bufSize,
                "SESSION_END,%lu,%s,%lu,AVG_BPM=%s,AVG_SPO2=%s,DURATION_MS=%lu,%s,%s\n",
                ts, rtc, (unsigned long)s->sessionId,
                abpmS, aspo2S,
                (unsigned long)s->durationMs, statusStr(s->status),
                reasonStr(s->endReason));
            break;
        }
        case TELEMETRY_MSG_HISTORY:
        {
            const TelemetrySessionSummary* s = &msg->payload.session;
            char abpmS[16];
            char aspo2S[16];
            fmt1(abpmS, sizeof abpmS, s->averageBpm);
            fmt1(aspo2S, sizeof aspo2S, s->averageSpo2);
            n = snprintf(buf, bufSize,
                "HISTORY,%lu,%lu,AVG_BPM=%s,AVG_SPO2=%s,DURATION_MS=%lu,%s,TEMPORARY\n",
                ts, (unsigned long)s->sessionId,
                abpmS, aspo2S,
                (unsigned long)s->durationMs, statusStr(s->status));
            break;
        }
        case TELEMETRY_MSG_SYSTEM:
            n = snprintf(buf, bufSize, "SYSTEM,%lu,%s,CODE=%lu\n",
                ts, rtc, (unsigned long)msg->payload.systemCode);
            break;

        default:
            return 0U;
    }

    /* snprintf trả về số byte "đáng lẽ" ghi; >= bufSize nghĩa là bị cắt. */
    if ((n > 0) && ((size_t)n < bufSize))
    {
        return (size_t)n;
    }
    return 0U;
}
