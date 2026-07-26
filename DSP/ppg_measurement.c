/**
 * @file    ppg_measurement.c
 * @brief   Engine đo PPG — không HAL, không cấp phát động.
 *
 * Luồng xử lý chính:
 *   1. Nhận mẫu RAW RED/IR từ sensor task (qua Ppg_PushSample)
 *   2. Phát hiện ngón tay (finger detection) với hysteresis
 *   3. Theo dõi DC baseline thích nghi và căn giữa tín hiệu
 *   4. Lọc tín hiệu theo chế độ được chọn (RAW/MA/Median/Lowpass/Cascade)
 *   5. Phát hiện đỉnh (peak detection) trên tín hiệu IR
 *   6. Tính BPM từ median các RR interval
 *   7. Tính SpO2 từ ratio-of-ratios trên mẫu RAW
 *   8. Tính SQI (Signal Quality Index) từ tỉ lệ peak chấp nhận/từ chối
 *   9. Tạo waveform 240 điểm với auto-range cho GUI
 *  10. Đóng gói kết quả qua Ppg_GetResult cho GUI bridge
 *
 * State machine: IDLE → WAIT_FINGER → STABILIZING → MEASURING → RESULT_READY
 *                (nhánh INVALID_SIGNAL và SENSOR_ERROR)
 *
 * @note    User-owned. Thuần C, không HAL, không cấp phát.
 *          Dữ liệu RAW giữ nguyên; dữ liệu centered là riêng.
 */

#include "ppg_measurement.h"
#include "moving_average_filter.h"
#include "median_filter.h"
#include "lowpass_filter.h"
#include "spo2_estimator.h"

/* ========================================================================== */
/* TRẠNG THÁI ENGINE (toàn cục tĩnh)                                          */
/* ========================================================================== */

/* ---- State machine ---- */
static PpgState s_state;              /**< Trạng thái hiện tại của engine.    */
static PpgInvalidReason s_reason;     /**< Lý do invalid (nếu có).            */
static bool s_sensorError;            /**< Cờ lỗi cảm biến từ bên ngoài.      */

/* ---- Phát hiện ngón tay (finger detection) ---- */
/* Dùng bộ bám DC IR chậm (slow EMA) để theo dõi mức DC tổng. Khi DC vượt
   ngưỡng on đủ số mẫu liên tiếp → xác nhận có ngón tay. Ngược lại với ngưỡng
   off. Hysteresis + xác nhận liên tiếp tránh false trigger do nhiễu. */
static uint32_t s_irDc;              /**< Bộ bám DC IR chậm (slow EMA).      */
static bool s_irDcInit;              /**< Đã khởi tạo bộ bám DC chưa.        */
static bool s_fingerPresent;         /**< Đang có ngón tay trên cảm biến.     */
static uint16_t s_onCount;           /**< Số mẫu liên tiếp vượt ngưỡng on.   */
static uint16_t s_offCount;          /**< Số mẫu liên tiếp dưới ngưỡng off.  */

/* ---- Baseline / căn giữa ---- */
/* DC baseline thích nghi theo EMA, lấy mean của tín hiệu RAW. Tín hiệu
   centered = RAW - baseline, loại bỏ thành phần DC để只剩 lại AC (sóng tim). */
static int32_t s_baselineIr;         /**< Baseline DC hiện tại của IR.        */
static int32_t s_baselineRed;        /**< Baseline DC hiện tại của RED.       */
static int32_t s_lastCenteredIr;     /**< IR centered mới nhất (RAW - DC).    */
static int32_t s_lastCenteredRed;    /**< RED centered mới nhất (RAW - DC).   */
static uint32_t s_lastRedRaw;        /**< Giá trị RED RAW mới nhất.          */
static uint32_t s_lastIrRaw;         /**< Giá trị IR RAW mới nhất.           */

/* ---- Bộ lọc tín hiệu (filter chain) ---- */
/* Mỗi kênh IR/RED có 3 bộ lọc song song: Moving Average, Median, Lowpass.
   Tại một thời điểm chỉ dùng 1 chế độ (s_filterMode). Kết quả filtered lưu
   ở s_lastIrFiltered / s_lastRedFiltered. Buffer được định cỡ theo cửa sổ
   tối đa (PPG_MA_WINDOW_MAX=15) để đổi N lúc chạy mà không cấp phát lại. */
static int32_t s_maBufIr[PPG_MA_WINDOW_MAX];    /**< Buffer MA cho IR.        */
static int32_t s_maBufRed[PPG_MA_WINDOW_MAX];   /**< Buffer MA cho RED.       */
static MovingAverageFilter s_maIr;               /**< Instance MA filter IR.   */
static MovingAverageFilter s_maRed;              /**< Instance MA filter RED.  */
static int32_t s_lastIrFiltered;     /**< IR đã lọc mới nhất.               */
static int32_t s_lastRedFiltered;    /**< RED đã lọc mới nhất.              */

/* Median filter (IR + RED) — loại nhiễu xung/spikes mà không làm mờ đỉnh */
static int32_t s_medianBufIr[PPG_MA_WINDOW_MAX];    /**< Buffer ring median IR. */
static int32_t s_medianSortIr[PPG_MA_WINDOW_MAX];   /**< Buffer phụ sort IR.   */
static MedianFilter s_medianIr;                      /**< Instance median IR.   */

static int32_t s_medianBufRed[PPG_MA_WINDOW_MAX];   /**< Buffer ring median RED.*/
static int32_t s_medianSortRed[PPG_MA_WINDOW_MAX];  /**< Buffer phụ sort RED.  */
static MedianFilter s_medianRed;                     /**< Instance median RED.  */

/* Low-pass filter Butterworth (IR + RED) — loại nhiễu tần số cao >4Hz */
static LowpassFilter s_lpIr;         /**< Instance lowpass Butterworth IR.    */
static LowpassFilter s_lpRed;        /**< Instance lowpass Butterworth RED.   */

/* ---- Chế độ lọc đang dùng ---- */
static PpgFilterMode s_filterMode;   /**< Chế độ lọc toàn cục (ảnh hưởng
                                          peak/BPM/waveform). SpO2 luôn dùng
                                          RAW bất kể chế độ này.             */
static uint8_t s_maWindowN;          /**< Cỡ cửa sổ MA đang dùng (1..15).   */

/* ---- Envelope (auto-range cho waveform) ---- */
/* Theo dõi min/max của tín hiệu IR centered để tự độngsetScale waveform
   cho vừa khung hiển thị 0..1000. */
