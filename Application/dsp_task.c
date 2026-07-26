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
/* SEQLOCK PUBLISH PATTERN (một-ghi DSP thread, một-đọc GUI thread).           */
/*                                                                             */
/* Tại sao dùng seqlock thay vì mutex?                                        */
/*   - GUI thread chỉ ĐỌC kết quả, không cần block等待 khi DSP đang ghi.      */
/*   - Mutex có nguy cơ priority inversion: nếu GUI ưu tiên cao hơn DSP và   */
/*     giữ mutex, DSP (ưu tiên thấp hơn) bị block -> không nhận sample kịp.   */
/*   - Seqlock: reader (GUI) retry nếu thấy dữ liệu đang thay đổi, không bao  */
/*     giờ block, không lock contention. Chi phí retry thấp vì DSP viết rất   */
/*     nhanh (memcpy 1 PpgResult ≈ vài chục byte).                             */
/*                                                                             */
/* Cơ chế:                                                                     */
/*   s_pubGen là biến đếm thế hệ (generation counter):                        */
/*     - CHẴN = dữ liệu ổn định, reader đọc an toàn.                         */
/*     - LẺ   = DSP đang ghi, reader phải retry.                               */
/*                                                                             */
/*   Writer (DSP): s_pubGen++ -> lẻ, DMB, copy, DMB, s_pubGen++ -> chẵn      */
/*   Reader (GUI): g0=s_pubGen, DMB, copy, DMB, check g0==s_pubGen, retry nếu */
/*     không khớp hoặc g0 lẻ. Giới hạn 8 lần retry防止vòng lặp vô hạn.       */
/* -------------------------------------------------------------------------- */
static PpgResult s_pub;
/* Biến đếm thế hệ của seqlock. volatile保证编译器每次 từ RAM đọc, không cache
   trong thanh ghi. Giá trị chẵn = dữ liệu ổn định; lẻ = DSP đang ghi. */
static volatile uint32_t s_pubGen; /* chẵn = ổn định, lẻ = đang ghi */

/** Gửi (công bố) kết quả PPG mới nhất cho GUI thread đọc.
 *  Implement seqlock writer: tăng generation -> DMB -> copy -> DMB -> tăng lại.
 *  @param r Con trỏ tới PpgResult vừa tính xong từ engine PPG. */
static void publishResult(const PpgResult* r)
{
    s_pubGen++;              /* Bước 1: Tăng generation -> số LẺ.
                                Báo cho GUI reader rằng dữ liệu sắp bị ghi.
                                Nếu reader đang đọc, nó sẽ thấy số lẻ và retry. */
    __DMB();                 /* Bước 2: Data Memory Barrier.
                                Đảm bảo phép ghi s_pubGen (số lẻ) đã được
                                Cortex-M4 flush ra bộ nhớ VÀ hoàn tất TRƯỚC
                                khi bắt đầu phép ghi tiếp theo (copy s_pub).
                                Không có DMB: CPU có thể ghi s_pub trước s_pubGen
                                -> reader thấy số chẵn nhưng dữ liệu cũ! */
    s_pub = *r;              /* Bước 3: Copy toàn bộ PpgResult (~60-80 byte)
                                vào buffer công bố. Tại lúc này s_pubGen đang lẻ,
                                nên nếu GUI reader đang đọc nó sẽ retry. */
    __DMB();                 /* Bước 4: Data Memory Barrier.
                                Đảm bảo phép copy s_pub hoàn tất TRƯỚC khi
                                tăng s_pubGen về số chẵn. Nếu thiếu DMB,
                                Cortex-M4 có thể ghi s_pubGen trước khi copy
                                s_pub xong -> reader thấy chẵn nhưng dữ liệu
                                chưa hoàn chỉnh -> kết quả bị xé! */
    s_pubGen++;              /* Bước 5: Tăng generation -> số CHẴN.
                                Báo cho GUI reader rằng dữ liệu đã ổn định.
                                Mọi reader đang retry sẽ thấy chẵn và dừng. */
}

/** Đọc kết quả PPG đã công bố (dùng từ GUI thread).
 *  Implement seqlock reader: retry nếu dữ liệu đang bị ghi hoặc thay đổi.
 *  @param out Con trỏ tới PpgResult sẽ nhận snapshot không bị xé.
 *             Nếu NULL -> trả về ngay, không làm gì. */
void DspTask_GetResult(PpgResult* out)
{
    if (out == 0)            /* Guard: nếu con trỏ NULL -> thoát ngay.
                                Tránh crash do解引用 null pointer. */
    {
        return;
    }
    uint32_t g0;             /* Lưu giá trị generation BAN ĐẦU để so sánh
                                SAU khi copy. Nếu thay đổi -> dữ liệu bị xé
                                trong lúc copy -> phải retry. */
    uint32_t attempts = 0U;  /* Đếm số lần đọc thử. Giới hạn 8 lần防止
                                vòng lặp vô hạn: nếu DSP viết liên tục
                                (ví dụ DSP loop chạy nhanh hơn copy),
                                reader có thể retry mãi. Giới hạn 8 lần
                                ngăn trường hợp này. Trong thực tế,
                                2-3 lần retry là đủ vì DSP viết trong vài µs. */
    do
    {
        g0 = s_pubGen;       /* Đọc thế hệ hiện tại. Nếu g0 lẻ -> DSP đang
                                ghi -> sẽ retry ở điều kiện while. */
        __DMB();             /* Đảm bảo đọc s_pubGen xong TRƯỚC khi copy s_pub.
                                Nếu thiếu: CPU có thể copy s_pub TRƯỚC khi đọc
                                g0 -> phép so sánh sau sẽ sai. */
        *out = s_pub;        /* Copy toàn bộ PpgResult (~60-80 byte) về output.
                                memcpy cấu trúc: copy từng field từ buffer
                                công bố sang vùng nhớ của GUI. */
        __DMB();             /* Đảm bảo copy xong TRƯỚC khi đọc lại s_pubGen.
                                Nếu thiếu: CPU có thể đọc s_pubGen TRƯỚC khi
                                copy s_pub xong -> phép so sánh sẽ sai. */
        ++attempts;
    } while (((g0 & 1U) != 0U || g0 != s_pubGen) && (attempts < 8U));
    /* Điều kiện retry:                                                    */
    /*   (g0 & 1U) != 0U  -> g0 ban đầu đã lẻ, nghĩa là DSP đang ghi khi   */
    /*                       reader bắt đầu -> dữ liệu không tin cậy.      */
    /*   g0 != s_pubGen    -> generation thay đổi giữa lúc copy, nghĩa là   */
    /*                       DSP đã ghi xong rồi ghi bản mới trong lúc     */
    /*                       reader copy -> dữ liệu bị xé.                 */
    /*   attempts < 8U     -> giới hạn retry防止vòng lặp vô hạn nếu DSP     */
    /*                       viết liên tục (CPU DSP rất nhanh, viết vài µs). */
}

