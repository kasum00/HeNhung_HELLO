/**
 * @file    ppg_measurement.c
 * @brief   Cài đặt engine đo PPG (không HAL, không cấp phát).
 * @note    User-owned. Chỉ căn giữa DC + peak/BPM đơn giản (không lọc DSP).
 *          Dữ liệu RAW được giữ nguyên; dữ liệu centered là riêng.
 */

#include "ppg_measurement.h"
#include "moving_average_filter.h"
#include "median_filter.h"
#include "lowpass_filter.h"
#include "spo2_estimator.h"

/* -------------------------------------------------------------------------- */
/* Trạng thái engine                                                           */
/* -------------------------------------------------------------------------- */
static PpgState s_state;
static PpgInvalidReason s_reason;
static bool s_sensorError;

/* Phát hiện ngón tay. */
static uint32_t s_irDc;           /* bộ bám DC IR chậm                */
static bool s_irDcInit;
static bool s_fingerPresent;
static uint16_t s_onCount;
static uint16_t s_offCount;

/* Baseline / căn giữa. */
static int32_t s_baselineIr;
static int32_t s_baselineRed;
static int32_t s_lastCenteredIr;
static int32_t s_lastCenteredRed;
static uint32_t s_lastRedRaw;
static uint32_t s_lastIrRaw;

/* Moving average của tín hiệu centered: IR đã làm mượt (khi được chọn) điều khiển
   phát hiện peak, BPM và waveform; RED đã làm mượt giữ lại để hiển thị RED. Đây
   chỉ là bước làm mượt, KHÔNG phải tín hiệu lọc hoàn chỉnh/y tế. Buffer được định
   cỡ theo cửa sổ tối đa để đổi N lúc chạy. */
static int32_t s_maBufIr[PPG_MA_WINDOW_MAX];
static int32_t s_maBufRed[PPG_MA_WINDOW_MAX];
static MovingAverageFilter s_maIr;
static MovingAverageFilter s_maRed;
static int32_t s_lastIrFiltered;
static int32_t s_lastRedFiltered;

/* Median filter (RED + IR) */
static int32_t s_medianBufIr[PPG_MA_WINDOW_MAX];
static int32_t s_medianSortIr[PPG_MA_WINDOW_MAX];
static MedianFilter s_medianIr;

static int32_t s_medianBufRed[PPG_MA_WINDOW_MAX];
static int32_t s_medianSortRed[PPG_MA_WINDOW_MAX];
static MedianFilter s_medianRed;

/* Low-pass filter (RED + IR) */
static LowpassFilter s_lpIr;
static LowpassFilter s_lpRed;

/* Nguồn tín hiệu phân tích/hiển thị chọn được (toàn cục) + cỡ cửa sổ đang dùng. */
static PpgFilterMode s_filterMode;
static uint8_t s_maWindowN;

/* Envelope (cho biên độ + ngưỡng peak). */
static int32_t s_envMax;
static int32_t s_envMin;
static int32_t s_displayRange;

/* Ổn định tín hiệu. */
static uint32_t s_stateStartMs;
static uint32_t s_samplesInState;

/* Ring waveform IR (đã ánh xạ 0..full-scale). */
static int16_t s_wave[PPG_WAVE_POINTS];
static uint8_t s_peakFlag[PPG_WAVE_POINTS];
static uint16_t s_waveHead;   /* vị trí ghi tiếp */
static uint16_t s_waveFill;   /* số điểm hợp lệ (<= POINTS) */

/* Ring waveform RED (dùng chung envelope + auto-range với IR). */
static int16_t s_waveRed[PPG_WAVE_POINTS];
static uint16_t s_waveRedHead;
static uint16_t s_waveRedFill;

/* Peak detector. */
static int32_t s_prevFilteredIr;
static bool s_prevValid;
static bool s_rising;
static uint32_t s_lastPeakMs;
static bool s_havePeak;

/* BPM. */
static uint16_t s_intervals[PPG_BPM_INTERVAL_COUNT];
static uint8_t s_intervalCount;
static uint8_t s_intervalHead;
static float s_bpm;
static bool s_bpmValid;

/* Bộ tích lũy theo phiên (từ finger-on tới finalize). Phiên trải suốt một lần
   chạm ngón tay và sống sót qua các lần MEASURING<->STABILIZING ngắn; chỉ một lần
   finger-off được xác nhận mới kết thúc. BPM trung bình lấy từ các RR interval đã
   chấp nhận (§13), không phải trung bình các giá trị BPM theo frame. */
static bool s_sessionActive;         /* true từ finger-on tới khi reset        */
static bool s_measuringStarted;      /* đã thấy lần vào MEASURING đầu tiên     */
static uint32_t s_sessionStartMs;    /* timestamp của sample MEASURING đầu tiên */
static uint32_t s_sessionLastMs;     /* timestamp sample mới nhất              */
static uint32_t s_sessionRrSumMs;    /* tổng các RR interval đã chấp nhận      */
static uint16_t s_sessionRrCount;    /* số RR interval đã chấp nhận            */
static float s_sessionBpmMin;
static float s_sessionBpmMax;