static int32_t s_envMax;             /**< Giá trị max hiện tại của envelope.  */
static int32_t s_envMin;             /**< Giá trị min hiện tại của envelope.  */
static int32_t s_displayRange;       /**< Khoảng hiển thị hiện tại (auto).   */

/* ---- Ổn định tín hiệu ---- */
static uint32_t s_stateStartMs;      /**< Thời gian bắt đầu trạng thái hiện tại.*/
static uint32_t s_samplesInState;    /**< Số mẫu đã xử lý trong trạng thái này. */

/* ---- Waveform IR (ring buffer 240 điểm) ---- */
/* waveform[] lưu tín hiệu IR đã mapped vào [0..1000] với WAVE_ZERO=500 là
   đường tâm. peakFlag[] đánh dấu vị trí đỉnh đã chấp nhận. */
static int16_t s_wave[PPG_WAVE_POINTS];          /**< Ring buffer waveform IR. */
static uint8_t s_peakFlag[PPG_WAVE_POINTS];      /**< Cờ đánh dấu peak.       */
static uint16_t s_waveHead;          /**< Vị trí ghi tiếp trong ring buffer.  */
static uint16_t s_waveFill;          /**< Số điểm hợp lệ trong buffer.        */

/* ---- Waveform RED (dùng chung envelope + auto-range với IR) ---- */
static int16_t s_waveRed[PPG_WAVE_POINTS];       /**< Ring buffer waveform RED.*/
static uint16_t s_waveRedHead;       /**< Vị trí ghi tiếp waveform RED.       */
static uint16_t s_waveRedFill;       /**< Số điểm hợp lệ waveform RED.        */

/* ---- Peak detector ---- */
/* Phát hiện đỉnh theo streaming: theo dõi cạnh lên (rising edge), khi phát
   hiện cực đại cục bộ thì kiểm tra amplitude + prominence + khoảng cách
   thời gian so với đỉnh trước. */
static int32_t s_prevFilteredIr;     /**< Giá trị IR filtered của mẫu trước.  */
static bool s_prevValid;             /**< Đã có mẫu trước chưa.              */
static bool s_rising;                /**< Đang trên cạnh lên hay xuống.       */
static uint32_t s_lastPeakMs;        /**< Thời gian đỉnh cuối cùng (ms).      */
static bool s_havePeak;              /**< Đã phát hiện đỉnh đầu tiên chưa.   */

/* ---- BPM (từ median RR intervals) ---- */
/* Lưu 5 interval cuối cùng, lấy median để ra BPM tức thời. Median robust
   hơn mean trước nhiễu (interval bất thường). */
static uint16_t s_intervals[PPG_BPM_INTERVAL_COUNT]; /**< Buffer circular RR intervals.*/
static uint8_t s_intervalCount;      /**< Số interval đã lưu (≤ COUNT).       */
static uint8_t s_intervalHead;       /**< Vị trí ghi tiếp trong buffer.       */
static float s_bpm;                  /**< BPM tức thời (median RR).           */
static bool s_bpmValid;              /**< BPM hiện tại có hợp lệ chưa.        */

/* ---- Bộ tích lũy theo phiên ---- */
/* Phiên trải suốt một lần chạm ngón tay (finger-on → finger-off). Sống sót
   qua các lần MEASURING↔STABILIZING ngắn. Chỉ finger-off được xác nhận mới
   kết thúc phiên. BPM trung bình lấy từ RR interval (không phải mean BPM/frame). */
static bool s_sessionActive;         /**< Đang có phiên đo active.            */
static bool s_measuringStarted;      /**< Đã thấy lần vào MEASURING đầu tiên. */
static uint32_t s_sessionStartMs;    /**< Timestamp sample MEASURING đầu tiên.*/
static uint32_t s_sessionLastMs;     /**< Timestamp sample mới nhất.          */
static uint32_t s_sessionRrSumMs;    /**< Tổng các RR interval đã chấp nhận.  */
static uint16_t s_sessionRrCount;    /**< Số RR interval đã chấp nhận.        */
static float s_sessionBpmMin;        /**< BPM tức thời nhỏ nhất trong phiên.  */
static float s_sessionBpmMax;        /**< BPM tức thời lớn nhất trong phiên.  */

/* ---- SpO2 + chất lượng tín hiệu ---- */
/* SpO2 tính trên mẫu RAW RED/IR (độc lập với filter mode hiển thị).
   SQI = accepted/(accepted+rejected) × 100, chỉ là heuristic, KHÔNG phải
   điều kiện hợp lệ của SpO2 (SpO2 dùng chất lượng DC/AC riêng trong estimator). */
static Spo2Estimator s_spo2;         /**< Estimator SpO2 ratio-of-ratios.     */
static float s_latestSpo2;           /**< SpO2% mới nhất (nếu hợp lệ).       */
static bool s_latestSpo2Valid;       /**< SpO2 mới nhất có hợp lệ không.      */
static float s_sqiPercent;           /**< SQI hiện tại (0..100%).             */
static float s_lastRatio;            /**< Ratio R gần nhất (chẩn đoán).       */
static float s_lastDcRed;            /**< DC RED cửa sổ gần nhất.            */
static float s_lastDcIr;             /**< DC IR cửa sổ gần nhất.             */
static float s_lastAcRed;            /**< AC RED cửa sổ gần nhất.            */
static float s_lastAcIr;             /**< AC IR cửa sổ gần nhất.             */
static float s_sessionSpo2Sum;       /**< Tổng SpO2 các cửa sổ hợp lệ.       */
static uint16_t s_sessionSpo2Count;  /**< Số cửa sổ SpO2 hợp lệ trong phiên. */
static float s_sessionSpo2Min;       /**< SpO2 nhỏ nhất trong phiên.         */
static float s_sessionSpo2Max;       /**< SpO2 lớn nhất trong phiên.         */
static float s_sessionSqiSum;        /**< Tổng SQI các frame.                */
static uint32_t s_sessionSqiCount;   /**< Số frame đã cộng SQI.              */

/* ---- Kết quả đã chốt ---- */
/* Khi nhấc ngón tay, kết quả được đóng băng (freeze) cho tới khi ngón tay mới
   bắt đầu phiên mới. DSP task xây bản ghi lịch sử trước khi reset. */
static bool s_resultReady;           /**< Có kết quả đã chốt, đóng băng.      */
static MeasurementResultStatus s_resultStatus; /**< VALID/PARTIAL/INVALID.     */
static MeasurementEndReason s_endReason;       /**< Lý do kết thúc phiên.     */