/* -------------------------------------------------------------------------- */
/* FILTER REQUEST PATTERN (GUI thread ghi yêu cầu, DSP thread áp dụng)        */
/*                                                                             */
/* Mô hình: GUI thread muốn thay đổi filter mode hoặc cửa sổ moving average.  */
/*   → Viết giá trị mới vào biến volatile.                                    */
/*   → DSP thread đọc và áp dụng ở đầu mỗi chu kỳ, sau đó đặt lại về -1.     */
/*                                                                             */
/* Tại sao dùng biến đơn lẻ thay vì mutex/queue?                              */
/*   - Chỉ cần ghi/đọc 1 biến int (4 byte) -> atomic trên Cortex-M4 (32-bit  */
/*     bus, aligned access). Không cần atomic instruction hay critical section. */
/*   - Không cần queue vì GUI chỉ muốn giá trị MỚI NHẤT (latest-wins).       */
/*     Nếu GUI ghi 2 lần liên tiếp, giá trị cũ bị bỏ qua -> DSP chỉ áp dụng  */
/*     giá trị cuối cùng.                                                      */
/*   - Giá trị -1 = "không có yêu cầu chờ" (sentinel value). Cortex-M4保证   */
/*     aligned 32-bit write/read là atomic, nên không cần lo race condition.   */
/*                                                                             */
/* Lưu ý: Nếu DSP đang đọc mà GUI ghi cùng lúc → có thể đọc sai. Nhưng trong  */
/*   thực tế, DSP đọc rất nhanh (< 1µs) và GUI ghi rất thưa → xác suất race  */
/*   cực thấp, và hậu quả chỉ là áp dụng sai 1 chu kỳ -> DSP sẽ đọc lại ở    */
/*   chu kỳ sau.                                                             */
/* -------------------------------------------------------------------------- */
static volatile int s_reqFilterMode = -1;  /* Yêu cầu filter mode mới từ GUI.
                                               -1 = không có yêu cầu chờ.
                                               Giá trị hợp lệ: PPG_FILTER_RAW
                                               hoặc PPG_FILTER_MOVING_AVERAGE. */
static volatile int s_reqMaWindow = -1;    /* Yêu cầu cửa sổ moving average mới
                                               từ GUI. -1 = không có yêu cầu chờ.
                                               Giá trị hợp lệ: 1..PPG_MA_WINDOW_MAX. */

/** GUI thread gọi hàm này để yêu cầu thay đổi filter mode.
 *  Giá trị được ghi vào biến volatile, DSP thread sẽ áp dụng ở chu kỳ tiếp.
 *  @param mode PPG_FILTER_RAW hoặc PPG_FILTER_MOVING_AVERAGE. */
void DspTask_SetFilterMode(PpgFilterMode mode)
{
    s_reqFilterMode = (int)mode;  /* Ghi nguyên tử (atomic write on Cortex-M4).
                                     DSP sẽ đọc và áp dụng ở đầu chu kỳ tiếp. */
}

/** GUI thread gọi hàm này để yêu cầu thay đổi cửa sổ moving average.
 *  Giá trị được ghi vào biến volatile, DSP thread sẽ áp dụng ở chu kỳ tiếp.
 *  @param window Số mẫu cửa sổ (1..PPG_MA_WINDOW_MAX). */
void DspTask_SetMaWindow(uint8_t window)
{
    s_reqMaWindow = (int)window;  /* Ghi nguyên tử (atomic write on Cortex-M4).
                                     DSP sẽ đọc và áp dụng ở đầu chu kỳ tiếp. */
}

/** Đọc và áp dụng các yêu cầu filter từ GUI thread.
 *  Được gọi ở ĐẦU mỗi chu kỳ DSP (trước khi xử lý sample).
 *  Logic: đọc giá trị -> nếu hợp lệ (>= 0) -> áp dụng -> đặt lại về -1.
 *  Nếu DSP không đọc mà GUI ghi liên tiếp -> giá trị cũ bị đè (latest-wins).
 *  Nếu DSP đọc mà GUI không ghi -> fm/mw âm -> không làm gì. */
static void applyPendingRequests(void)
{
    const int fm = s_reqFilterMode;  /* Đọc snapshot giá trị (atomic read).
                                        Lưu vào biến cục bộ để tránh TOCTOU:
                                        nếu đọc lại sau, giá trị có thể đã
                                        thay đổi. */
    if (fm >= 0)                     /* >= 0 nghĩa là có yêu cầu chờ (-1 =
                                        sentinel "không có yêu cầu"). */
    {
        Ppg_SetFilterMode((PpgFilterMode)fm);  /* Áp dụng filter mode mới cho
                                                   engine PPG. Chỉ DSP thread
                                                   gọi hàm này -> an toàn. */
        s_reqFilterMode = -1;        /* Đặt lại về -1 -> báo "đã xử lý xong".
                                        Nếu GUI ghi giá trị mới trong lúc xử lý,
                                        giá trị đó sẽ bị mất. Nhưng GUI chỉ ghi
                                        khi người dùng chạm nút -> xác suất thấp. */
    }
    const int mw = s_reqMaWindow;   /* Đọc snapshot tương tự cho cửa sổ MA. */
    if (mw >= 0)                     /* Có yêu cầu cửa sổ MA mới? */
    {
        Ppg_SetMaWindow((uint8_t)mw);  /* Áp dụng cửa sổ MA mới cho engine PPG. */
        s_reqMaWindow = -1;          /* Đặt lại về -1 -> đã xử lý xong. */
    }
}

/* -------------------------------------------------------------------------- */
/* Xử lý kết quả đã chốt (nhấc ngón tay -> bản ghi lịch sử)                     */
/* -------------------------------------------------------------------------- */
/* Store lịch sử tạm (TemporaryHistory) được GHI ở đây (DSP thread trong
   handleFinalize) và ĐỌC bởi GUI thread (hiển thị danh sách lịch sử).
   Mutex này tuần tự hóa TRUY CẬP để đảm bảo khi DSP đang ghi một bản ghi mới,
   GUI không đọc nửa chừng (partial read).
   Khác với seqlock (publishResult), ở đây dùng mutex vì:
   - GUI ĐỌC nhiều field trong một lần (duyệt danh sách) -> khó dùng seqlock.
   - History store có thể ghi nhiều bản ghi liên tiếp -> cần lock longer-held.
   - Không có priority inversion risk vì cả hai thread ưu tiên bằng nhau
     (osPriorityNormal). */
static osMutexId_t s_histMutex = NULL;

/** Khóa mutex history store (gọi từ GUI thread trước khi đọc).
 *  Blocking: chờ tối đa osWaitForever直到DSP thread giải phóng.
 *  Nếu mutex chưa được tạo (s_histMutex == NULL) -> bỏ qua (an toàn). */
void DspTask_HistoryLock(void)
{
    if (s_histMutex != NULL) { (void)osMutexAcquire(s_histMutex, osWaitForever); }
}

