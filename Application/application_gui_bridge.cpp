/**
 * @file    application_gui_bridge.cpp
 * @brief   Cài đặt nguồn dữ liệu GUI thật (target).
 * @note    User-owned. Đọc kết quả engine (chạy trong DSP task) mỗi tick.
 */

#include "application_gui_bridge.hpp"

extern "C" {
#include "dsp_task.h"
#include "rtc_service.h"
#include "app_init.h"
#include "datetime.h"
#include "temporary_history_store.h"
#include "measurement_types.h"
#include "alert_config.h"
#include "telemetry_service.h"
#include "stm32f4xx_hal.h"
}

/** Kết quả được coi là stale sau khoảng thời gian này (ms). */
static constexpr uint32_t RESULT_STALE_TIMEOUT_MS = 60000U;  /* 60 giây */

namespace gui
{
namespace
{
MeasurementState mapState(PpgState s)
{
    switch (s)
    {
    case PPG_STATE_WAIT_FINGER:   return MeasurementState::WaitFinger;
    case PPG_STATE_STABILIZING:   return MeasurementState::Stabilizing;
    case PPG_STATE_MEASURING:     return MeasurementState::Measuring;
    case PPG_STATE_INVALID_SIGNAL:return MeasurementState::InvalidSignal;
    case PPG_STATE_SENSOR_ERROR:  return MeasurementState::SensorError;
    case PPG_STATE_RESULT_READY:  return MeasurementState::ResultReady;
    case PPG_STATE_IDLE:
    default:                      return MeasurementState::Idle;
    }
}

MeasurementInvalidReason mapReason(PpgInvalidReason r)
{
    switch (r)
    {
    case PPG_REASON_NO_FINGER:    return MeasurementInvalidReason::NoFinger;
    case PPG_REASON_UNSTABLE:     return MeasurementInvalidReason::MotionDetected;
    case PPG_REASON_SATURATION:   return MeasurementInvalidReason::Saturation;
    case PPG_REASON_WEAK_SIGNAL:  return MeasurementInvalidReason::WeakSignal;
    case PPG_REASON_SENSOR_ERROR: return MeasurementInvalidReason::SensorUnavailable;
    case PPG_REASON_NONE:
    default:                      return MeasurementInvalidReason::None;
    }
}

} // namespace

ApplicationGuiBridge::ApplicationGuiBridge()
    : ppg_(), generation_(1U),
      resultReadyMs_(0U), wasResultReady_(false)
{
}

void ApplicationGuiBridge::tick(uint32_t frameCounter)
{
    /* Vẫn tick mock cho các màn hình nó còn phục vụ (đồng hồ, settings). */
    mock_.tick(frameCounter);

    /* Engine chạy trong DSP thread; GUI tick chỉ đọc kết quả công bố mới nhất
       (không có DSP trong TouchGFX tick). */
    DspTask_GetResult(&ppg_);
}

void ApplicationGuiBridge::postCommand(const GuiCommand& command)
{
    if (command.type == GuiCommandType::SetDateTime)
    {
        DateTime dt;
        dt.year = command.year;
        dt.month = command.month;
        dt.day = command.day;
        dt.weekday = command.weekday;
        dt.hour = command.hour;
        dt.minute = command.minute;
        dt.second = command.second;
        RtcService_RequestSet(&dt);
        return;
    }
    if (command.type == GuiCommandType::SelectFilter)
    {
        PpgFilterMode mode;
        switch (command.filterMode)
        {
        case FilterMode::Raw:           mode = PPG_FILTER_RAW;            break;
        case FilterMode::MovingAverage: mode = PPG_FILTER_MOVING_AVERAGE; break;
        case FilterMode::Median:        mode = PPG_FILTER_MEDIAN;         break;
        case FilterMode::Lowpass:       mode = PPG_FILTER_LOWPASS;        break;
        case FilterMode::MedianLowpass: mode = PPG_FILTER_MEDIAN_LOWPASS; break;
        default:                        mode = PPG_FILTER_MOVING_AVERAGE; break;
        }
        DspTask_SetFilterMode(mode);
        /* Log thay đổi cài đặt (event — luôn được phép, kể cả trước khi ổn định). */
        TelemetrySettingEvent ev{};
        ev.id = TELEMETRY_SETTING_FILTER_MODE;
        ev.intValue = (int32_t)mode;
        (void)Telemetry_PublishSettingChange(&ev);
        return;
    }
    if (command.type == GuiCommandType::SetFilterWindow)
    {
        DspTask_SetMaWindow(command.filterWindow);
        TelemetrySettingEvent ev{};
        ev.id = TELEMETRY_SETTING_FILTER_WINDOW;
        ev.intValue = (int32_t)command.filterWindow;
        (void)Telemetry_PublishSettingChange(&ev);
        return;
    }
    /* Các command còn lại (đo, scenario test...) do mock xử lý. */
    mock_.postCommand(command);
}

void ApplicationGuiBridge::notifyScreenTransition()
{
    mock_.notifyScreenTransition();
}

bool ApplicationGuiBridge::getMeasurementSnapshot(GuiMeasurementSnapshot& snapshot)
{
    snapshot.generation = generation_++;

    snapshot.state = mapState(ppg_.state);
    snapshot.invalidReason = mapReason(ppg_.reason);
    snapshot.bpm = ppg_.bpm;
    snapshot.bpmValid = ppg_.bpmValid;
    snapshot.spo2Percent = ppg_.spo2;
    snapshot.spo2Valid = ppg_.spo2Valid;
    snapshot.sqiPercent = ppg_.sqiPercent;

    /* Stale detection: ghi lại thời điểm lần đầu thấy RESULT_READY.
       Nếu kết quả đã ngồi chờ quá lâu → đánh dấu stale. */
    if (ppg_.resultReady)
    {
        if (!wasResultReady_)
        {
            resultReadyMs_ = HAL_GetTick();
            wasResultReady_ = true;
        }
        const uint32_t elapsed = HAL_GetTick() - resultReadyMs_;
        snapshot.stale = (elapsed >= RESULT_STALE_TIMEOUT_MS);
    }
    else
    {
        wasResultReady_ = false;
        snapshot.stale = false;
    }

    snapshot.averageBpm = ppg_.averageBpm;
    snapshot.averageBpmValid = ppg_.averageBpmValid;
    snapshot.averageSpo2 = ppg_.averageSpo2;
    snapshot.averageSpo2Valid = ppg_.averageSpo2Valid;
    snapshot.elapsedMeasurementMs = ppg_.elapsedMeasurementMs;
    snapshot.validPeakCount = ppg_.validRrCount;
    snapshot.validSpo2WindowCount = ppg_.validSpo2Windows;
    snapshot.bpmMin = ppg_.bpmMin;
    snapshot.bpmMax = ppg_.bpmMax;
    snapshot.spo2Min = ppg_.spo2Min;
    snapshot.spo2Max = ppg_.spo2Max;
    snapshot.averageSqi = ppg_.averageSqi;
    snapshot.resultReady = ppg_.resultReady;
    snapshot.temporarilySaved = ppg_.resultSaved;

    snapshot.sensorStatus = (g_sensorOk != 0) ? SensorStatus::Ok : SensorStatus::Error;

    snapshot.fingerPresent = ppg_.fingerPresent;
    snapshot.signalStable = ppg_.signalStable;
    snapshot.waveformVisible = ppg_.waveformVisible;
    snapshot.stabilizationProgress = ppg_.stabilizationProgress;
    snapshot.redRaw = ppg_.redRaw;
    snapshot.irRaw = ppg_.irRaw;
    snapshot.redCentered = ppg_.redCentered;
    snapshot.irCentered = ppg_.irCentered;
    snapshot.acceptedPeakCount = ppg_.acceptedPeaks;
    snapshot.rejectedPeakCount = ppg_.rejectedPeaks;
    snapshot.droppedSampleCount = ppg_.droppedSamples;
    snapshot.fifoOverflowCount = ppg_.fifoOverflows;

    switch (ppg_.filterMode)
    {
    case PPG_FILTER_RAW:            snapshot.filterMode = FilterMode::Raw;            break;
    case PPG_FILTER_MOVING_AVERAGE: snapshot.filterMode = FilterMode::MovingAverage;  break;
    case PPG_FILTER_MEDIAN:         snapshot.filterMode = FilterMode::Median;         break;
    case PPG_FILTER_LOWPASS:        snapshot.filterMode = FilterMode::Lowpass;        break;
    case PPG_FILTER_MEDIAN_LOWPASS: snapshot.filterMode = FilterMode::MedianLowpass;  break;
    default:                        snapshot.filterMode = FilterMode::Raw;            break;
    }
    snapshot.maWindow = ppg_.maWindow;

    DateTime dt;
    bool valid = false;
    RtcService_GetSnapshot(&dt, &valid);
    snapshot.rtcValid = valid;
    snapshot.time.hour = dt.hour;
    snapshot.time.minute = dt.minute;
    snapshot.time.second = dt.second;
    snapshot.time.day = dt.day;
    snapshot.time.month = dt.month;
    snapshot.time.year = dt.year;
    return true;
}

bool ApplicationGuiBridge::getWaveformSnapshot(GuiWaveformSnapshot& snapshot)
{
    snapshot.generation = generation_++;
    snapshot.sampleRateHz = PPG_SAMPLE_RATE_HZ;
    snapshot.count = ppg_.waveformCount;
    snapshot.irChannelValid = (g_sensorOk != 0);
    snapshot.redChannelValid = (g_sensorOk != 0);
    snapshot.sensorStatus = (g_sensorOk != 0) ? SensorStatus::Ok : SensorStatus::Error;
    snapshot.droppedSamples = ppg_.droppedSamples;

    const uint16_t n = (ppg_.waveformCount <= WAVEFORM_POINTS) ? ppg_.waveformCount
                                                              : WAVEFORM_POINTS;
    for (uint16_t i = 0U; i < n; ++i)
    {
        snapshot.irSamples[i] = ppg_.waveform[i];
    }
    for (uint16_t i = n; i < WAVEFORM_POINTS; ++i)
    {
        snapshot.irSamples[i] = 0;
    }

    const uint16_t nRed = (ppg_.redWaveformCount <= WAVEFORM_POINTS) ? ppg_.redWaveformCount
                                                                    : WAVEFORM_POINTS;
    for (uint16_t i = 0U; i < nRed; ++i)
    {
        snapshot.redSamples[i] = ppg_.redWaveform[i];
    }
    for (uint16_t i = nRed; i < WAVEFORM_POINTS; ++i)
    {
        snapshot.redSamples[i] = 0;
    }

    snapshot.peakCount = (ppg_.peakCount <= WAVEFORM_MAX_PEAKS) ? ppg_.peakCount
                                                              : WAVEFORM_MAX_PEAKS;
    for (uint8_t i = 0U; i < snapshot.peakCount; ++i)
    {
        snapshot.peakIndices[i] = ppg_.peakIndices[i];
    }
    return true;
}

bool ApplicationGuiBridge::getHistoryPage(uint16_t pageIndex, GuiHistoryPageSnapshot& s)
{
    s.generation = generation_++;
    s.pageIndex = pageIndex;

    const size_t perPage = HISTORY_RECORDS_PER_PAGE;
    size_t count = 0U;

    DspTask_HistoryLock();
    const size_t total = TemporaryHistory_GetCount();
    for (size_t j = 0U; j < perPage; ++j)
    {
        MeasurementHistoryRecord rec;
        if (TemporaryHistory_GetByNewestIndex((static_cast<size_t>(pageIndex) * perPage) + j, &rec)
            != HISTORY_STATUS_OK)
        {
            break;
        }
        GuiHistoryRecord& g = s.records[count];
        g.time.hour = rec.startDateTime.hour;
        g.time.minute = rec.startDateTime.minute;
        g.time.second = rec.startDateTime.second;
        g.time.day = rec.startDateTime.day;
        g.time.month = rec.startDateTime.month;
        g.time.year = rec.startDateTime.year;
        g.bpm = rec.averageBpm;
        g.spo2Percent = rec.averageSpo2;
        g.sqiPercent = rec.averageSqi;
        g.valid = (rec.status != MEASUREMENT_RESULT_INVALID);
        g.invalidReason = MeasurementInvalidReason::None;
        ++count;
    }
    DspTask_HistoryUnlock();

    s.recordCount = static_cast<uint8_t>(count);
    s.totalRecords = static_cast<uint16_t>(total);
    s.pageCount = (total == 0U) ? 0U
        : static_cast<uint16_t>((total + perPage - 1U) / perPage);
    s.status = (total == 0U) ? HistoryPageStatus::Empty : HistoryPageStatus::Ok;
    return true;
}

} // namespace gui