/* SpO2 (tính trên RAW RED/IR) + chất lượng tín hiệu. */
static Spo2Estimator s_spo2;
static float s_latestSpo2;
static bool s_latestSpo2Valid;
static float s_sqiPercent;
static float s_lastRatio;
static float s_lastDcRed;
static float s_lastDcIr;
static float s_lastAcRed;
static float s_lastAcIr;
static float s_sessionSpo2Sum;
static uint16_t s_sessionSpo2Count;
static float s_sessionSpo2Min;
static float s_sessionSpo2Max;
static float s_sessionSqiSum;
static uint32_t s_sessionSqiCount;

/* Kết quả đã chốt (khi nhấc ngón tay): đóng băng cho tới khi ngón tay mới bắt đầu phiên. */
static bool s_resultReady;
static MeasurementResultStatus s_resultStatus;
static MeasurementEndReason s_endReason;

/* Chẩn đoán. */
static uint32_t s_acceptedPeaks;
static uint32_t s_rejectedPeaks;
static uint32_t s_droppedSamples;
static uint32_t s_fifoOverflows;

/* -------------------------------------------------------------------------- */
/* Hàm hỗ trợ                                                                  */
/* -------------------------------------------------------------------------- */
static int32_t clamp32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

/** Tín hiệu IR mà peak/BPM/waveform chạy trên đó, theo filter mode đã chọn. */
static int32_t analysisIr(void)
{
    return (s_filterMode == PPG_FILTER_RAW) ? s_lastCenteredIr : s_lastIrFiltered;
}

/** Tín hiệu RED dùng cho waveform display, theo filter mode đã chọn. */
static int32_t analysisRed(void)
{
    return (s_filterMode == PPG_FILTER_RAW) ? s_lastCenteredRed : s_lastRedFiltered;
}

static void resetPeakDetector(void)
{
    s_prevValid = false;
    s_rising = false;
    s_havePeak = false;
    s_intervalCount = 0U;
    s_intervalHead = 0U;
    s_bpm = 0.0F;
    s_bpmValid = false;
    s_acceptedPeaks = 0U;
    s_rejectedPeaks = 0U;
}

/** Bắt đầu một phiên đo mới (gọi một lần khi finger-on). */
static void resetSession(void)
{
    s_sessionActive = true;
    s_measuringStarted = false;
    s_sessionStartMs = 0U;
    s_sessionLastMs = 0U;
    s_sessionRrSumMs = 0U;
    s_sessionRrCount = 0U;
    s_sessionBpmMin = 999.0F;   /* lớn hơn BPM thực để min/max đúng từ peak đầu */
    s_sessionBpmMax = 0.0F;
    s_sessionSpo2Sum = 0.0F;
    s_sessionSpo2Count = 0U;
    s_sessionSpo2Min = 101.0F;  /* lớn hơn SpO2 thực để min/max đúng từ cửa sổ đầu */
    s_sessionSpo2Max = 0.0F;
    s_sessionSqiSum = 0.0F;
    s_sessionSqiCount = 0U;
    s_resultReady = false;
    s_resultStatus = MEASUREMENT_RESULT_INVALID;
    s_endReason = MEASUREMENT_END_FINGER_REMOVED;
}

/** Đóng băng và phân loại phiên khi finger-off (§26). */
static void finalizeSession(MeasurementEndReason reason)
{
    const uint32_t elapsed = (s_measuringStarted && (s_sessionLastMs >= s_sessionStartMs))
                                 ? (s_sessionLastMs - s_sessionStartMs) : 0U;
    const bool durationOk = (elapsed >= MEASUREMENT_MIN_DURATION_MS);
    const bool rrOk = (s_sessionRrCount >= MEASUREMENT_MIN_RR_INTERVALS);
    const bool spo2Ok = (s_sessionSpo2Count >= MEASUREMENT_MIN_SPO2_WINDOWS);

    if (durationOk && rrOk && spo2Ok) { s_resultStatus = MEASUREMENT_RESULT_VALID; }
    else if (durationOk && rrOk)      { s_resultStatus = MEASUREMENT_RESULT_PARTIAL; }
    else                              { s_resultStatus = MEASUREMENT_RESULT_INVALID; }

    s_endReason = reason;
    s_resultReady = true;   /* đóng băng; KHÔNG xóa accumulator ở đây (§24) */
}