/** Mở khóa mutex history store (gọi từ GUI thread sau khi đọc xong). */
void DspTask_HistoryUnlock(void)
{
    if (s_histMutex != NULL) { (void)osMutexRelease(s_histMutex); }
}

/* -------------------------------------------------------------------------- */
/* STATE cho handleFinalize                                                    */
/* -------------------------------------------------------------------------- */
static DateTime s_startDt;       /* Thời điểm BẮT ĐẦU phiên đo. Được capture
                                     tại sample MEASURING đầu tiên (sau khi
                                     ngón tay được phát hiện và tín hiệu ổn định).
                                     Dùng để tính duration và lưu vào lịch sử. */
static bool s_startCaptured;     /* Cờ: đã capture thời điểm bắt đầu chưa?
                                     true = đã có startDt hợp lệ.
                                     false = reset về false khi ngón tay được nhấc
                                     (WAIT_FINGER/IDLE) để sẵn sàng cho phiên kế. */
static bool s_prevResultReady;   /* Giá trị resultReady của chu kỳ TRƯỚC.
                                     Dùng để phát hiện CẠNH LÊN (rising edge):
                                     resultReady=true VÀ s_prevResultReady=false.
                                     Chỉ finalize MỘT LẦN khi cạnh lên xảy ra. */
static bool s_resultSaved;       /* Cờ: kết quả đã được lưu vào TemporaryHistory
                                     chưa? Dùng để GUI biết có bản ghi mới.
                                     true = đã lưu thành công.
                                     false = chưa lưu hoặc không hợp lệ. */

/* Trạng thái cảnh báo + telemetry (khai báo ở đây vì handleFinalize dùng
   s_sessionId; phần cài đặt nằm ở mục "Cảnh báo y tế + telemetry" bên dưới). */
static PpgState s_prevTelemetryState = PPG_STATE_IDLE;  /* Trạng thái trước đó.
                                                             Dùng để phát hiện
                                                             chuyển đổi trạng thái
                                                             (ví dụ IDLE -> MEASURING)
                                                             và publish state event. */
static uint32_t s_prevAlertFlags = (uint32_t)MEDICAL_ALERT_NONE;  /* Cờ cảnh báo
                                                                      trước đó.
                                                                      Dùng để phát
                                                                      hiện thay đổi
                                                                      (bật/tắt) và
                                                                      publish alert
                                                                      event. */
static uint32_t s_sessionId = 0U;  /* ID phiên đo hiện tại. Tăng 1 khi chuyển
                                       từ trạng thái khác sang MEASURING.
                                       Dùng trong telemetry để liên kết dữ liệu
                                       với phiên đo cụ thể. */
static uint32_t s_vitalDivider = 0U;  /* Bộ đếm cho vital summary ~1Hz.
                                          Mỗi 100 chu kỳ (100 x 10ms = 1s)
                                          publish BPM/SpO2 summary một lần. */
static uint32_t s_sampleSeq = 0U;     /* Số thứ tự sample (sequence number).
                                          Tăng mỗi lần publish sample waveform.
                                          Dùng để GUI/PC kiểm tra missing samples. */

/** Ghi lại timestamp phiên và, ở cạnh lên của finger-off, xây và lưu một bản ghi
    lịch sử đúng một lần (§24, §43). Ghi đè resultSaved cho GUI.

    LUỒNG CHÍNH:
    1. Khi state == MEASURING và chưa capture startDt -> chụp thời gian bắt đầu.
    2. Khi ngón tay được nhấc (WAIT_FINGER/IDLE) -> reset s_startCaptured.
    3. Cạnh LÊN của resultReady (từ false -> true):
       a. Nếu kết quả HỢP LỆ -> xây MeasurementHistoryRecord, lưu vào
          TemporaryHistory, phát âm báo "xong", publish telemetry.
       b. Nếu kết quả KHÔNG HỢP LỆ -> phát âm báo "không hợp lệ".
       c. Luôn publish session summary và reset MedicalAlert.
    4. Ghi đè r->resultSaved để GUI thread biết kết quả đã được lưu.
       (GUI đọc resultSaved trong snapshot để hiển thị icon "đã lưu" hoặc "mới".) */
