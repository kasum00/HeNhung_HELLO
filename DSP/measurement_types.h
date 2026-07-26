#ifndef MEASUREMENT_TYPES_H
#define MEASUREMENT_TYPES_H

/**
 * @file    measurement_types.h
 * @brief   Kiểu dữ liệu kết quả đo đã chốt + bản ghi lịch sử.
 *
 * Các kiểu plain-data dùng chung giữa engine đo/DSP task và history service.
 * Độc lập với TouchGFX. Một bản ghi lịch sử là bản TÓM TẮT gọn của một phiên
 * (không có waveform raw) để chứa được nhiều bản trong buffer RAM cố định.
 *
 * @note  User-owned. Thuần C, không HAL / bộ nhớ động.
 */

#include <stdint.h>
#include <stdbool.h>
#include "datetime.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Phân loại chất lượng tổng thể của một phiên đo đã chốt.
 *
 * Mỗi phiên đo kết thúc, hệ thống đánh giá kết quả dựa trên:
 *  - BPM hợp lệ? (có đủ nhịp tim để tính BPM không?)
 *  - SpO2 hợp lệ? (có đủ dữ liệu quang để tính SpO2 không?)
 *
 * Ba cấp độ:
 *  - VALID:   Cả BPM VÀ SpO2 đều hợp lệ (kết quả đầy đủ, đáng tin).
 *  - PARTIAL: Chỉ MỘT trong hai chỉ số hợp lệ (ví dụ: phát hiện được nhịp
 *             tim nhưng SpO2 không đủ dữ liệu, hoặc ngược lại).
 *  - INVALID: Không dùng được - phiên đo quá ngắn (< 5s), không phát hiện
 *             được nhịp tim nào, hoặc tín hiệu quá yếu/nhiễu.
 */
typedef enum
{
    MEASUREMENT_RESULT_VALID = 0,  /**< Cả BPM và SpO2 đều hợp lệ.          */
    MEASUREMENT_RESULT_PARTIAL,    /**< Một chỉ số hợp lệ (ví dụ chỉ BPM).  */
    MEASUREMENT_RESULT_INVALID     /**< Không dùng được (quá ngắn / không có nhịp). */
} MeasurementResultStatus;

/**
 * @brief Lý do một phiên đo kết thúc.
 *
 * Mỗi phiên đo phải kết thúc vì một lý do nào đó. Các lý do phân thành 2 nhóm:
 *
 *  KẾT THÚC BÌNH THƯỜNG (dự kiến):
 *    - FINGER_REMOVED:   Cảm biến phát hiện ngón tay được nhấc ra khỏi cảm biến.
 *                        (tự động dừng, không cần người dùng bấm nút).
 *    - USER_STOPPED:     Người dùng chủ động bấm nút "Dừng" trên giao diện.
 *    - TIMEOUT:          Đo quá thời gian giới hạn (ví dụ: 60s), tự dừng.
 *
 *  KẾT THÚC DO LỖI (không mong muốn):
 *    - SENSOR_ERROR:     Lỗi cảm biến MAX30102 (không đọc được dữ liệu từ I2C/SPI).
 *    - SIGNAL_LOST:      Cảm biến vẫn hoạt động nhưng tín hiệu quá yếu,
 *                        mất nhịp (signal lost), không thể tiếp tục đo.
 */
typedef enum
{
    MEASUREMENT_END_FINGER_REMOVED = 0, /* Ngón tay rời khỏi cảm biến (dừng tự động) */
    MEASUREMENT_END_USER_STOPPED,       /* Người dùng bấm nút dừng */
    MEASUREMENT_END_TIMEOUT,            /* Hết thời gian đo cho phép */
    MEASUREMENT_END_SENSOR_ERROR,       /* Lỗi cảm biến MAX30102 (không giao tiếp được) */
    MEASUREMENT_END_SIGNAL_LOST         /* Mất tín hiệu (cảm biến hoạt động nhưng tín hiệu yếu) */
} MeasurementEndReason;

/**
 * @brief Một phiên đo đã tóm tắt, lưu trong lịch sử (bản ghi lịch sử).
 *
 * Đây là bản TÓM TẮT gọn gàng của một phiên đo hoàn chỉnh.
 * KHÔNG chứa waveform raw (dữ liệu thô từ cảm biến) để tiết kiệm RAM.
 *
 * Cấu trúc chia thành các nhóm thông tin:
 *  1. Định danh và thời gian
 *  2. Thống kê BPM (nhịp tim)
 *  3. Thống kê SpO2 (nồng độ oxy trong máu)
 *  4. Chất lượng tín hiệu
 *  5. Thống kê peaks (nhịp phát hiện được)
 *  6. Lỗi và tình trạng
 */