/** Xóa toàn bộ trạng thái per-measurement (nhấc ngón tay, hoặc ổn định lại). */
static void resetMeasurement(uint32_t seedIr, uint32_t seedRed, uint32_t nowMs)
{
    s_baselineIr = (int32_t)seedIr;
    s_baselineRed = (int32_t)seedRed;
    s_lastCenteredIr = 0;
    s_lastCenteredRed = 0;
    MovingAverage_Reset(&s_maIr);
    MovingAverage_Reset(&s_maRed);
    Median_Reset(&s_medianIr);
    Median_Reset(&s_medianRed);
    Lowpass_Reset(&s_lpIr);
    Lowpass_Reset(&s_lpRed);
    s_lastIrFiltered = 0;
    s_lastRedFiltered = 0;
    Spo2_Reset(&s_spo2);
    s_latestSpo2 = 0.0F;
    s_latestSpo2Valid = false;
    s_sqiPercent = 0.0F;
    s_lastRatio = 0.0F;
    s_lastDcRed = 0.0F;
    s_lastDcIr = 0.0F;
    s_lastAcRed = 0.0F;
    s_lastAcIr = 0.0F;
    s_envMax = 0;
    s_envMin = 0;
    s_displayRange = PPG_DISPLAY_RANGE_MIN;
    s_waveHead = 0U;
    s_waveFill = 0U;
    s_waveRedHead = 0U;
    s_waveRedFill = 0U;
    for (uint16_t i = 0U; i < PPG_WAVE_POINTS; ++i)
    {
        s_wave[i] = PPG_WAVE_ZERO;
        s_waveRed[i] = PPG_WAVE_ZERO;
        s_peakFlag[i] = 0U;
    }
    resetPeakDetector();
    s_stateStartMs = nowMs;
    s_samplesInState = 0U;
}

static void updateEnvelope(int32_t centered)
{
    if (centered > s_envMax) { s_envMax = centered; }
    else { s_envMax -= (s_envMax - centered) >> 7; }
    if (centered < s_envMin) { s_envMin = centered; }
    else { s_envMin += (centered - s_envMin) >> 7; }
}

static void pushWaveformPoint(int32_t centered, uint8_t isPeak)
{
    /* Auto-range để cả đỉnh và đáy đều lọt: căn đồ thị theo điểm giữa tín hiệu và
       co nửa-biên-độ về DEN/NUM của nửa chiều cao (chừa lề hai bên). */
    const int32_t mid = (s_envMax + s_envMin) / 2;
    const int32_t halfAmp = (s_envMax - s_envMin) / 2;
    const int32_t target = clamp32((halfAmp * PPG_DISPLAY_MARGIN_NUM) / PPG_DISPLAY_MARGIN_DEN,
                                   PPG_DISPLAY_RANGE_MIN, PPG_DISPLAY_RANGE_MAX);
    s_displayRange += (target - s_displayRange) >> 4;     /* đổi thang chậm */
    if (s_displayRange < PPG_DISPLAY_RANGE_MIN) { s_displayRange = PPG_DISPLAY_RANGE_MIN; }

    const int32_t span = (PPG_WAVE_FULL_SCALE / 2) - 50;  /* +/- quanh đường zero */
    int32_t mapped = PPG_WAVE_ZERO + ((centered - mid) * span) / s_displayRange;
    mapped = clamp32(mapped, PPG_WAVE_ZERO - span, PPG_WAVE_ZERO + span);

    s_wave[s_waveHead] = (int16_t)mapped;
    s_peakFlag[s_waveHead] = isPeak;
    s_waveHead = (uint16_t)((s_waveHead + 1U) % PPG_WAVE_POINTS);
    if (s_waveFill < PPG_WAVE_POINTS) { ++s_waveFill; }

    /* RED waveform: dùng chung auto-range envelope với IR. */
    const int32_t redCentered = analysisRed();
    int32_t redMapped = PPG_WAVE_ZERO + ((redCentered - mid) * span) / s_displayRange;
    redMapped = clamp32(redMapped, PPG_WAVE_ZERO - span, PPG_WAVE_ZERO + span);
    s_waveRed[s_waveRedHead] = (int16_t)redMapped;
    s_waveRedHead = (uint16_t)((s_waveRedHead + 1U) % PPG_WAVE_POINTS);
    if (s_waveRedFill < PPG_WAVE_POINTS) { ++s_waveRedFill; }
}

/** Median của các interval đã ghi (copy nhỏ + insertion sort). */
static uint16_t medianInterval(void)
{
    uint16_t tmp[PPG_BPM_INTERVAL_COUNT];
    for (uint8_t i = 0U; i < s_intervalCount; ++i)
    {
        tmp[i] = s_intervals[i];
    }
    for (uint8_t i = 1U; i < s_intervalCount; ++i)
    {
        const uint16_t key = tmp[i];
        int8_t j = (int8_t)i - 1;
        while ((j >= 0) && (tmp[j] > key)) { tmp[j + 1] = tmp[j]; --j; }
        tmp[j + 1] = key;
    }
    return tmp[s_intervalCount / 2U];
}

static void addInterval(uint16_t intervalMs)
{
    /* Interval đã chấp nhận (đã nằm trong giới hạn sinh lý): nạp cho cả buffer
       median BPM tức thời và bộ tích lũy trung bình phiên (§13). */
    s_sessionRrSumMs += intervalMs;
    ++s_sessionRrCount;
    const float ibpm = 60000.0F / (float)intervalMs;
    if (s_sessionRrCount == 1U) { s_sessionBpmMin = ibpm; s_sessionBpmMax = ibpm; }
    else
    {
        if (ibpm < s_sessionBpmMin) { s_sessionBpmMin = ibpm; }
        if (ibpm > s_sessionBpmMax) { s_sessionBpmMax = ibpm; }
    }

    s_intervals[s_intervalHead] = intervalMs;
    s_intervalHead = (uint8_t)((s_intervalHead + 1U) % PPG_BPM_INTERVAL_COUNT);
    if (s_intervalCount < PPG_BPM_INTERVAL_COUNT) { ++s_intervalCount; }

    if (s_intervalCount >= PPG_BPM_MIN_INTERVALS)
    {
        const uint16_t med = medianInterval();
        if (med > 0U)
        {
            const float bpm = 60000.0F / (float)med;
            if ((bpm >= (float)PPG_BPM_MIN) && (bpm <= (float)PPG_BPM_MAX))
            {
                s_bpm = bpm;
                s_bpmValid = true;
                return;
            }
        }
    }
    s_bpmValid = false;
}