static void handleFinalize(PpgResult* r)
{
    /* === PHẦN 1: Capture thời điểm bắt đầu phiên đo === */
    /* Tại sample MEASURING ĐẦU TIÊN (ngón tay vừa được phát hiện,
       tín hiệu vừa ổn định), chụp thời gian từ RTC.
       s_startCaptured = true đảm bảo chỉ capture MỘT LẦN mỗi phiên. */
    if ((r->state == PPG_STATE_MEASURING) && !s_startCaptured)
    {
        bool v = false;       /* Biến phụ cho RTC: unused but required by API. */
        RtcService_GetSnapshot(&s_startDt, &v);  /* Lấy thời gian hiện tại từ RTC
                                                     module và lưu vào s_startDt.
                                                     Dùng làm "thời điểm bắt đầu" cho
                                                     bản ghi lịch sử. */
        s_startCaptured = true;  /* Đánh dấu đã capture. Các sample MEASURING
                                     tiếp theo sẽ KHÔNG ghi đè startDt. */
    }
    /* Khi ngón tay được nhấc (chuyển sang WAIT_FINGER hoặc IDLE) -> reset cờ.
       Chuẩn bị cho phiên đo kế tiếp: lần MEASURING tiếp theo sẽ capture lại
       thời gian bắt đầu mới. */
    if ((r->state == PPG_STATE_WAIT_FINGER) || (r->state == PPG_STATE_IDLE))
    {
        s_startCaptured = false;   /* sẵn sàng cho phiên kế tiếp */
    }

    /* === PHẦN 2: Finalize khi kết quả đo sẵn sàng (cạnh lên resultReady) === */
    /* resultReady chuyển từ false -> true (rising edge) nghĩa là engine PPG
       đã hoàn thành một phiên đo và kết quả đã sẵn sàng.
       Chỉ finalize ĐÚNG MỘT LẦN cho mỗi phiên (dùng edge detection). */
    if (r->resultReady && !s_prevResultReady)
    {
        s_resultSaved = false;  /* Reset cờ trước khi bắt đầu xử lý.
                                    Nếu lưu thất bại, sẽ giữ false. */
        DateTime endDt;         /* Thời gian KẾT THÚC phiên đo. */
        bool ev = false;        /* Biến phụ cho RTC event: unused. */
        RtcService_GetSnapshot(&endDt, &ev);  /* Chụp thời gian hiện tại làm
                                                  endDateTime cho bản ghi lịch sử. */

        /* === PHẦN 2a: Kết quả HỢP LỆ -> lưu vào lịch sử === */
        if (r->resultStatus != MEASUREMENT_RESULT_INVALID)
        {
            /* Xây dựng bản ghi lịch sử phiên đo từ các field của PpgResult.
               Đổ đầy từng field: start/end datetime, BPM min/avg/max,
               SpO2 min/avg/max, SQI, số peak accepted/rejected, v.v.
               {0} đảm bảo các field không được gán sẽ bằng 0. */
            MeasurementHistoryRecord rec = {0};
            rec.startDateTime = s_startCaptured ? s_startDt : endDt;
            /* Nếu startDt đã được capture (bình thường) -> dùng s_startDt.
               Nếu chưa (edge case: MEASURING ngay khi bắt đầu) -> dùng endDt
               làm fallback, tránh giá trị garbage. */
            rec.endDateTime = endDt;
            rec.durationMs = r->elapsedMeasurementMs;  /* Thời gian đo (ms) */
            rec.averageBpm = r->averageBpm;    /* BPM trung bình toàn phiên */
            rec.minimumBpm = r->bpmMin;        /* BPM thấp nhất */
            rec.maximumBpm = r->bpmMax;        /* BPM cao nhất */
            rec.averageSpo2 = r->averageSpo2;  /* SpO2 trung bình (%) */
            rec.minimumSpo2 = r->spo2Min;      /* SpO2 thấp nhất */
            rec.maximumSpo2 = r->spo2Max;      /* SpO2 cao nhất */
            rec.averageSqi = r->averageSqi;    /* SQI trung bình (chất lượng tín hiệu) */
            rec.acceptedPeakCount = r->acceptedPeaks;  /* Số đỉnh mạch được chấp nhận */
            rec.rejectedPeakCount = r->rejectedPeaks;  /* Số đỉnh mạch bị loại */
            rec.droppedSampleCount = r->droppedSamples;  /* Số sample bị mất */
            rec.fifoOverflowCount = r->fifoOverflows;    /* Số lần FIFO bị tràn */
            rec.bpmValid = r->averageBpmValid;    /* BPM trung bình có hợp lệ? */
            rec.spo2Valid = r->averageSpo2Valid;  /* SpO2 trung bình có hợp lệ? */
            rec.status = r->resultStatus;    /* Trạng thái kết quả (OK, INVALID, v.v.) */
            rec.endReason = r->endReason;    /* Lý do kết thúc (finger removed, timeout, v.v.) */

            /* Lưu vào TemporaryHistory: cần lock mutex vì GUI thread có thể
               đang đọc danh sách lịch sử cùng lúc.
               DSP thread GHI, GUI thread ĐỌC -> cần tuần tự hóa. */
            DspTask_HistoryLock();
            const HistoryStatus hs = TemporaryHistory_Add(&rec);  /* Thêm bản ghi mới
                                                                      vào store tạm.
                                                                      Trả về OK hoặc lỗi
                                                                      (store đầy). */
            DspTask_HistoryUnlock();

            if (hs == HISTORY_STATUS_OK)
            {
                s_resultSaved = true;  /* Đánh dấu đã lưu thành công.
                                            GUI sẽ thấy resultSaved = true trong snapshot
                                            và hiển thị icon "đã lưu". */
                /* Phát âm báo "đo xong" -> nhạc hiệu ngắn vui tai.
                   Buzzer_PlayMelody là non-blocking: bắt đầu phát và trả về ngay. */
                (void)Buzzer_PlayMelody(BUZZER_MELODY_DONE, BUZZER_MELODY_DONE_LEN);
                /* Publish bản ghi lịch sử qua telemetry để máy tính nhận được
                   toàn bộ dữ liệu phiên đo. s_sessionId giúp PC liên kết
                   bản ghi với phiên đo cụ thể. */
                (void)Telemetry_PublishHistoryRecord(s_sessionId, &rec);
            }
        }
        else
        {
            /* Kết quả KHÔNG HỢP LỆ: đo quá ngắn, không đủ peak, tín hiệu xấu.
               Không lưu vào TemporaryHistory.
               Phát âm báo "không hợp lệ" (âm báo khác với "xong"). */
            (void)Buzzer_PlayMelody(BUZZER_MELODY_INVALID, BUZZER_MELODY_INVALID_LEN);
        }

        /* === PHẦN 2b: Publish session summary cho PC/telemetry === */
        /* Gửi tổng kết phiên đo bất kể kết quả hợp lệ hay không.
           PC có thể dùng dữ liệu này để thống kê, phân tích. */
        TelemetrySessionSummary sum;
        sum.sessionId   = s_sessionId;           /* ID phiên đo hiện tại */
        sum.durationMs  = r->elapsedMeasurementMs;  /* Tổng thời gian đo (ms) */
        sum.averageBpm  = r->averageBpm;         /* BPM trung bình */
        sum.averageSpo2 = r->averageSpo2;        /* SpO2 trung bình */
        sum.status      = r->resultStatus;       /* Trạng thái kết quả */
        sum.endReason   = r->endReason;          /* Lý do kết thúc */
        (void)Telemetry_PublishSessionEnd(&sum);  /* Publish non-blocking:
                                                      TelemetryTask sẽ gửi
                                                      qua UART khi rảnh. */

        /* === PHẦN 2c: Reset MedicalAlert khi nhấc ngón tay === */
        /* Nhấc ngón tay -> xóa ngay cờ cảnh báo y tế.
           Không giữ LED theo giá trị BPM/SpO2 cũ (sai thực tế).
           Sensor task sẽ tắt LED ở vòng kế tiếp (~20 ms). */
        MedicalAlert_Reset();  /* Xóa mọi cờ đang active: BPM_LOW, BPM_HIGH,
                                   SPO2_LOW -> tất cả về NONE. LED sẽ tắt. */
    }
    /* Nếu resultReady chuyển về false (giữa các phiên) -> reset s_resultSaved.
       Đảm bảo GUI không hiển thị "đã lưu" cho phiên cũ. */
    if (!r->resultReady)
    {
        s_resultSaved = false;
    }
    /* Lưu trạng thái resultReady của chu kỳ này để so sánh ở chu kỳ sau.
       Dùng cho edge detection ở lần gọi tiếp theo. */
    s_prevResultReady = r->resultReady;
    /* GHI ĐÈ r->resultSaved: cho phép GUI thread đọc giá trị này qua
       seqlock snapshot. GUI sẽ thấy resultSaved = true nếu kết quả đã
       được lưu vào TemporaryHistory, hoặc false nếu chưa.
       Dùng để hiển thị icon "đã lưu" hoặc trạng thái "mới". */
    r->resultSaved = s_resultSaved;   /* ghi đè cho snapshot GUI */
}