/* ---- Chẩn đoán ---- */
static uint32_t s_acceptedPeaks;     /**< Số đỉnh được chấp nhận.             */
static uint32_t s_rejectedPeaks;     /**< Số đỉnh bị từ chối.                 */
static uint32_t s_droppedSamples;    /**< Số mẫu bị rơi (từ sensor task).     */
static uint32_t s_fifoOverflows;     /**< Số lần FIFO cảm biến overflow.      */

/* ========================================================================== */
/* HÀM HỖ TRỢ (internal)                                                     */
/* ========================================================================== */

/**
 * @brief Giới hạn giá trị v trong khoảng [lo, hi].
 * Nếu v < lo trả lo, v > hi trả hi, nếu không trả v.
 */
static int32_t clamp32(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

/**
 * @brief Trả về tín hiệu IR dùng cho phân tích (peak/BPM/waveform).
 * Ở chế độ RAW: dùng centered (chưa lọc).
 * Ở các chế độ khác: dùng filtered (đã qua bộ lọc).
 */
static int32_t analysisIr(void)
{
    return (s_filterMode == PPG_FILTER_RAW) ? s_lastCenteredIr : s_lastIrFiltered;
}

/**
 * @brief Trả về tín hiệu RED dùng cho waveform display.
 * Logic tương tự analysisIr().
 */
static int32_t analysisRed(void)
{
    return (s_filterMode == PPG_FILTER_RAW) ? s_lastCenteredRed : s_lastRedFiltered;
}

/**
 * @brief Reset toàn bộ trạng thái peak detector và BPM.
 * Gọi khi bắt đầu measuring mới hoặc khi tín hiệu suy giảm.
 */
static void resetPeakDetector(void)// Peak Detector phát hiện đỉnh sóng PPG
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

/**
 * @brief Bắt đầu phiên đo mới (gọi khi finger-on).
 * Reset tất cả bộ tích lũy phiên: BPM min/max, SpO2 min/max/sum, SQI sum,
 * thời gian phiên, và cờ kết quả.
 */
static void resetSession(void)
{
    s_sessionActive = true;
    s_measuringStarted = false;
    s_sessionStartMs = 0U;
    s_sessionLastMs = 0U;
    s_sessionRrSumMs = 0U;
    s_sessionRrCount = 0U;
    s_sessionBpmMin = 999.0F;   /* lớn hơn BPM thực → min/max đúng từ peak đầu */
    s_sessionBpmMax = 0.0F;
    s_sessionSpo2Sum = 0.0F;
    s_sessionSpo2Count = 0U;
    s_sessionSpo2Min = 101.0F;  /* lớn hơn SpO2 thực → min/max đúng từ cửa sổ đầu */
    s_sessionSpo2Max = 0.0F;
    s_sessionSqiSum = 0.0F;
    s_sessionSqiCount = 0U;
    s_resultReady = false;
    s_resultStatus = MEASUREMENT_RESULT_INVALID;
    s_endReason = MEASUREMENT_END_FINGER_REMOVED;
}

/**
 * @brief Đóng băng và phân loại phiên khi finger-off.
 *
 * Phân loại dựa trên 3 điều kiện:
 *   - durationOk: thời gian đo ≥ MEASUREMENT_MIN_DURATION_MS (10s)
 *   - rrOk: số RR intervals ≥ MEASUREMENT_MIN_RR_INTERVALS (5)
 *   - spo2Ok: số cửa sổ SpO2 ≥ MEASUREMENT_MIN_SPO2_WINDOWS (3)
 *
 *   durationOk && rrOk && spo2Ok  → VALID
 *   durationOk && rrOk            → PARTIAL
 *   còn lại                       → INVALID
 *
 * Kết quả được đóng băng (s_resultReady = true), KHÔNG xóa accumulator
 * ở đây — DSP task sẽ xây bản ghi lịch sử trước khi reset.
 */
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
    s_resultReady = true;   /* đóng băng; KHÔNG xóa accumulator ở đây */
}

/**
 * @brief Xóa toàn bộ trạng thái per-measurement.
 * Gọi khi finger-on mới, hoặc khi tín hiệu suy giảm cần ổn định lại.
 * Reset: baseline, filters (MA/Median/Lowpass), SpO2, envelope, waveform,
 * peak detector, BPM intervals.
 */
static void resetMeasurement(uint32_t seedIr, uint32_t seedRed, uint32_t nowMs)
{
    /* Seed baseline từ mẫu RAW hiện tại để tránh transient lớn */
    s_baselineIr = (int32_t)seedIr;
    s_baselineRed = (int32_t)seedRed;
    s_lastCenteredIr = 0;
    s_lastCenteredRed = 0;

    /* Reset tất cả bộ lọc */
    MovingAverage_Reset(&s_maIr);
    MovingAverage_Reset(&s_maRed);
    Median_Reset(&s_medianIr);
    Median_Reset(&s_medianRed);
    Lowpass_Reset(&s_lpIr);
    Lowpass_Reset(&s_lpRed);
    s_lastIrFiltered = 0;
    s_lastRedFiltered = 0;

    /* Reset SpO2 estimator */
    Spo2_Reset(&s_spo2);
    s_latestSpo2 = 0.0F;
    s_latestSpo2Valid = false;
    s_sqiPercent = 0.0F;
    s_lastRatio = 0.0F;
    s_lastDcRed = 0.0F;
    s_lastDcIr = 0.0F;
    s_lastAcRed = 0.0F;
    s_lastAcIr = 0.0F;

    /* Reset envelope (auto-range) */
    s_envMax = 0;
    s_envMin = 0;
    s_displayRange = PPG_DISPLAY_RANGE_MIN;

    /* Xóa waveform IR + RED và peak flags */
    s_waveHead = 0U;
    s_waveFill = 0U;
    s_waveRedHead = 0U;
    s_waveRedFill = 0U;
    for (uint16_t i = 0U; i < PPG_WAVE_POINTS; ++i)
    {
        s_wave[i] = PPG_WAVE_ZERO;      /* đường tâm = 500 */
        s_waveRed[i] = PPG_WAVE_ZERO;
        s_peakFlag[i] = 0U;
    }

    /* Reset peak detector và BPM */
    resetPeakDetector();

    /* Đặt lại đồng hồ trạng thái */
    s_stateStartMs = nowMs;
    s_samplesInState = 0U;
}

/**
 * @brief Cập nhật envelope min/max thích nghi từ tín hiệu centered.
 *
 * Dùng EMA asymmetrical: tăng nhanh (full step), giảm chậm (>>7).
 * Điều này giúp envelope bám theo tín hiệu khi nó tăng nhưng không
 * giảm quá nhanh khi có spike ngắn → ổn định hơn cho auto-range.
 */