/** Peak detector streaming trên tín hiệu IR đã centered. */
static void runPeakDetector(int32_t centered, uint32_t nowMs)
{
    uint8_t markPeak = 0U;

    if (s_prevValid)
    {
        const bool risingNow = (centered > s_prevFilteredIr);
        if (s_rising && !risingNow)
        {
            /* Cực đại cục bộ tại sample trước đó. */
            const int32_t amplitude = s_envMax - s_envMin;
            const int32_t threshold = s_envMin + (amplitude / 2);
            const bool strong = (amplitude >= PPG_MIN_AC_AMPLITUDE);
            const bool aboveThr = (s_prevFilteredIr >= (threshold + PPG_PEAK_PROMINENCE));

            if (strong && aboveThr)
            {
                if (!s_havePeak)
                {
                    s_havePeak = true;
                    s_lastPeakMs = nowMs;           /* peak đầu tiên: chưa có interval */
                    markPeak = 1U;
                }
                else
                {
                    const uint32_t interval = nowMs - s_lastPeakMs;
                    if (interval < PPG_PEAK_MIN_INTERVAL_MS)
                    {
                        ++s_rejectedPeaks;           /* quá gần (double peak) */
                    }
                    else
                    {
                        s_lastPeakMs = nowMs;
                        markPeak = 1U;
                        if (interval <= PPG_PEAK_MAX_INTERVAL_MS)
                        {
                            ++s_acceptedPeaks;
                            addInterval((uint16_t)interval);
                        }
                        else
                        {
                            ++s_rejectedPeaks;       /* quá xa -> kết quả không hợp lệ */
                            s_bpmValid = false;
                            s_intervalCount = 0U;    /* làm lại lịch sử interval */
                        }
                    }
                }
            }
        }
        s_rising = risingNow;
    }

    /* Đánh dấu peak trên sample vừa lưu (head trước đó). */
    if (markPeak != 0U)
    {
        const uint16_t idx = (uint16_t)((s_waveHead + PPG_WAVE_POINTS - 1U) % PPG_WAVE_POINTS);
        s_peakFlag[idx] = 1U;
    }

    s_prevFilteredIr = centered;
    s_prevValid = true;
}

/* -------------------------------------------------------------------------- */
/* API công khai                                                               */
/* -------------------------------------------------------------------------- */
void Ppg_Init(void)
{
    s_state = PPG_STATE_WAIT_FINGER;
    s_reason = PPG_REASON_NO_FINGER;
    s_sensorError = false;
    s_irDc = 0U;
    s_irDcInit = false;
    s_fingerPresent = false;
    s_onCount = 0U;
    s_offCount = 0U;
    s_lastRedRaw = 0U;
    s_lastIrRaw = 0U;
    s_droppedSamples = 0U;
    s_fifoOverflows = 0U;
    s_sessionActive = false;
    s_filterMode = PPG_FILTER_MOVING_AVERAGE;   /* mặc định làm mượt */
    s_maWindowN = (uint8_t)PPG_MOVING_AVERAGE_WINDOW;
    (void)MovingAverage_Init(&s_maIr, s_maBufIr, s_maWindowN);
    (void)MovingAverage_Init(&s_maRed, s_maBufRed, s_maWindowN);

    /* Median filter init */
    Median_Init(&s_medianIr, s_medianBufIr, s_medianSortIr, s_maWindowN);
    Median_Init(&s_medianRed, s_medianBufRed, s_medianSortRed, s_maWindowN);

    /* Low-pass filter init */
    Lowpass_Init(&s_lpIr);
    Lowpass_Init(&s_lpRed);

    Spo2_Init(&s_spo2, NULL);   /* calibration thực nghiệm mặc định */
    resetSession();
    s_sessionActive = false;
    resetMeasurement(0U, 0U, 0U);
}

void Ppg_SetFilterMode(PpgFilterMode mode)
{
    s_filterMode = mode;
}

void Ppg_SetMaWindow(uint8_t window)
{
    uint8_t n = window;
    if (n < 1U) { n = 1U; }
    if (n > (uint8_t)PPG_MA_WINDOW_MAX) { n = (uint8_t)PPG_MA_WINDOW_MAX; }
    s_maWindowN = n;
    (void)MovingAverage_Init(&s_maIr, s_maBufIr, n);   /* khởi tạo lại (xóa) */
    (void)MovingAverage_Init(&s_maRed, s_maBufRed, n);
    Median_Reset(&s_medianIr);
    Median_Reset(&s_medianRed);
    Lowpass_Reset(&s_lpIr);
    Lowpass_Reset(&s_lpRed);
}