/* -------------------------------------------------------------------------- */
/* CẢNH BÁO Y TẾ + TELEMETRY                                                  */
/*                                                                             */
/* DSP thread là chủ sở hữu kết quả đo, nên nó thực hiện:                     */
/*   1. MedicalAlert_Update(): feed BPM/SpO2/validity vào bộ đánh giá ngưỡng. */
/*   2. Publish telemetry: state events, vital summaries, waveform samples.    */
/*                                                                             */
/* DSP thread KHÔNG:                                                          */
/*   - Chạm GPIO (LED do sensor task nháy theo cờ cảnh báo).                  */
/*   - Gọi UART (TelemetryTask sở hữu USART1, mọi publish đều non-blocking). */
/*                                                                             */
/* Phân công:                                                                  */
/*   DSP thread -> ghi dữ liệu vào ring buffer telemetry (non-blocking).      */
/*   TelemetryTask -> đọc ring buffer và gửi qua UART (blocking nhưng riêng). */
/*   Sensor task -> đọc cờ MedicalAlert và nháy LED tương ứng.                */
/* -------------------------------------------------------------------------- */
/* Định nghĩa khoảng thời gian publish vital summary: 100 chu kỳ x 10ms = 1s.
   Tại sao ~1Hz? Vì BPM/SpO2 thay đổi chậm (giọt máu mỗi 0.5-1s).
   Publish mỗi giây đủ để GUI cập nhật mà không gây quá tải UART. */
#define DSP_VITAL_PERIOD_TICKS  100U   /* ~1 Hz ở nhịp 10 ms */

/** Publish một sample waveform PPG qua telemetry.
 *  Chỉ được gọi khi streamAllowed = true (fingerPresent + signalStable + MEASURING).
 *  Telemetry service sẽ tự chia tần (downsample) trước khi gửi qua UART,
 *  nên dù DSP chạy 100Hz, UART chỉ nhận khoảng 25-50 sample/giây.
 *  @param r Con trỏ tới PpgResult chứa giá trị thô và đã lọc. */
static void publishSample(const PpgResult* r)
{
    TelemetryPpgSample s;            /* Cấu trúc sample cần gửi. */
    s.sequence     = s_sampleSeq++;  /* Số thứ tự tăng dần. Dùng để PC kiểm tra
                                         missing samples: nếu sequence nhảy từ
                                         10 -> 13, nghĩa là 2 sample bị mất. */
    s.redRaw       = r->redRaw;       /* Giá trị đỏ thô từ cảm biến (ADC raw) */
    s.irRaw        = r->irRaw;        /* Giá trị hồng ngoại thô từ cảm biến */
    s.redCentered  = r->redCentered;  /* Red đã centering (bỏ DC offset) */
    s.irCentered   = r->irCentered;   /* IR đã centering (bỏ DC offset) */
    s.redFiltered  = r->redFiltered;  /* Red đã qua filter (MA hoặc raw) */
    s.irFiltered   = r->irFiltered;   /* IR đã qua filter */
    s.bpm          = r->bpm;          /* BPM tức thì (từ peak detection) */
    s.spo2         = r->spo2;         /* SpO2 tức thì (từ ratio R) */
    s.sqi          = r->sqiPercent;   /* Signal Quality Index (0-100%) */
    s.state        = r->state;        /* Trạng thái hiện tại của engine PPG */
    s.filterMode   = r->filterMode;   /* Filter mode đang dùng (RAW/MA) */
    s.maWindow     = r->maWindow;     /* Cửa sổ MA đang dùng */
    s.bpmValid     = r->bpmValid;     /* BPM có hợp lệ không (đã detect đủ peak?) */
    s.spo2Valid    = r->spo2Valid;    /* SpO2 có hợp lệ không? */
    (void)Telemetry_PublishPpgSample(&s);  /* Đẩy vào ring buffer (non-blocking).
                                               TelemetryTask sẽ gửi qua UART. */
}

/** Publish alert event khi một cờ cảnh báo BẬT hoặc TẮT.
 *  Chỉ publish khi có thay đổi (không publish liên tục khi LED nháy).
 *  Dùng XOR để tìm bits thay đổi giữa chu kỳ hiện tại và trước.
 *  @param r Con trỏ PpgResult (dùng để lấy giá trị BPM/SpO2 hiện tại).
 *  @param nowFlags bitmask cờ cảnh báo đang active từ MedicalAlert_GetActiveFlags(). */
static void publishAlertChanges(const PpgResult* r, uint32_t nowFlags)
{
    /* XOR: bit = 1 nếu cờ đã thay đổi (bật->tắt hoặc tắt->bật).
       Nếu changed == 0 -> không có gì thay đổi -> thoát ngay. */
    const uint32_t changed = nowFlags ^ s_prevAlertFlags;
    if (changed == 0U)
    {
        return;  /* Không có thay đổi cảnh báo -> không publish gì. */
    }
    /* 3 loại cảnh báo cần theo dõi: BPM thấp, BPM cao, SpO2 thấp. */
    const uint32_t bits[3] = { (uint32_t)MEDICAL_ALERT_BPM_LOW,
                               (uint32_t)MEDICAL_ALERT_BPM_HIGH,
                               (uint32_t)MEDICAL_ALERT_SPO2_LOW };
    /* Giá trị hiện tại tương ứng với mỗi loại cảnh báo. */
    const float values[3]     = { r->bpm, r->bpm, r->spo2 };
    /* Ngưỡng kích hoạt từ alert_config.h. */
    const float thresholds[3] = { ALERT_BPM_LOW_THRESHOLD,
                                  ALERT_BPM_HIGH_THRESHOLD,
                                  ALERT_SPO2_LOW_THRESHOLD };
    /* Duyệt qua từng loại cảnh báo: nếu bit tương ứng THAY ĐỔI -> publish. */
    for (uint8_t i = 0U; i < 3U; ++i)
    {
        if ((changed & bits[i]) != 0U)  /* Bit này đã thay đổi? */
        {
            TelemetryAlertEvent e;
            e.flag      = bits[i];       /* Loại cảnh báo nào (BPM_LOW, v.v.) */
            e.value     = values[i];     /* Giá trị hiện tại (BPM hoặc SpO2) */
            e.threshold = thresholds[i]; /* Ngưỡng kích hoạt (để PC log/hiển thị) */
            e.active    = ((nowFlags & bits[i]) != 0U);  /* true = vừa bật,
                                                              false = vừa tắt */
            (void)Telemetry_PublishAlert(&e);  /* Đẩy vào ring buffer (non-blocking). */
        }
    }
    s_prevAlertFlags = nowFlags;  /* Lưu flags hiện tại để so sánh ở chu kỳ sau. */
}

/** Cập nhật cảnh báo y tế + phát telemetry cho một kết quả PPG mới.
 *  Được gọi MỖI chu kỳ DSP (10ms) sau khi có kết quả mới từ engine.
 *  Thực hiện 3 việc chính:
 *    1. Phát state events khi trạng thái engine thay đổi.
 *    2. Đánh giá ngưỡng cảnh báo y tế (BPM/SpO2).
 *    3. Stream waveform data và vital summary (khi được phép).
 *  @param r Con trỏ tới PpgResult vừa được tính từ engine PPG. */