static void updateEnvelope(int32_t centered)
{
    if (centered > s_envMax) { s_envMax = centered; }
    else { s_envMax -= (s_envMax - centered) >> 7; }  /* giảm chậm */
    if (centered < s_envMin) { s_envMin = centered; }
    else { s_envMin += (centered - s_envMin) >> 7; }  /* tăng chậm */
}

/**
 * @brief Ánh xạ tín hiệu centered vào khoảng hiển thị [0..1000] và đẩy vào
 *        ring buffer waveform cho cả IR và RED.
 *
 * Auto-range: căn đồ thị theo điểm giữa tín hiệu (mid) và co nửa biên độ
 * về displayRange với margin NUM/DEN (5/4) để cả đỉnh lẫn đáy đều lọt khung.
 * Công thức: mapped = ZERO + ((centered - mid) × span) / displayRange
 *   where span = (FULL_SCALE/2) - 50, ZERO = 500.
 *
 * Waveform RED dùng chung envelope + mid với IR để đồ thị叠 lên nhau đúng tỷ lệ.
 */
static void pushWaveformPoint(int32_t centered, uint8_t isPeak)
{
    /* Tính điểm giữa tín hiệu từ envelope */
    const int32_t mid = (s_envMax + s_envMin) / 2;

    /* Nửa biên độ tín hiệu × margin ratio → target cho displayRange */
    const int32_t halfAmp = (s_envMax - s_envMin) / 2;
    const int32_t target = clamp32((halfAmp * PPG_DISPLAY_MARGIN_NUM) / PPG_DISPLAY_MARGIN_DEN,
                                   PPG_DISPLAY_RANGE_MIN, PPG_DISPLAY_RANGE_MAX);
    /* Đổi displayRange chậm để đồ thị không nhấp nháy */
    s_displayRange += (target - s_displayRange) >> 4;
    if (s_displayRange < PPG_DISPLAY_RANGE_MIN) { s_displayRange = PPG_DISPLAY_RANGE_MIN; }

    /* Map centered → [ZERO-span .. ZERO+span] */
    const int32_t span = (PPG_WAVE_FULL_SCALE / 2) - 50;  /* +/- quanh đường zero */
    int32_t mapped = PPG_WAVE_ZERO + ((centered - mid) * span) / s_displayRange;
    mapped = clamp32(mapped, PPG_WAVE_ZERO - span, PPG_WAVE_ZERO + span);

    /* Lưu vào ring buffer IR */
    s_wave[s_waveHead] = (int16_t)mapped;
    s_peakFlag[s_waveHead] = isPeak;
    s_waveHead = (uint16_t)((s_waveHead + 1U) % PPG_WAVE_POINTS);
    if (s_waveFill < PPG_WAVE_POINTS) { ++s_waveFill; }

    /* RED waveform: dùng chung auto-range envelope với IR */
    const int32_t redCentered = analysisRed();
    int32_t redMapped = PPG_WAVE_ZERO + ((redCentered - mid) * span) / s_displayRange;
    redMapped = clamp32(redMapped, PPG_WAVE_ZERO - span, PPG_WAVE_ZERO + span);
    s_waveRed[s_waveRedHead] = (int16_t)redMapped;
    s_waveRedHead = (uint16_t)((s_waveRedHead + 1U) % PPG_WAVE_POINTS);
    if (s_waveRedFill < PPG_WAVE_POINTS) { ++s_waveRedFill; }
}

/**
 * @brief Tính median của các RR interval đã ghi.
 *
 * Copy nhỏ buffer intervals sang mảng tạm, insertion sort, lấy phần tử giữa.
 * Median robust hơn mean: loại bỏ được 1-2 interval bất thường (do miss peak
 * hoặc double peak) mà BPM vẫn đúng.
 */
static uint16_t medianInterval(void)
{
    uint16_t tmp[PPG_BPM_INTERVAL_COUNT];
    for (uint8_t i = 0U; i < s_intervalCount; ++i)
    {
        tmp[i] = s_intervals[i];
    }
    /* Insertion sort (N nhỏ = 5, hiệu quả hơn qsort) */
    for (uint8_t i = 1U; i < s_intervalCount; ++i)
    {
        const uint16_t key = tmp[i];
        int8_t j = (int8_t)i - 1;
        while ((j >= 0) && (tmp[j] > key)) { tmp[j + 1] = tmp[j]; --j; }
        tmp[j + 1] = key;
    }
    return tmp[s_intervalCount / 2U];  /* phần tử giữa = median */
}

/**
 * @brief Tiếp nhận một RR interval hợp lệ (đã qua kiểm tra khoảng cách).
 *
 * - Cộng dồn vào bộ tích lũy phiên (s_sessionRrSumMs, s_sessionRrCount)
 *   để tính BPM trung bình phiên sau này.
 * - Cập nhật min/max BPM phiên từ instant BPM = 60000/intervalMs.
 * - Đẩy vào buffer circular s_intervals và tính median → instant BPM.
 * - Nếu median nằm trong khoảng hợp lệ [PPG_BPM_MIN, PPG_BPM_MAX] → s_bpmValid.
 */
static void addInterval(uint16_t intervalMs)
{
    /* Nạp cho bộ tích lũy trung bình phiên */
    s_sessionRrSumMs += intervalMs;
    ++s_sessionRrCount;

    /* Cập nhật min/max BPM phiên từ instant BPM */
    const float ibpm = 60000.0F / (float)intervalMs;
    if (s_sessionRrCount == 1U) { s_sessionBpmMin = ibpm; s_sessionBpmMax = ibpm; }
    else
    {
        if (ibpm < s_sessionBpmMin) { s_sessionBpmMin = ibpm; }
        if (ibpm > s_sessionBpmMax) { s_sessionBpmMax = ibpm; }
    }

    /* Đẩy vào buffer circular intervals */
    s_intervals[s_intervalHead] = intervalMs;
    s_intervalHead = (uint8_t)((s_intervalHead + 1U) % PPG_BPM_INTERVAL_COUNT);
    if (s_intervalCount < PPG_BPM_INTERVAL_COUNT) { ++s_intervalCount; }

    /* Tính BPM tức thời từ median (cần đủ 3 intervals) */
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
    s_bpmValid = false;  /* chưa đủ data hoặc BPM ngoài khoảng */
}