void Ppg_SetSensorError(bool error)
{
    s_sensorError = error;
    if (error)
    {
        s_state = PPG_STATE_SENSOR_ERROR;
        s_reason = PPG_REASON_SENSOR_ERROR;
        s_fingerPresent = false;
        s_bpmValid = false;
    }
    else if (s_state == PPG_STATE_SENSOR_ERROR)
    {
        s_state = PPG_STATE_WAIT_FINGER;
        s_reason = PPG_REASON_NO_FINGER;
    }
}

void Ppg_ReportLoss(uint32_t droppedDelta, uint32_t overflowDelta)
{
    s_droppedSamples += droppedDelta;
    s_fifoOverflows += overflowDelta;
}

void Ppg_PushSample(const PpgRawSample* sample)
{
    if ((sample == 0) || s_sensorError)
    {
        return;
    }

    const uint32_t ir = sample->irRaw;
    const uint32_t red = sample->redRaw;
    const uint32_t nowMs = sample->timestampMs;
    s_lastIrRaw = ir;
    s_lastRedRaw = red;

    /* Bộ bám DC IR chậm phục vụ phát hiện ngón tay. */
    if (!s_irDcInit) { s_irDc = ir; s_irDcInit = true; }
    else { s_irDc = (uint32_t)((int32_t)s_irDc + (((int32_t)ir - (int32_t)s_irDc) >> 6)); }

    /* Phát hiện ngón tay với hysteresis + xác nhận liên tiếp. */
    if (!s_fingerPresent)
    {
        if (s_irDc > PPG_FINGER_ON_THRESHOLD) { ++s_onCount; } else { s_onCount = 0U; }
        if (s_onCount >= PPG_FINGER_ON_SAMPLES)
        {
            s_fingerPresent = true;
            s_offCount = 0U;
            s_state = PPG_STATE_STABILIZING;
            s_reason = PPG_REASON_NONE;
            resetMeasurement(ir, red, nowMs);
            resetSession();               /* chạm ngón tay mới = phiên mới */
        }
    }
    else
    {
        if (s_irDc < PPG_FINGER_OFF_THRESHOLD) { ++s_offCount; } else { s_offCount = 0U; }
        if (s_offCount >= PPG_FINGER_OFF_SAMPLES)
        {
            s_fingerPresent = false;
            s_onCount = 0U;
            if (s_measuringStarted)
            {
                /* Đã có một phép đo thực sự: đóng băng + finalize (§24). Giữ ở
                   RESULT_READY tới khi ngón tay mới bắt đầu phiên mới; KHÔNG xóa
                   accumulator (DSP task xây bản ghi trước). */
                finalizeSession(MEASUREMENT_END_FINGER_REMOVED);
                s_state = PPG_STATE_RESULT_READY;
                s_reason = PPG_REASON_NONE;
            }
            else
            {
                /* Chỉ chạm thoáng qua, chưa từng đo: bỏ qua âm thầm. */
                s_state = PPG_STATE_WAIT_FINGER;
                s_reason = PPG_REASON_NO_FINGER;
                resetMeasurement(ir, red, nowMs);
                resetSession();
                s_sessionActive = false;
            }
        }
    }

    if (!s_fingerPresent)
    {
        return; /* WAIT_FINGER: không có gì để hiển thị hay đo */
    }

    ++s_samplesInState;
    s_sessionLastMs = nowMs;

    const bool saturated = (ir > PPG_SATURATION_LEVEL);
    const bool dcOk = (s_irDc > PPG_DC_MIN);

    if ((s_state == PPG_STATE_STABILIZING) || (s_state == PPG_STATE_INVALID_SIGNAL))
    {
        /* Dựng baseline nhanh hơn khi đang ổn định. Chia nguyên (không phải dịch
           bit) để EMA đối xứng và không làm trôi tâm khỏi zero. */
        s_baselineIr += ((int32_t)ir - s_baselineIr) / 16;
        s_baselineRed += ((int32_t)red - s_baselineRed) / 16;
        s_lastCenteredIr = (int32_t)ir - s_baselineIr;
        s_lastCenteredRed = (int32_t)red - s_baselineRed;

        /* Lọc tín hiệu IR/RED theo chế độ đã chọn */
        switch (s_filterMode)
        {
        case PPG_FILTER_MOVING_AVERAGE:
            (void)MovingAverage_Process(&s_maIr,  s_lastCenteredIr,  &s_lastIrFiltered);
            (void)MovingAverage_Process(&s_maRed, s_lastCenteredRed, &s_lastRedFiltered);
            break;
        case PPG_FILTER_MEDIAN:
            s_lastIrFiltered  = Median_Process(&s_medianIr,  s_lastCenteredIr);
            s_lastRedFiltered = Median_Process(&s_medianRed, s_lastCenteredRed);
            break;
        case PPG_FILTER_LOWPASS:
            s_lastIrFiltered  = Lowpass_Process(&s_lpIr,  s_lastCenteredIr);
            s_lastRedFiltered = Lowpass_Process(&s_lpRed, s_lastCenteredRed);
            break;
        case PPG_FILTER_MEDIAN_LOWPASS:
            s_lastIrFiltered  = Lowpass_Process(&s_lpIr,
                                    Median_Process(&s_medianIr, s_lastCenteredIr));
            s_lastRedFiltered = Lowpass_Process(&s_lpRed,
                                    Median_Process(&s_medianRed, s_lastCenteredRed));
            break;
        case PPG_FILTER_RAW:
        default:
            s_lastIrFiltered  = s_lastCenteredIr;
            s_lastRedFiltered = s_lastCenteredRed;
            break;
        }
        updateEnvelope(analysisIr());

        const int32_t amplitude = s_envMax - s_envMin;
        const bool ampOk = (amplitude >= PPG_MIN_AC_AMPLITUDE);
        const uint32_t elapsed = nowMs - s_stateStartMs;
        const bool enoughSamples = (s_samplesInState >= (PPG_WAVE_POINTS / 2U));

        if (saturated) { s_reason = PPG_REASON_SATURATION; }
        else if (!dcOk) { s_reason = PPG_REASON_WEAK_SIGNAL; }
        else { s_reason = PPG_REASON_NONE; }

        if ((elapsed >= PPG_STABILIZE_MIN_MS) && dcOk && ampOk && !saturated && enoughSamples)
        {
            s_state = PPG_STATE_MEASURING;
            s_reason = PPG_REASON_NONE;
            resetPeakDetector();
            if (!s_measuringStarted)
            {
                s_measuringStarted = true;
                s_sessionStartMs = nowMs;   /* đồng hồ đo của phiên bắt đầu */
            }
            /* Seed thang hiển thị từ envelope đã ổn định để những frame đo đầu
               tiên đã đúng cỡ ngay (không bị cắt lúc đầu). */
            s_displayRange = clamp32(((amplitude / 2) * PPG_DISPLAY_MARGIN_NUM) / PPG_DISPLAY_MARGIN_DEN,
                                     PPG_DISPLAY_RANGE_MIN, PPG_DISPLAY_RANGE_MAX);
        }
        /* Không xuất waveform khi đang ổn định. */
    }
    else if (s_state == PPG_STATE_MEASURING)
    {
        /* Baseline thích nghi (chậm) — chỉ bù trôi. Chia đối xứng. */
        s_baselineIr += ((int32_t)ir - s_baselineIr) / (1 << PPG_BASELINE_SHIFT);
        s_baselineRed += ((int32_t)red - s_baselineRed) / (1 << PPG_BASELINE_SHIFT);
        s_lastCenteredIr = (int32_t)ir - s_baselineIr;
        s_lastCenteredRed = (int32_t)red - s_baselineRed;

        /* Lọc tín hiệu IR/RED theo chế độ đã chọn */
        switch (s_filterMode)
        {
        case PPG_FILTER_MOVING_AVERAGE:
            (void)MovingAverage_Process(&s_maIr,  s_lastCenteredIr,  &s_lastIrFiltered);
            (void)MovingAverage_Process(&s_maRed, s_lastCenteredRed, &s_lastRedFiltered);
            break;
        case PPG_FILTER_MEDIAN:
            s_lastIrFiltered  = Median_Process(&s_medianIr,  s_lastCenteredIr);
            s_lastRedFiltered = Median_Process(&s_medianRed, s_lastCenteredRed);
            break;
        case PPG_FILTER_LOWPASS:
            s_lastIrFiltered  = Lowpass_Process(&s_lpIr,  s_lastCenteredIr);
            s_lastRedFiltered = Lowpass_Process(&s_lpRed, s_lastCenteredRed);
            break;
        case PPG_FILTER_MEDIAN_LOWPASS:
            s_lastIrFiltered  = Lowpass_Process(&s_lpIr,
                                    Median_Process(&s_medianIr, s_lastCenteredIr));
            s_lastRedFiltered = Lowpass_Process(&s_lpRed,
                                    Median_Process(&s_medianRed, s_lastCenteredRed));
            break;
        case PPG_FILTER_RAW:
        default:
            s_lastIrFiltered  = s_lastCenteredIr;
            s_lastRedFiltered = s_lastCenteredRed;
            break;
        }
        updateEnvelope(analysisIr());

        const int32_t amplitude = s_envMax - s_envMin;
        const bool ampOk = (amplitude >= PPG_MIN_AC_AMPLITUDE);

        if (saturated || !dcOk || !ampOk)
        {
            /* Suy giảm: ngừng hiển thị/đếm, ổn định lại. Bỏ cửa sổ SpO2 (nếu
               không sẽ bắc qua chỗ gián đoạn). */
            s_state = PPG_STATE_STABILIZING;
            s_reason = saturated ? PPG_REASON_SATURATION
                                 : (!ampOk ? PPG_REASON_WEAK_SIGNAL : PPG_REASON_UNSTABLE);
            s_bpmValid = false;
            s_latestSpo2Valid = false;
            Spo2_Reset(&s_spo2);
            s_stateStartMs = nowMs;
            s_samplesInState = 0U;
        }
        else
        {
            s_reason = PPG_REASON_NONE;
            /* Peak/BPM/waveform chạy trên tín hiệu IR đã chọn (§11). Ở mode moving
               average thì chờ cửa sổ đầy (luôn đầy khi tới MEASURING); raw, median
               và lowpass luôn sẵn sàng. */
            const bool sigReady = (s_filterMode == PPG_FILTER_RAW) ||
                                  (s_filterMode == PPG_FILTER_MEDIAN) ||
                                  (s_filterMode == PPG_FILTER_LOWPASS) ||
                                  (s_filterMode == PPG_FILTER_MEDIAN_LOWPASS) ||
                                  MovingAverage_IsReady(&s_maIr);
            if (sigReady)
            {
                const int32_t sig = analysisIr();
                pushWaveformPoint(sig, 0U);
                runPeakDetector(sig, nowMs);
            }

            /* SQI: heuristic chất lượng hướng peak/BPM (tỉ lệ peak được chấp nhận).
               Chỉ hiển thị Dashboard + đánh giá nhịp; KHÔNG dùng làm điều kiện hợp
               lệ của SpO2 (SpO2 dùng chất lượng RED/IR riêng trong estimator). */
            const uint32_t totalPeaks = s_acceptedPeaks + s_rejectedPeaks;
            s_sqiPercent = (totalPeaks > 0U)
                ? (100.0F * (float)s_acceptedPeaks / (float)totalPeaks) : 0.0F;
            s_sessionSqiSum += s_sqiPercent;
            ++s_sessionSqiCount;

            /* SpO2 trên RAW RED/IR (độc lập với filter mode hiển thị). */
            Spo2Result sp;
            const Spo2Status sst = Spo2_Process(&s_spo2, red, ir, nowMs, &sp);
            if (sst != SPO2_STATUS_NOT_READY)
            {
                s_lastRatio = sp.ratio;
                s_lastDcRed = sp.dcRed;
                s_lastDcIr = sp.dcIr;
                s_lastAcRed = sp.acRed;
                s_lastAcIr = sp.acIr;
                /* Chất lượng riêng của SpO2: estimator xác nhận DC/AC RED+IR, đủ
                   sample, không bão hòa, mẫu số khác 0, R hữu hạn và SpO2 trong dải
                   sinh lý (SPO2_STATUS_OK). ĐỘC LẬP với SQI peak/BPM ở trên. */
                const bool spo2QualityOk = (sst == SPO2_STATUS_OK);
                if (spo2QualityOk)
                {
                    s_latestSpo2 = sp.spo2;
                    s_latestSpo2Valid = true;
                    s_sessionSpo2Sum += sp.spo2;
                    if (s_sessionSpo2Count == 0U)
                    {
                        s_sessionSpo2Min = sp.spo2;
                        s_sessionSpo2Max = sp.spo2;
                    }
                    else
                    {
                        if (sp.spo2 < s_sessionSpo2Min) { s_sessionSpo2Min = sp.spo2; }
                        if (sp.spo2 > s_sessionSpo2Max) { s_sessionSpo2Max = sp.spo2; }
                    }
                    ++s_sessionSpo2Count;
                }
                else
                {
                    s_latestSpo2Valid = false;  /* không bao giờ hiện giá trị cũ / chất lượng xấu */
                }
            }
        }
    }
}