static void updateAlertsAndTelemetry(const PpgResult* r)
{
    /* === PHẦN 1: Quản lý phiên đo và state events === */
    /* Nếu trạng thái engine thay đổi (ví dụ IDLE -> MEASURING, hay
       MEASURING -> WAIT_FINGER), publish event cho PC.
       Đặc biệt: khi chuyển VÀO MEASURING từ trạng thái khác -> phiên mới,
       tăng s_sessionId và publish SESSION_START. */
    if (r->state != s_prevTelemetryState)  /* Có thay đổi trạng thái? */
    {
        /* Chỉ tạo phiên mới khi CHUYỂN TỪ trạng thái khác SANG MEASURING.
           Điều này xảy ra khi: ngón tay được phát hiện, tín hiệu ổn định,
           và engine bắt đầu đo BPM/SpO2. */
        if ((r->state == PPG_STATE_MEASURING) &&
            (s_prevTelemetryState != PPG_STATE_MEASURING))
        {
            ++s_sessionId;  /* Tăng ID phiên: mỗi phiên đo có ID riêng.
                                 Dùng trong telemetry để liên kết dữ liệu
                                 (sample, vital, history) với phiên cụ thể. */
            (void)Telemetry_PublishSessionStart(s_sessionId);  /* Báo cho PC:
                                                    "phiên đo mới bắt đầu". */
        }
        /* Publish state hiện tại: PC dùng để hiển thị tiến trình
           (IDLE -> WAIT_FINGER -> STABILIZING -> MEASURING). */
        (void)Telemetry_PublishMeasurementState(r->state);
        s_prevTelemetryState = r->state;  /* Lưu lại để so sánh chu kỳ sau. */
    }

    /* === PHẦN 2: Đánh giá ngưỡng cảnh báo y tế === */
    /* Gọi MedicalAlert_Update với dữ liệu hiện tại: BPM, SpO2, và trạng thái.
       MedicalAlert service sẽ so sánh với ngưỡng và cập nhật cờ:
         - MEDICAL_ALERT_BPM_LOW:  BPM < ALERT_BPM_LOW_THRESHOLD
         - MEDICAL_ALERT_BPM_HIGH: BPM > ALERT_BPM_HIGH_THRESHOLD
         - MEDICAL_ALERT_SPO2_LOW: SpO2 < ALERT_SPO2_LOW_THRESHOLD
       Chỉ kích hoạt khi: state == MEASURING + signalStable + metric hợp lệ.
       (MediAlert internally kiểm tra các điều kiện này.) */
    MedicalMeasurementUpdate u;          /* Cấu trúc đầu vào cho MedicalAlert. */
    u.currentBpm       = r->bpm;        /* BPM tức thì (từ peak detection) */
    u.currentSpo2      = r->spo2;       /* SpO2 tức thì (từ ratio R) */
    u.bpmValid         = r->bpmValid;   /* BPM có hợp lệ không? */
    u.spo2Valid        = r->spo2Valid;  /* SpO2 có hợp lệ không? */
    u.signalValid      = r->signalStable;  /* Tín hiệu có ổn định không?
                                                (ảnh hưởng đến quyết định alert) */
    u.measurementState = r->state;      /* Trạng thái engine hiện tại.
                                                (chỉ alert khi MEASURING) */
    u.timestampMs      = HAL_GetTick(); /* Thời gian hiện tại (ms từ boot).
                                                Dùng cho timeout logic trong alert. */
    MedicalAlert_Update(&u);            /* Cập nhật cờ cảnh báo (non-blocking). */
    /* So sánh cờ cảnh báo mới với cũ -> publish event khi CÓ THAY ĐỔI.
       Chỉ gửi ALERT_ON hoặc ALERT_OFF, không gửi liên tục mỗi chu kỳ. */
    publishAlertChanges(r, (uint32_t)MedicalAlert_GetActiveFlags());

    /* === PHẦN 3: Stream waveform data và vital summary === */
    /* Điều kiện stream: PHẢI ĐỦ CẢ BA:
       1. fingerPresent  -> ngón tay đang đặt trên cảm biến.
       2. signalStable   -> tín hiệu đã ổn định (không phải đang stabilizing).
       3. state == MEASURING -> đang trong giai đoạn đo thực sự.
       Nếu thiếu BẤT KỲ điều kiện nào -> KHÔNG gửi waveform/vital.
       Lý do: TRƯỚC khi ổn định (WAIT_FINGER/STABILIZING), dữ liệu RED/IR
       chưa có ý nghĩa y tế. Khi MẤT ỔN ĐỊNH giữa chừng, state sẽ chuyển
       sang trạng thái khác ở chu kỳ tiếp theo -> stream tự dừng.
       Lưu ý: event/state/alert vẫn được gửi ở Phần 1 và 2 ở trên. */
    const bool streamAllowed = r->fingerPresent && r->signalStable &&
                               (r->state == PPG_STATE_MEASURING);
    if (streamAllowed)
    {
        /* Gửi waveform sample: RED raw, IR raw, RED filtered, IR filtered,
           BPM tức thì, SpO2 tức thì. Telemetry service tự downsample
           trước khi gửi UART (DSP 100Hz, UART ~25-50 Hz). */
        publishSample(r);

        /* BPM/SpO2 TÓM TẮT ~1 Hz: mỗi 100 chu kỳ x 10ms = 1 giây.
           Gửi BPM trung bình, SpO2 trung bình, SQI.
           Tại sao tách riêng? Vì vital summary gửi BPM/SpO2 TRUNG BÌNH
           (average), còn waveform sample gửi BPM/SpO2 TỨC THÌ (instant).
           PC dùng vital summary để hiển thị giá trị ổn định trên dashboard. */
        if (++s_vitalDivider >= DSP_VITAL_PERIOD_TICKS)
        {
            s_vitalDivider = 0U;  /* Reset bộ đếm, bắt đầu đếm lại. */
            TelemetryVitalResult v;   /* Cấu trúc vital summary. */
            v.bpm         = r->bpm;           /* BPM tức thì (hiển thị realtime) */
            v.averageBpm  = r->averageBpm;    /* BPM trung bình toàn phiên */
            v.spo2        = r->spo2;          /* SpO2 tức thì */
            v.averageSpo2 = r->averageSpo2;   /* SpO2 trung bình toàn phiên */
            v.sqi         = r->sqiPercent;    /* Chất lượng tín hiệu (%) */
            v.state       = r->state;         /* Trạng thái engine hiện tại */
            v.bpmValid    = r->bpmValid;      /* BPM có hợp lệ? */
            v.spo2Valid   = r->spo2Valid;     /* SpO2 có hợp lệ? */
            (void)Telemetry_PublishVitalResult(&v);  /* Đẩy vào ring buffer. */
        }
    }
    else
    {
        /* Không stream: reset bộ đếm vital.
           Để lần đo SAU bắt đầu clean: nếu không reset, bộ đếm có thể
           đã gần 100 -> lần đo sau sẽ publish vital ngay lập tức
           thay vì chờ đủ 1 giây. */
        s_vitalDivider = 0U;   /* reset nhịp vital để lần đo sau bắt đầu sạch */
    }
}