/**
 * @brief Peak detector streaming trên tín hiệu IR đã centered.
 *
 * Thuật toán:
 *   1. Theo dõi cạnh lên (rising): centered > prevFiltered
 *   2. Khi chuyển từ rising → không rising → phát hiện cực đại cục bộ
 *   3. Kiểm tra amplitude (envMax - envMin ≥ PPG_MIN_AC_AMPLITUDE)
 *   4. Kiểm tra prominence (giá trị tại đỉnh - threshold ≥ PPG_PEAK_PROMINENCE)
 *   5. Kiểm tra khoảng cách thời gian:
 *      - < PPG_PEAK_MIN_INTERVAL_MS (300ms) → rejected (double peak)
 *      - > PPG_PEAK_MAX_INTERVAL_MS (1500ms) → rejected (quá xa, reset intervals)
 *      - Trong khoảng → accepted, gọi addInterval()
 *   6. Đánh dấu peak trên waveform (s_peakFlag)
 */
static void runPeakDetector(int32_t centered, uint32_t nowMs)
{
    uint8_t markPeak = 0U;

    if (s_prevValid)
    {
        const bool risingNow = (centered > s_prevFilteredIr);

        /* Phát hiện chuyển cạnh: rising → không rising = cực đại cục bộ */
        if (s_rising && !risingNow)
        {
            /* Kiểm tra biên độ đỉnh-đỉnh từ envelope */
            const int32_t amplitude = s_envMax - s_envMin;
            const int32_t threshold = s_envMin + (amplitude / 2); /* ngưỡng giữa */
            const bool strong = (amplitude >= PPG_MIN_AC_AMPLITUDE);

            /* Kiểm tra prominence: đỉnh phải cao hơn ngưỡng + prominence */
            const bool aboveThr = (s_prevFilteredIr >= (threshold + PPG_PEAK_PROMINENCE));

            if (strong && aboveThr)
            {
                if (!s_havePeak)
                {
                    /* Peak đầu tiên: chưa có interval để tính BPM */
                    s_havePeak = true;
                    s_lastPeakMs = nowMs;
                    markPeak = 1U;
                }
                else
                {
                    /* So sánh với đỉnh trước để tính interval */
                    const uint32_t interval = nowMs - s_lastPeakMs;

                    if (interval < PPG_PEAK_MIN_INTERVAL_MS)
                    {
                        ++s_rejectedPeaks;      /* Quá gần → double peak, bỏ */
                    }
                    else
                    {
                        s_lastPeakMs = nowMs;
                        markPeak = 1U;

                        if (interval <= PPG_PEAK_MAX_INTERVAL_MS)
                        {
                            /* Interval hợp lệ (300–1500ms → 40–200 BPM) */
                            ++s_acceptedPeaks;
                            addInterval((uint16_t)interval);
                        }
                        else
                        {
                            /* Quá xa → tín hiệu gián đoạn, reset lịch sử */
                            ++s_rejectedPeaks;
                            s_bpmValid = false;
                            s_intervalCount = 0U;
                        }
                    }
                }
            }
        }
        s_rising = risingNow;
    }

    /* Đánh dấu peak trên sample vừa lưu (waveHead - 1) */
    if (markPeak != 0U)
    {
        const uint16_t idx = (uint16_t)((s_waveHead + PPG_WAVE_POINTS - 1U) % PPG_WAVE_POINTS);
        s_peakFlag[idx] = 1U;
    }

    s_prevFilteredIr = centered;
    s_prevValid = true;
}

/* ========================================================================== */
/* API CÔNG KHAI                                                              */
/* ========================================================================== */

/**
 * @brief Khởi tạo toàn bộ engine PPG.
 *
 * Đặt trạng thái ban đầu WAIT_FINGER, khởi tạo tất cả bộ lọc (MA, Median,
 * Lowpass), SpO2 estimator, và reset measurement/session.
 * Chế độ lọc mặc định: MOVING_AVERAGE với window = PPG_MOVING_AVERAGE_WINDOW (5).
 */
void Ppg_Init(void)
{
    s_state = PPG_STATE_WAIT_FINGER;
    s_reason = PPG_REASON_NO_FINGER;
    s_sensorError = false;

    /* Finger detection — chưa có ngón tay */
    s_irDc = 0U;
    s_irDcInit = false;
    s_fingerPresent = false;
    s_onCount = 0U;
    s_offCount = 0U;

    /* RAW values */
    s_lastRedRaw = 0U;
    s_lastIrRaw = 0U;

    /* Diagnostic counters */
    s_droppedSamples = 0U;
    s_fifoOverflows = 0U;

    s_sessionActive = false;

    /* Khởi tạo chế độ lọc mặc định */
    s_filterMode = PPG_FILTER_MOVING_AVERAGE;
    s_maWindowN = (uint8_t)PPG_MOVING_AVERAGE_WINDOW;
    (void)MovingAverage_Init(&s_maIr, s_maBufIr, s_maWindowN);
    (void)MovingAverage_Init(&s_maRed, s_maBufRed, s_maWindowN);

    /* Khởi tạo median filter */
    Median_Init(&s_medianIr, s_medianBufIr, s_medianSortIr, s_maWindowN);
    Median_Init(&s_medianRed, s_medianBufRed, s_medianSortRed, s_maWindowN);

    /* Khởi tạo lowpass Butterworth filter */
    Lowpass_Init(&s_lpIr);
    Lowpass_Init(&s_lpRed);

    /* Khởi tạo SpO2 estimator với calibration mặc định */
    Spo2_Init(&s_spo2, NULL);

    resetSession();
    s_sessionActive = false;
    resetMeasurement(0U, 0U, 0U);
}

/**
 * @brief Đổi chế độ lọc tín hiệu (toàn cục).
 * Ảnh hưởng đến: tín hiệu phân tích (peak/BPM), waveform display.
 * KHÔNG ảnh hưởng đến SpO2 (luôn dùng RAW).
 */
void Ppg_SetFilterMode(PpgFilterMode mode)
{
    s_filterMode = mode;
}

/**
 * @brief Đổi cỡ cửa sổ moving average (1..PPG_MA_WINDOW_MAX).
 * Khởi tạo lại toàn bộ bộ lọc (MA, Median, Lowpass) vì cửa sổ thay đổi.
 */
void Ppg_SetMaWindow(uint8_t window)
{
    uint8_t n = window;
    if (n < 1U) { n = 1U; }
    if (n > (uint8_t)PPG_MA_WINDOW_MAX) { n = (uint8_t)PPG_MA_WINDOW_MAX; }
    s_maWindowN = n;

    /* Re-init tất cả filters với cửa sổ mới */
    (void)MovingAverage_Init(&s_maIr, s_maBufIr, n);
    (void)MovingAverage_Init(&s_maRed, s_maBufRed, n);
    Median_Reset(&s_medianIr);
    Median_Reset(&s_medianRed);
    Lowpass_Reset(&s_lpIr);
    Lowpass_Reset(&s_lpRed);
}