typedef struct
{
    /* === NHÓM 1: ĐỊNH DANH VÀ THỜI GIAN === */
    uint32_t recordId;          /* ID tự tăng, gán bởi TemporaryHistory_Add. Dùng để
                                   tìm lại bản ghi cụ thể (xem GetById). */

    DateTime startDateTime;     /* Ngày/giờ bắt đầu đo (theo RTC hoặc system clock) */
    DateTime endDateTime;       /* Ngày/giờ kết thúc đo */

    uint32_t durationMs;        /* Tổng thời gian đo (tính bằng mili-giây).
                                   Ví dụ: 15000 ms = 15 giây đo liên tục. */

    /* === NHÓM 2: THỐNG KÊ BPM (NHỊP TIM) === */
    float averageBpm;           /* BPM trung bình trong phiên đo (lấy từ các nhịp hợp lệ).
                                   Ví dụ: 72.5 BPM = nhịp tim trung bình 72.5 lần/phút. */
    float minimumBpm;           /* BPM thấp nhất ghi nhận được trong phiên */
    float maximumBpm;           /* BPM cao nhất ghi nhận được trong phiên */

    /* === NHÓM 3: THỐNG KÊ SpO2 (OXY TRONG MÁU) === */
    float averageSpo2;          /* SpO2 trung bình (%) trong phiên đo.
                                   Ví dụ: 98.2% = nồng độ oxy trung bình 98.2%. */
    float minimumSpo2;          /* SpO2 thấp nhất ghi nhận */
    float maximumSpo2;          /* SpO2 cao nhất ghi nhận */

    /* === NHÓM 4: CHẤT LƯỢNG TÍN HIỆU === */
    float averageSqi;           /* Signal Quality Index trung bình (chỉ số chất lượng tín hiệu).
                                   SQI = 0.0 (kém) đến 1.0 (tốt).
                                   Giá trị cao = tín hiệu PPG rõ, ít nhiễu (noise). */

    /* === NHÓM 5: THỐNG KÊ PEAKS (CÁC ĐỈNH TÍN HIỆU PPG) === */
    uint32_t acceptedPeakCount; /* Số đỉnh tín hiệu được CHẤP NHẬN (dùng để tính BPM).
                                   Mỗi đỉnh = một nhịp tim. Chỉ các đỉnh "đẹp" mới được chấp nhận. */
    uint32_t rejectedPeakCount; /* Số đỉnh tín hiệu bị TỪ CHỐI (phát hiện nhưng bị lọc bỏ).
                                   Đỉnh bị từ chối: có thể do nhiễu, movement artifact,
                                   hoặc không thỏa tiêu chí khoảng cách giữa 2 đỉnh. */
    uint32_t droppedSampleCount;/* Số mẫu (sample) bị MẤT do queue bị đầy (overflow).
                                   Khi CPU xử lý chậm hơn tần số lấy mẫu của cảm biến,
                                   một số mẫu sẽ bị bỏ qua. Giá trị > 0 = có mất dữ liệu. */
    uint32_t fifoOverflowCount; /* Số lần bộ nhớ đệm FIFO của MAX30102 bị tràn.
                                   MAX30102 có FIFO 32 mẫu. Nếu MCU đọc chậm,
                                   FIFO sẽ đầy và các mẫu mới bị mất (overflow).
                                   Giá trị > 0 = MCU đọc chậm so với tốc độ cảm biến. */

    /* === NHÓM 6: TÌNH TRẠNG VÀ KẾT LUẬN === */
    bool bpmValid;              /* true = BPM tính được ĐÁNG TIN (đủ đỉnh, tín hiệu tốt).
                                   false = BPM không hợp lệ hoặc không tính được. */
    bool spo2Valid;             /* true = SpO2 tính được ĐÁNG TIN (đủ dữ liệu quang).
                                   false = SpO2 không hợp lệ hoặc không tính được. */

    MeasurementResultStatus status;  /* Chất lượng tổng thể: VALID / PARTIAL / INVALID.
                                        Xem enum MeasurementResultStatus ở trên. */
    MeasurementEndReason endReason;  /* Lý do phiên đo kết thúc.
                                        Xem enum MeasurementEndReason ở trên. */
} MeasurementHistoryRecord;

#ifdef __cplusplus
}
#endif

#endif /* MEASUREMENT_TYPES_H */
