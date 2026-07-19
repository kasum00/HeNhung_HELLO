#ifndef MOVING_AVERAGE_FILTER_H
#define MOVING_AVERAGE_FILTER_H

/**
 * @file    moving_average_filter.h
 * @brief   Moving average cửa sổ trượt trên buffer cố định (O(1) mỗi sample).
 *
 * Moving average dùng running-sum trên buffer tĩnh do caller cấp. Không cấp phát
 * động: caller sở hữu mảng backing. Mỗi kênh (RED, IR, ...) dùng một instance
 * filter và một buffer riêng.
 *
 * Đây là bước lọc DUY NHẤT trong giai đoạn này; chỉ là bước làm mượt đơn giản,
 * KHÔNG phải tín hiệu lọc hoàn chỉnh / cấp y tế. Group delay là hằng số (N-1)/2
 * sample, triệt tiêu khi lấy hiệu giữa các peak (RR interval), nên BPM không bị
 * ảnh hưởng bởi độ trễ này.
 *
 * @note  User-owned. Thuần C, không HAL, không cấp phát. Test được trên host.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Kết quả của một thao tác moving-average. */
typedef enum
{
    MOVING_AVERAGE_STATUS_OK = 0,          /**< Cửa sổ đầy: output là trung bình đầy đủ.  */
    MOVING_AVERAGE_STATUS_INVALID_ARGUMENT,/**< Con trỏ null hoặc capacity bằng 0.        */
    MOVING_AVERAGE_STATUS_NOT_READY        /**< Đang lấp: output là trung bình một phần.  */
} MovingAverageStatus;

/** @brief Trạng thái moving-average cửa sổ trượt (running sum). */
typedef struct
{
    int32_t* buffer;     /**< Ring do caller sở hữu, giữ @c capacity sample gần nhất. */
    size_t   capacity;   /**< Kích thước cửa sổ N (>= 1).                             */
    size_t   count;      /**< Số sample đã thấy, chặn tại @c capacity.                */
    size_t   writeIndex; /**< Vị trí ghi tiếp (phần tử cũ nhất khi đã đầy).           */
    int64_t  sum;        /**< Running sum của các sample hiện có trong cửa sổ.        */
} MovingAverageFilter;

/**
 * @brief Khởi tạo filter trên buffer do caller cấp.
 * @param filter        Filter cần khởi tạo.
 * @param backingBuffer Mảng ít nhất @p capacity phần tử int32_t (tĩnh, không free).
 * @param capacity      Kích thước cửa sổ N (>= 1).
 * @return OK, hoặc INVALID_ARGUMENT nếu con trỏ null / capacity bằng 0.
 */
MovingAverageStatus MovingAverage_Init(MovingAverageFilter* filter,
                                       int32_t* backingBuffer,
                                       size_t capacity);

/** @brief Xóa cửa sổ (count/sum/index) mà không đụng tới buffer. */
void MovingAverage_Reset(MovingAverageFilter* filter);

/**
 * @brief Đẩy một sample vào và trả về trung bình cửa sổ hiện tại.
 * @param filter Instance filter.
 * @param input  Sample mới.
 * @param output Nhận trung bình của các sample hiện có trong cửa sổ (trung bình
 *               một phần cho tới khi cửa sổ đầy, sau đó là trung bình đầy đủ).
 * @return OK khi cửa sổ đã đầy, NOT_READY khi còn đang lấp, hoặc INVALID_ARGUMENT
 *         nếu con trỏ null.
 *
 * O(1): trừ sample cũ nhất và cộng sample mới vào running sum. Caller cần một giá
 * trị cửa sổ đầy đủ (ví dụ BPM) phải kiểm tra @ref MovingAverage_IsReady (hoặc
 * return OK), không dùng trung bình một phần.
 */
MovingAverageStatus MovingAverage_Process(MovingAverageFilter* filter,
                                          int32_t input,
                                          int32_t* output);

/** @brief True khi đã thấy đủ @c capacity sample (cửa sổ đầy). */
bool MovingAverage_IsReady(const MovingAverageFilter* filter);

#ifdef __cplusplus
}
#endif

#endif /* MOVING_AVERAGE_FILTER_H */