/**
 * @brief Xử lý sự kiện lỗi cảm biến từ bên ngoài.
 * Khi error = true: chuyển sang SENSOR_ERROR, không đo được.
 * Khi error = false: trở về WAIT_FINGER nếu đang ở SENSOR_ERROR.
 */
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

/**
 * @brief Báo cáo số mẫu bị rơi và FIFO overflow từ sensor task.
 * Cộng dồn vào diagnostic counters để hiển thị trong waveform info.
 */
void Ppg_ReportLoss(uint32_t droppedDelta, uint32_t overflowDelta)
{
    s_droppedSamples += droppedDelta;
    s_fifoOverflows += overflowDelta;
}

/**
 * @brief **HÀM CHÍNH** — Nhận một mẫu PPG RAW từ sensor task.
 *
 * Xử lý theo từng bước:
 *
 * **Bước 1 — Finger detection (hysteresis):**
 *   - Theo dõi DC IR qua slow EMA (shift 6 bits ≈ α=1/64)
 *   - Nếu chưa có ngón tay: đếm mẫu liên tiếp > ON_THRESHOLD (50000)
 *     → đủ 15 mẫu → xác nhận finger-on, reset measurement + session
 *   - Nếu đang có ngón tay: đếm mẫu liên tiếp < OFF_THRESHOLD (30000)
 *     → đủ 25 mẫu → xác nhận finger-off
 *       + Nếu đã đo (measuringStarted) → finalizeSession, chuyển RESULT_READY
 *       + Nếu chưa đo → bỏ qua, trở về WAIT_FINGER
 *
 * **Bước 2 — Baseline centering:**
 *   - STABILIZING: EMA nhanh (chia 16) để bắt kịp baseline nhanh
 *   - MEASURING: EMA chậm (chia 2^BASELINE_SHIFT = 2^9 = 512) để chỉ bù trôi
 *   - centered = RAW - baseline
 *
 * **Bước 3 — Filter chain:**
 *   Switch trên s_filterMode, áp dụng cho cả IR và RED:
 *   - RAW: centered trực tiếp
 *   - MOVING_AVERAGE: cửa sổ N = s_maWindowN
 *   - MEDIAN: cửa sổ N = s_maWindowN
 *   - LOWPASS: Butterworth fc=4Hz
 *   - MEDIAN_LOWPASS: cascade Median → Lowpass
 *
 * **Bước 4 — Envelope tracking** (chỉ trong MEASURING):
 *   updateEnvelope(analysisIr()) → cập nhật envMax/envMin
 *
 * **Bước 5 — Chuyển trạng thái:**
 *   STABILIZING → MEASURING khi: elapsed ≥ 2.5s, DC ok, amplitude ok, đủ mẫu
 *   MEASURING → STABILIZING khi: saturation hoặc weak signal hoặc amplitude thấp
 *
 * **Bước 6 — Peak detection + BPM** (chỉ trong MEASURING):
 *   runPeakDetector() → phát hiện đỉnh → addInterval() → medianInterval() → BPM
 *
 * **Bước 7 — SQI** (chỉ trong MEASURING):
 *   SQI = 100 × acceptedPeaks / (acceptedPeaks + rejectedPeaks)
 *
 * **Bước 8 — SpO2** (chỉ trong MEASURING):
 *   Spo2_Process() trên mẫu RAW RED/IR, độc lập với filter mode
 *
 * @param sample Mẫu RAW từ cảm biến (RED/IR 18-bit + timestamp + sequence).
 */