/* -------------------------------------------------------------------------- */
/* VÒNG LẶP CHÍNH CỦA DSP THREAD                                              */
/*                                                                             */
/* Chạy liên tục (for (;;) ) với nhịp 10ms (~100 Hz).                        */
/* Mỗi chu kỳ thực hiện:                                                      */
/*   1. applyPendingRequests  - Áp dụng filter request từ GUI                 */
/*   2. Ppg_SetSensorError    - Báo lỗi sensor (nếu có)                       */
/*   3. Ppg_ReportLoss        - Báo sample bị mất/FIFO overflow               */
/*   4. Drain queue           - Rút tối đa 512 sample từ queue lock-free      */
/*   5. Ppg_GetResult         - Lấy kết quả từ engine PPG                     */
/*   6. handleFinalize        - Lưu lịch sử nếu phiên đo xong                 */
/*   7. publishResult         - Công bố kết quả cho GUI qua seqlock            */
/*   8. updateAlerts...       - Cập nhật cảnh báo + telemetry                  */
/*   9. osDelay(10)           - Nghỉ 10ms, nhường CPU cho task khác           */
/* -------------------------------------------------------------------------- */
#define DSP_TASK_PERIOD_MS   10U   /* Nhịp rút + công bố: 10ms ≈ 100 Hz.
                                       DSP engine yêu cầu sample mỗi 10ms
                                       (100 Hz sampling rate của MAX30102). */
#define DSP_DRAIN_GUARD      512U  /* Giới hạn số sample xử lý mỗi lần thức.
                                       Tại sao 512? Queue lock-free có thể tích
                                       tụ nhiều sample khi DSP bị delay (do mutex
                                       hoặc higher priority task). Giới hạn防止
                                       DSP bị "chết đuối" trong hàng đợi.
                                       512 sample = ~5 giây data ở 100 Hz.
                                       Nếu queue nhiều hơn -> sẽ xử lý ở chu kỳ sau. */

/** Vòng lặp chính của DSP thread. Chạy liên tục, không bao giờ thoát.
 *  Mỗi chu kỳ: drain queue -> tính toán -> publish -> delay. */
static void dspLoop(void)
{
    /* === KHỞI TẠO MỘT LẦN === */
    Ppg_Init();                   /* Khởi tạo engine PPG: thiết lập filter,
                                       buffer, peak detection, SpO2 calculator.
                                       Gọi MỘT LẦN khi thread bắt đầu. */
    (void)TemporaryHistory_Init();  /* Khởi tạo store lịch sử tạm: cấp phát
                                       bộ nhớ, đặt con trỏ đầu danh sách.
                                       Trả về HISTORY_STATUS_OK hoặc lỗi
                                       (bỏ qua vì đã cấp phát tĩnh). */

    /* Theo dõi sample bị mất/FIFO overflow từ chu kỳ trước.
       Mỗi chu kỳ: đếm mới - đếm cũ = số mất trong chu kỳ này. */
    uint32_t lastDropped = 0U;   /* Số sample bị mất tích lũy (chu kỳ trước). */
    uint32_t lastOverflow = 0U;  /* Số lần FIFO overflow tích lũy (chu kỳ trước). */

    /* Công bố kết quả IDLE ban đầu: GUI cần có dữ liệu hợp lệ ngay khi
       mở ứng dụng (trước khi người dùng đặt ngón tay).
       Nếu không publish, GUI sẽ đọc PpgResult rỗng/garbage. */
    PpgResult r;                  /* Biến PpgResult cục bộ: được tái sử dụng
                                       trong mỗi chu kỳ để tránh cấp phát stack
                                       liên tục (cấu trúc khá lớn ~60-80 byte). */
    Ppg_GetResult(&r);            /* Lấy kết quả hiện tại từ engine (ban đầu = IDLE). */
    publishResult(&r);            /* Publish cho GUI qua seqlock. GUI sẽ đọc
                                       được PpgResult ở trạng thái IDLE. */

    /* === VÒNG LẶP VÔ HẠN === */
    for (;;)
    {
        /* BƯỚC 1: Áp dụng yêu cầu filter từ GUI thread.
           Nếu GUI đã gửi yêu cầu thay đổi filter mode hoặc cửa sổ MA,
           áp dụng NGAY ở đầu chu kỳ trước khi xử lý sample.
           Đảm bảo sample đầu tiên của phiên đo mới đã dùng filter đúng. */
        applyPendingRequests();

        /* BƯỚC 2: Báo lỗi sensor cho engine PPG.
           g_sensorOk == 0 nghĩa là sensor MAX30102 không phản hồi (I2C error).
           Engine PPG sẽ đánh dấu kết quả INVALID nếu sensor lỗi. */
        Ppg_SetSensorError(g_sensorOk == 0);

        /* BƯỚC 3: Báo cáo sample bị mất và FIFO overflow.
           PpgQueue_DroppedCount(): tổng số sample bị mất từ khi khởi động.
           g_fifoOverflowTotal: tổng số lần FIFO ADC bị tràn.
           Mỗi chu kỳ: gửi DELTA (mất trong chu kỳ này) cho engine.
           Engine dùng thông tin này để điều chỉnh peak detection
           và đánh giá chất lượng tín hiệu. */
        const uint32_t dropped = PpgQueue_DroppedCount();
        const uint32_t overflow = g_fifoOverflowTotal;
        Ppg_ReportLoss(dropped - lastDropped, overflow - lastOverflow);
        lastDropped = dropped;
        lastOverflow = overflow;

        /* BƯỚC 4: Rút hàng đợi sample (drain queue).
           Queue lock-free (PpgQueue) được sensor task đẩy sample vào.
           DSP thread pop và đẩy vào engine PPG.
           Giới hạn 512 sample mỗi chu kỳ防止CPU bị chiếm quá lâu
           (nếu queue tích tụ nhiều sample do DSP bị delay). */
        PpgRawSample s;          /* Biến暂lưu sample thô từ queue. */
        uint32_t guard = 0U;     /* Bộ đếm: đã pop bao nhiêu sample. */
        while ((guard < DSP_DRAIN_GUARD) && PpgQueue_Pop(&s))
        {
            /* Pop thành công: đẩy sample vào engine PPG để xử lý.
               Engine sẽ: apply filter -> peak detection -> tính BPM -> tính SpO2.
               Ppg_PushSample là non-blocking: xử lý ngay trong call. */
            Ppg_PushSample(&s);
            ++guard;             /* Tăng bộ đếm, kiểm tra giới hạn ở vòng while. */
        }
        /* Sau khi drain: engine đã có kết quả mới nhất.
           guard < 512 && !PpgQueue_Pop: queue trống (bình thường).
           guard == 512: queue vẫn còn sample -> sẽ xử lý ở chu kỳ sau. */

        /* BƯỚC 5: Lấy kết quả mới nhất từ engine PPG.
           Kết quả bao gồm: state, BPM, SpO2, SQI, waveform data, v.v.
           r được truyền qua handleFinalize, publishResult, updateAlerts. */
        Ppg_GetResult(&r);

        /* BƯỚC 6: Xử lý finalize (lưu lịch sử khi phiên đo xong).
           Kiểm tra resultReady rising edge: nếu vừa chốt -> lưu bản ghi
           lịch sử, phát âm báo, publish session summary.
           Ghi đè r->resultSaved cho GUI snapshot. */
        handleFinalize(&r);

        /* BƯỚC 7: Công bố kết quả cho GUI thread qua seqlock.
           GUI thread sẽ đọc snapshot không bị xé.
           publishResult: s_pubGen++ -> DMB -> copy -> DMB -> s_pubGen++ */
        publishResult(&r);

        /* BƯỚC 8: Cập nhật cảnh báo y tế + publish telemetry.
           Đánh giá ngưỡng BPM/SpO2, publish state events, stream waveform. */
        updateAlertsAndTelemetry(&r);

        /* BƯỚC 9: Nghỉ 10ms, nhường CPU cho task khác.
           osDelay: FreeRTOS.tick-based delay, chính xác ~10ms.
           Trong 10ms này: sensor task sẽ đẩy sample mới vào queue,
           GUI task sẽ render TouchGFX, TelemetryTask sẽ gửi UART. */
        osDelay(DSP_TASK_PERIOD_MS);
    }
}