void Ppg_GetResult(PpgResult* out)
{
    if (out == 0)
    {
        return;
    }

    out->state = s_state;
    out->reason = s_reason;
    out->fingerPresent = s_fingerPresent;
    out->signalStable = (s_state == PPG_STATE_MEASURING);
    out->waveformVisible = (s_state == PPG_STATE_MEASURING);
    out->bpmValid = s_bpmValid && (s_state == PPG_STATE_MEASURING);
    out->bpm = out->bpmValid ? s_bpm : 0.0F;

    /* Tiến trình ổn định (theo thời gian, chặn bởi độ sẵn sàng). */
    if (s_state == PPG_STATE_STABILIZING)
    {
        /* Không có nowMs ở đây; tiến trình được đặt 100 khi vào MEASURING. Xấp xỉ
           theo số sample so với cửa sổ tối thiểu. */
        const uint32_t need = (PPG_STABILIZE_MIN_MS * PPG_SAMPLE_RATE_HZ) / 1000U;
        out->stabilizationProgress = (need > 0U)
            ? (100.0F * (float)s_samplesInState / (float)need) : 0.0F;
        if (out->stabilizationProgress > 99.0F) { out->stabilizationProgress = 99.0F; }
    }
    else if (s_state == PPG_STATE_MEASURING)
    {
        out->stabilizationProgress = 100.0F;
    }
    else
    {
        out->stabilizationProgress = 0.0F;
    }

    /* BPM trung bình phiên từ các RR interval đã chấp nhận (§13). Có sẵn dưới dạng
       giá trị sơ bộ khi đủ interval; chỉ đánh dấu hợp lệ sau thời gian đo tối
       thiểu. */
    const uint32_t elapsed = (s_measuringStarted && (s_sessionLastMs >= s_sessionStartMs))
                                 ? (s_sessionLastMs - s_sessionStartMs) : 0U;
    out->elapsedMeasurementMs = elapsed;
    out->validRrCount = s_sessionRrCount;
    out->bpmMin = (s_sessionRrCount > 0U) ? s_sessionBpmMin : 0.0F;
    out->bpmMax = (s_sessionRrCount > 0U) ? s_sessionBpmMax : 0.0F;
    out->averageBpm = 0.0F;
    out->averageBpmValid = false;
    if (s_sessionRrCount >= MEASUREMENT_MIN_RR_INTERVALS)
    {
        const uint32_t avgIntervalMs = s_sessionRrSumMs / s_sessionRrCount;
        if (avgIntervalMs > 0U)
        {
            const float avg = 60000.0F / (float)avgIntervalMs;
            if ((avg >= (float)PPG_BPM_MIN) && (avg <= (float)PPG_BPM_MAX))
            {
                out->averageBpm = avg;
                out->averageBpmValid = (elapsed >= MEASUREMENT_MIN_DURATION_MS);
            }
        }
    }

    out->redRaw = s_lastRedRaw;
    out->irRaw = s_lastIrRaw;
    out->redCentered = s_lastCenteredRed;
    out->irCentered = s_lastCenteredIr;
    out->redFiltered = s_lastRedFiltered;
    out->irFiltered = s_lastIrFiltered;

    out->acceptedPeaks = s_acceptedPeaks;
    out->rejectedPeaks = s_rejectedPeaks;
    out->droppedSamples = s_droppedSamples;
    out->fifoOverflows = s_fifoOverflows;
    out->filterMode = s_filterMode;
    out->maWindow = s_maWindowN;

    /* SpO2 + chất lượng tín hiệu. Giá trị mới nhất chỉ hiện khi đang đo; trung
       bình phiên cần đủ cửa sổ hợp lệ và thời gian tối thiểu. */
    const bool measuring = (s_state == PPG_STATE_MEASURING);
    out->spo2Valid = s_latestSpo2Valid && measuring;
    out->spo2 = out->spo2Valid ? s_latestSpo2 : 0.0F;
    out->averageSpo2 = (s_sessionSpo2Count > 0U)
        ? (s_sessionSpo2Sum / (float)s_sessionSpo2Count) : 0.0F;
    out->averageSpo2Valid = (s_sessionSpo2Count >= MEASUREMENT_MIN_SPO2_WINDOWS) &&
                            (elapsed >= MEASUREMENT_MIN_DURATION_MS);
    out->spo2Min = (s_sessionSpo2Count > 0U) ? s_sessionSpo2Min : 0.0F;
    out->spo2Max = (s_sessionSpo2Count > 0U) ? s_sessionSpo2Max : 0.0F;
    out->validSpo2Windows = s_sessionSpo2Count;
    out->sqiPercent = measuring ? s_sqiPercent : 0.0F;
    out->averageSqi = (s_sessionSqiCount > 0U)
        ? (s_sessionSqiSum / (float)s_sessionSqiCount) : 0.0F;
    out->ratioOfRatios = s_lastRatio;
    out->dcRed = s_lastDcRed;
    out->dcIr = s_lastDcIr;
    out->acRed = s_lastAcRed;
    out->acIr = s_lastAcIr;

    out->resultReady = s_resultReady;
    out->resultStatus = s_resultStatus;
    out->endReason = s_endReason;
    out->resultSaved = false;   /* DSP task ghi đè trường này sau khi lưu */

    /* Xuất cửa sổ waveform theo thứ tự thời gian (cũ nhất..mới nhất). */
    out->waveformCount = s_waveFill;
    out->peakCount = 0U;
    const uint16_t start = (s_waveFill < PPG_WAVE_POINTS)
                               ? 0U
                               : s_waveHead;
    for (uint16_t i = 0U; i < s_waveFill; ++i)
    {
        const uint16_t src = (uint16_t)((start + i) % PPG_WAVE_POINTS);
        out->waveform[i] = s_wave[src];
        if ((s_peakFlag[src] != 0U) && (out->peakCount < PPG_MAX_PEAKS))
        {
            out->peakIndices[out->peakCount] = i;
            ++out->peakCount;
        }
    }
    for (uint16_t i = s_waveFill; i < PPG_WAVE_POINTS; ++i)
    {
        out->waveform[i] = PPG_WAVE_ZERO;
    }

    /* Xuất cửa sổ waveform RED theo thứ tự thời gian. */
    out->redWaveformCount = s_waveRedFill;
    const uint16_t startRed = (s_waveRedFill < PPG_WAVE_POINTS)
                                  ? 0U
                                  : s_waveRedHead;
    for (uint16_t i = 0U; i < s_waveRedFill; ++i)
    {
        const uint16_t src = (uint16_t)((startRed + i) % PPG_WAVE_POINTS);
        out->redWaveform[i] = s_waveRed[src];
    }
    for (uint16_t i = s_waveRedFill; i < PPG_WAVE_POINTS; ++i)
    {
        out->redWaveform[i] = PPG_WAVE_ZERO;
    }
    if (!out->waveformVisible)
    {
        out->waveformCount = 0U;   /* không xuất gì trừ khi MEASURING */
        out->peakCount = 0U;
    }
}