void Ppg_PushSample(const PpgRawSample* sample)
{
    /* Kiểm tra đầu vào */
    if ((sample == 0) || s_sensorError)
    {
        return;
    }

    const uint32_t ir = sample->irRaw;
    const uint32_t red = sample->redRaw;
    const uint32_t nowMs = sample->timestampMs;
    s_lastIrRaw = ir;
    s_lastRedRaw = red;

    /* ---- BƯỚC 1: Finger detection (hysteresis) ---- */
    /* Bộ bám DC IR chậm: EMA với α ≈ 1/64 (shift 6 bits) */
    if (!s_irDcInit) { s_irDc = ir; s_irDcInit = true; }
    else { s_irDc = (uint32_t)((int32_t)s_irDc + (((int32_t)ir - (int32_t)s_irDc) >> 6)); }

    if (!s_fingerPresent)
    {
        /* Chưa có ngón tay: kiểm tra ngưỡng ON */
        if (s_irDc > PPG_FINGER_ON_THRESHOLD) { ++s_onCount; } else { s_onCount = 0U; }
        if (s_onCount >= PPG_FINGER_ON_SAMPLES)
        {
            /* ✓ Đã xác nhận finger-on */
            s_fingerPresent = true;
            s_offCount = 0U;
            s_state = PPG_STATE_STABILIZING;
            s_reason = PPG_REASON_NONE;
            resetMeasurement(ir, red, nowMs);   /* seed baseline từ mẫu hiện tại */
            resetSession();                       /* bắt đầu phiên mới */
        }
    }
    else
    {
        /* Đang có ngón tay: kiểm tra ngưỡng OFF */
        if (s_irDc < PPG_FINGER_OFF_THRESHOLD) { ++s_offCount; } else { s_offCount = 0U; }
        if (s_offCount >= PPG_FINGER_OFF_SAMPLES)
        {
            /* ✓ Đã xác nhận finger-off */
            s_fingerPresent = false;
            s_onCount = 0U;
            if (s_measuringStarted)
            {
                /* Đã có phép đo thực sự → đóng băng kết quả */
                finalizeSession(MEASUREMENT_END_FINGER_REMOVED);
                s_state = PPG_STATE_RESULT_READY;
                s_reason = PPG_REASON_NONE;
            }
            else
            {
                /* Chỉ chạm thoáng qua, chưa đo → bỏ qua */
                s_state = PPG_STATE_WAIT_FINGER;
                s_reason = PPG_REASON_NO_FINGER;
                resetMeasurement(ir, red, nowMs);
                resetSession();
                s_sessionActive = false;
            }
        }
    }

    /* Nếu không có ngón tay → không xử lý gì thêm */
    if (!s_fingerPresent)
    {
        return;
    }

    ++s_samplesInState;
    s_sessionLastMs = nowMs;

    /* Kiểm tra tín hiệu cơ bản */
    const bool saturated = (ir > PPG_SATURATION_LEVEL);
    const bool dcOk = (s_irDc > PPG_DC_MIN);

    /* ---- TRẠNG THÁI STABILIZING / INVALID_SIGNAL ---- */
    if ((s_state == PPG_STATE_STABILIZING) || (s_state == PPG_STATE_INVALID_SIGNAL))
    {
        /* Baseline EMA nhanh (chia 16) để bắt kịp nhanh khi vừa đặt ngón tay */
        s_baselineIr += ((int32_t)ir - s_baselineIr) / 16;
        s_baselineRed += ((int32_t)red - s_baselineRed) / 16;
        s_lastCenteredIr = (int32_t)ir - s_baselineIr;
        s_lastCenteredRed = (int32_t)red - s_baselineRed;

        /* Áp dụng bộ lọc theo chế độ */
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
            /* Cascade: Median trước → Lowpass sau */
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

        /* Cập nhật envelope từ tín hiệu IR đã lọc/chosen */
        updateEnvelope(analysisIr());

        /* Kiểm tra điều kiện chuyển sang MEASURING */
        const int32_t amplitude = s_envMax - s_envMin;
        const bool ampOk = (amplitude >= PPG_MIN_AC_AMPLITUDE);
        const uint32_t elapsed = nowMs - s_stateStartMs;
        const bool enoughSamples = (s_samplesInState >= (PPG_WAVE_POINTS / 2U));

        /* Xác định lý do invalid */
        if (saturated) { s_reason = PPG_REASON_SATURATION; }
        else if (!dcOk) { s_reason = PPG_REASON_WEAK_SIGNAL; }
        else { s_reason = PPG_REASON_NONE; }

        /* Chuyển sang MEASURING nếu đủ điều kiện */
        if ((elapsed >= PPG_STABILIZE_MIN_MS) && dcOk && ampOk && !saturated && enoughSamples)
        {
            s_state = PPG_STATE_MEASURING;
            s_reason = PPG_REASON_NONE;
            resetPeakDetector();    /* Bắt đầu phát hiện peak mới */

            if (!s_measuringStarted)
            {
                s_measuringStarted = true;
                s_sessionStartMs = nowMs;   /* Bắt đầu đồng hồ đo phiên */
            }

            /* Seed displayRange từ envelope đã ổn định */
            s_displayRange = clamp32(((amplitude / 2) * PPG_DISPLAY_MARGIN_NUM) / PPG_DISPLAY_MARGIN_DEN,
                                     PPG_DISPLAY_RANGE_MIN, PPG_DISPLAY_RANGE_MAX);
        }
        /* Chưa xuất waveform khi đang stabilizing */
    }

    /* ---- TRẠNG THÁI MEASURING ---- */
    else if (s_state == PPG_STATE_MEASURING)
    {
        /* Baseline EMA chậm (shift=9 → α≈1/512) chỉ bù trôi chậm */
        s_baselineIr += ((int32_t)ir - s_baselineIr) / (1 << PPG_BASELINE_SHIFT);
        s_baselineRed += ((int32_t)red - s_baselineRed) / (1 << PPG_BASELINE_SHIFT);
        s_lastCenteredIr = (int32_t)ir - s_baselineIr;
        s_lastCenteredRed = (int32_t)red - s_baselineRed;

        /* Áp dụng bộ lọc theo chế độ (giống STABILIZING) */
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

        /* Kiểm tra suy giảm tín hiệu → quay lại STABILIZING */
        if (saturated || !dcOk || !ampOk)
        {
            s_state = PPG_STATE_STABILIZING;
            s_reason = saturated ? PPG_REASON_SATURATION
                                 : (!ampOk ? PPG_REASON_WEAK_SIGNAL : PPG_REASON_UNSTABLE);
            s_bpmValid = false;
            s_latestSpo2Valid = false;
            Spo2_Reset(&s_spo2);       /* Bỏ cửa sổ SpO2 đang dang dở */
            s_stateStartMs = nowMs;
            s_samplesInState = 0U;
        }
        else
        {
            s_reason = PPG_REASON_NONE;

            /* Kiểm tra tín hiệu đã sẵn sàng cho peak detection */
            const bool sigReady = (s_filterMode == PPG_FILTER_RAW) ||
                                  (s_filterMode == PPG_FILTER_MEDIAN) ||
                                  (s_filterMode == PPG_FILTER_LOWPASS) ||
                                  (s_filterMode == PPG_FILTER_MEDIAN_LOWPASS) ||
                                  MovingAverage_IsReady(&s_maIr);

            if (sigReady)
            {
                const int32_t sig = analysisIr();

                /* Đẩy waveform IR + RED vào ring buffer */
                pushWaveformPoint(sig, 0U);

                /* Phát hiện đỉnh và tính BPM */
                runPeakDetector(sig, nowMs);
            }

            /* ---- SQI: heuristic chất lượng hướng peak/BPM ---- */
            /* SQI = 100 × acceptedPeaks / (acceptedPeaks + rejectedPeaks)
               Chỉ hiển thị trên Dashboard; KHÔNG dùng làm điều kiện hợp lệ SpO2
               (SpO2 dùng chất lượng DC/AC riêng trong estimator). */
            const uint32_t totalPeaks = s_acceptedPeaks + s_rejectedPeaks;
            s_sqiPercent = (totalPeaks > 0U)
                ? (100.0F * (float)s_acceptedPeaks / (float)totalPeaks) : 0.0F;
            s_sessionSqiSum += s_sqiPercent;
            ++s_sessionSqiCount;

            /* ---- SpO2 trên RAW RED/IR (độc lập filter mode) ---- */
            Spo2Result sp;
            const Spo2Status sst = Spo2_Process(&s_spo2, red, ir, nowMs, &sp);
            if (sst != SPO2_STATUS_NOT_READY)
            {
                /* Lưu kết quả chẩn đoán */
                s_lastRatio = sp.ratio;
                s_lastDcRed = sp.dcRed;
                s_lastDcIr = sp.dcIr;
                s_lastAcRed = sp.acRed;
                s_lastAcIr = sp.acIr;

                /* Chỉ chấp nhận SpO2 khi estimator xác nhận hợp lệ
                   (SPO2_STATUS_OK: đủ sample, DC/AC ok, không bão hòa, R hữu hạn,
                   SpO2 trong [70%, 100%]). ĐỘC LẬP với SQI peak/BPM. */
                const bool spo2QualityOk = (sst == SPO2_STATUS_OK);
                if (spo2QualityOk)
                {
                    s_latestSpo2 = sp.spo2;
                    s_latestSpo2Valid = true;

                    /* Cập nhật bộ tích lũy phiên */
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
                    /* Không hợp lệ → không hiển thị giá trị cũ */
                    s_latestSpo2Valid = false;
                }
            }
        }
    }
}