/** Entry point của DSP thread. Được CMSIS-RTOS2 gọi khi thread bắt đầu.
 *  Argument không dùng (đã cast void). Gọi dspLoop() để chạy vòng lặp chính.
 *  dspLoop() không bao giờ return -> thread chạy mãi đến khi hệ thống reset. */
static void dspThreadEntry(void* argument)
{
    (void)argument;  /* Không dùng argument, cast void để tránh warning. */
    dspLoop();       /* Vòng lặp vô hạn: không bao giờ return. */
}

/* -------------------------------------------------------------------------- */
/* CẤP PHÁT TĨNH CHO DSP THREAD                                               */
/*                                                                             */
/* configSUPPORT_STATIC_ALLOCATION = 1: tất cả memory cho thread được cấp      */
/* phát tĩnh (static arrays), KHÔNG dùng FreeRTOS heap.                       */
/* Ưu điểm:                                                                   */
/*   - Không có fragmentation (ổn định bộ nhớ trong hệ thống nhúng).          */
/*   - Dễ debug: biết chính xác bao nhiêu RAM dùng cho stack.                 */
/*   - Không cần heap cho task creation -> tránh malloc fail ở runtime.        */
/*                                                                             */
/* StaticTask_t: FreeRTOS cần cấu trúc này để quản lý thread (TCB - Task      */
/* Control Block). Kích thước ~80-120 byte tùy cấu hình FreeRTOS.             */
/*                                                                             */
/* Stack: 1024 word = 4096 byte (1024 x 4 byte/word trên ARM Cortex-M4).     */
/*   Tại sao 4 KB? DSP thread làm:                                             */
/*   - Phép tính float (SpO2 calculation, moving average): ~1-2 KB.           */
/*   - Copy PpgResult (~60-80 byte) + MeasurementHistoryRecord (~80 byte).    */
/*   - Local variables + function call stack: ~1 KB.                           */
/*   - Margin an toàn: ~1 KB.                                                  */
/*   Tổng: ~4 KB là đủ. Nếu quá ít, FreeRTOS sẽ crash (HardFault hoặc        */
/*   stack overflow detection trigger).                                       */
/* -------------------------------------------------------------------------- */
static StaticTask_t s_dspCb;        /* FreeRTOS Task Control Block (TCB).
                                         configSUPPORT_STATIC_ALLOCATION yêu cầu
                                         cấp phát tĩnh. Kích thước ~80-120 byte. */
static uint32_t s_dspStack[1024];   /* Stack cho DSP thread: 1024 word = 4 KB.
                                         ARM Cortex-M4: stack grows downward (full
                                         descending). 4 KB đủ cho float math +
                                         local variables + margin an toàn. */
static osThreadId_t s_dspThread = NULL;  /* Handle của DSP thread.
                                              NULL = chưa tạo.
                                              Kiểm tra NULL防止tạo 2 lần. */

/* Thuộc tính thread: tên, bộ nhớ control block, stack, ưu tiên.
   osThreadNew() sẽ dùng cấu trúc này để tạo thread. */
static const osThreadAttr_t s_dspAttr = {
    .name = "dspTask",             /* Tên thread: hiển thị trong RTOS explorer. */
    .cb_mem = &s_dspCb,           /* Con trỏ tới TCB đã cấp phát tĩnh. */
    .cb_size = sizeof(s_dspCb),   /* Kích thước TCB. */
    .stack_mem = s_dspStack,      /* Con trỏ tới stack đã cấp phát tĩnh. */
    .stack_size = sizeof(s_dspStack),  /* Kích thước stack: 4096 byte. */
    .priority = (osPriority_t)osPriorityNormal,  /* Ưu tiên bình thường.
                                                      Đọc/SensorTask: osPriorityAboveNormal.
                                                      GUI: osPriorityNormal.
                                                      DSP: osPriorityNormal (bằng GUI).
                                                      TelemetryTask: osPriorityBelowNormal. */
};

/** Tạo và khởi động DSP thread. Gọi MỘT LẦN từ sensor task
 *  SAU khi FreeRTOS scheduler đã chạy.
 *  Idempotent: gọi nhiều lần cũng không sao (kiểm tra NULL trước khi tạo).
 *
 *  Tại sao gọi từ sensor task? Vì sensor task là task đầu tiên chạy
 *  sau khi scheduler start. Nó cần tạo DSP thread để có nơi nhận sample.
 *
 *  Tại sao không tạo trong main()? Vì osMutexNew() và osThreadNew() yêu cầu
 *  scheduler đã chạy (FreeRTOS kernel đã active). Ở main() trước khi
 *  vTaskStartScheduler(), RTOS chưa active -> gọi API sẽ fail. */
void DspTask_Start(void)
{
    /* Tạo mutex cho history store (nếu chưa có).
       osMutexNew(NULL): dùng default attributes (non-recursive, no mutex type).
       Mutex này DSP thread dùng khi ghi TemporaryHistory,
       GUI thread dùng khi đọc TemporaryHistory. */
    if (s_histMutex == NULL)
    {
        s_histMutex = osMutexNew(NULL);  /* Tạo mutex RTOS. Trả về handle. */
    }
    /* Tạo DSP thread (nếu chưa có).
       Kiểm tra NULL防止idempotent: nếu đã tạo rồi -> không tạo lại.
       osThreadNew: CMSIS-RTOS2 API, tạo thread với thuộc tính s_dspAttr.
       Thread bắt đầu chạy ngay khi tạo (không cần resume). */
    if (s_dspThread == NULL)
    {
        s_dspThread = osThreadNew(dspThreadEntry, NULL, &s_dspAttr);
        /* dspThreadEntry sẽ gọi dspLoop() -> vòng lặp vô hạn.
           Từ lúc này, DSP thread chạy song song với sensor task và GUI task. */
    }
}