/**
 * @brief Đóng gói toàn bộ kết quả vào PpgResult cho GUI bridge.
 *
 * Gọi định kỳ bởi DSP task để đọc kết quả. Bao gồm:
 *   - Trạng thái engine + lý do invalid
 *   - BPM tức thời + BPM trung bình phiên + min/max
 *   - SpO2 + SpO2 trung bình phiên + min/max
 *   - SQI hiện tại + SQI trung bình phiên
 *   - Waveform IR + RED (240 điểm) + peak indices
 *   - Các giá trị chẩn đoán: RAW, centered, filtered, DC/AC, ratio
 *   - Diagnostic counters: accepted/rejected peaks, dropped samples, FIFO overflows
 *   - Kết quả đã chốt (nếu finger-off đã xảy ra)
 *
 * Waveform xuất theo thứ tự thời gian (cũ nhất..mới nhất).
 * BPM trung bình phiên chỉ hợp lệ khi elapsed ≥ 10s và đủ 5 RR intervals.
 * SpO2 trung bình phiên chỉ hợp lệ khi ≥ 3 cửa sổ SpO2 hợp lệ và elapsed ≥ 10s.
 */
void Ppg_GetResult(PpgResult* out)
{
    if (out == 0) { return; }

    /* ---- Trạng thái engine ---- */
    out->state = s_state;
    out->reason = s_reason;
    out->fingerPresent = s_fingerPresent;
    out->signalStable = (s_state == PPG_STATE_MEASURING);
    out->waveformVisible = (s_state == PPG_STATE_MEASURING);

    /* BPM tức thời (median RR) — chỉ khi đang MEASURING */
    out->bpmValid = s_bpmValid && (s_state == PPG_STATE_MEASURING);
    out->bpm = out->bpmValid ? s_bpm : 0.0F;

    /* ---- Tiến trình ổn định ---- */
    if (s_state == PPG_STATE_STABILIZING)
    {
        /* Ước tính % theo số mẫu so với cửa sổ tối thiểu */
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

    /* ---- Thống kê phiên ---- */
    const uint32_t elapsed = (s_measuringStarted && (s_sessionLastMs >= s_sessionStartMs))
                                 ? (s_sessionLastMs - s_sessionStartMs) : 0U;
    out->elapsedMeasurementMs = elapsed;
    out->validRrCount = s_sessionRrCount;
    out->bpmMin = (s_sessionRrCount > 0U) ? s_sessionBpmMin : 0.0F;
    out->bpmMax = (s_sessionRrCount > 0U) ? s_sessionBpmMax : 0.0F;

    /* BPM trung bình phiên từ tổng RR intervals */
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

    /* ---- Giá trị tín hiệu (RAW / centered / filtered) ---- */
    out->redRaw = s_lastRedRaw;
    out->irRaw = s_lastIrRaw;
    out->redCentered = s_lastCenteredRed;
    out->irCentered = s_lastCenteredIr;
    out->redFiltered = s_lastRedFiltered;
    out->irFiltered = s_lastIrFiltered;

    /* ---- Diagnostic counters ---- */
    out->acceptedPeaks = s_acceptedPeaks;
    out->rejectedPeaks = s_rejectedPeaks;
    out->droppedSamples = s_droppedSamples;
    out->fifoOverflows = s_fifoOverflows;
    out->filterMode = s_filterMode;
    out->maWindow = s_maWindowN;

    /* ---- SpO2 + SQI ---- */
    const bool measuring = (s_state == PPG_STATE_MEASURING);
    out->spo2Valid = s_latestSpo2Valid && measuring;
    out->spo2 = out->spo2Valid ? s_latestSpo2 : 0.0F;

    /* SpO2 trung bình phiên */
    out->averageSpo2 = (s_sessionSpo2Count > 0U)
        ? (s_sessionSpo2Sum / (float)s_sessionSpo2Count) : 0.0F;
    out->averageSpo2Valid = (s_sessionSpo2Count >= MEASUREMENT_MIN_SPO2_WINDOWS) &&
                            (elapsed >= MEASUREMENT_MIN_DURATION_MS);
    out->spo2Min = (s_sessionSpo2Count > 0U) ? s_sessionSpo2Min : 0.0F;
    out->spo2Max = (s_sessionSpo2Count > 0U) ? s_sessionSpo2Max : 0.0F;
    out->validSpo2Windows = s_sessionSpo2Count;

    /* SQI */
    out->sqiPercent = measuring ? s_sqiPercent : 0.0F;
    out->averageSqi = (s_sessionSqiCount > 0U)
        ? (s_sessionSqiSum / (float)s_sessionSqiCount) : 0.0F;

    /* Giá trị chẩn đoán SpO2 */
    out->ratioOfRatios = s_lastRatio;
    out->dcRed = s_lastDcRed;
    out->dcIr = s_lastDcIr;
    out->acRed = s_lastAcRed;
    out->acIr = s_lastAcIr;

    /* ---- Kết quả đã chốt ---- */
    out->resultReady = s_resultReady;
    out->resultStatus = s_resultStatus;
    out->endReason = s_endReason;
    out->resultSaved = false;   /* DSP task ghi đè sau khi lưu lịch sử */

    /* ---- Waveform IR (cũ nhất..mới nhất) ---- */
    out->waveformCount = s_waveFill;
    out->peakCount = 0U;

    /* Xác định điểm bắt đầu đọc (cũ nhất) từ ring buffer */
    const uint16_t start = (s_waveFill < PPG_WAVE_POINTS)
                               ? 0U
                               : s_waveHead;

    for (uint16_t i = 0U; i < s_waveFill; ++i)
    {
        const uint16_t src = (uint16_t)((start + i) % PPG_WAVE_POINTS);
        out->waveform[i] = s_wave[src];

        /* Thu thập chỉ số peak (chỉ peak đã chấp nhận) */
        if ((s_peakFlag[src] != 0U) && (out->peakCount < PPG_MAX_PEAKS))
        {
            out->peakIndices[out->peakCount] = i;
            ++out->peakCount;
        }
    }
    /* Zeros padding cho phần còn lại */
    for (uint16_t i = s_waveFill; i < PPG_WAVE_POINTS; ++i)
    {
        out->waveform[i] = PPG_WAVE_ZERO;
    }

    /* ---- Waveform RED (cũ nhất..mới nhất) ---- */
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

    /* Không xuất waveform nếu không ở MEASURING */
    if (!out->waveformVisible)
    {
        out->waveformCount = 0U;
        out->peakCount = 0U;
    }
}
