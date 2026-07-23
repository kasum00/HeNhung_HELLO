#ifndef MEDIAN_FILTER_H
#define MEDIAN_FILTER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {

#endif

/**
 * @brief Bộ lọc trung vị (median filter) trên cửa sổ cố định.
 *
 * Loại bỏ nhiễu xung (spikes) mà không làm mờ peak.
 * Nên dùng N lẻ (5, 7, 9...) để median luôn là 1 giá trị có thật.
 * O(N log N) mỗi sample do sắp xếp, nhưng N nhỏ nên chấp nhận được.
 */
typedef struct {
	int32_t* buffer;    /* Ring buffer (circular) */
	int32_t* sortBuf;   /* Buffer phụ (copy) cho phép sắp xếp mà không mất thứ tự */
	size_t capacity;    /* Kích thước cửa sổ N (nên là số lẻ) */
	size_t count;       /* Số sample đã thấy */
	size_t writeIndex;  /* Vị trí ghi tiếp */
} MedianFilter;

/*
 * @brief Khởi tạo filter
 * @param f 	Filter cần khởi tạo
 * @param buf	Buffer tĩnh N phần tử
 * @param sortBuf	Buffer phụ N phần tử (dùng để sắp xếp)
 * @param n 	Kích thước cửa sổ (nên lẻ: 5, 7, 9,...)
 */
void Median_Init(MedianFilter* f, int32_t* buf, int32_t* sortBuf, size_t n);

/*
 * @brief Đẩy một sample vào và trả về giá trị trung vị
 * @param f 	Filter
 * @param input Sample mới
 * @return Giá trị trung vị của cửa sổ hiện tại
 */
int32_t Median_Process(MedianFilter* f, int32_t input);

/*
 * @brief Xóa toàn bộ dữ liệu trong filter (đặt lại trạng thái ban đầu)
 * @param f Filter cần reset
 */
void Median_Reset(MedianFilter* f);

/*
 * @brief True khi đã đủ N sample (cửa sổ đầy)
 * Khi chưa đầy, median lấy trên fewer samples (vẫn hợp lý nhưng chưa ổn định)
 */
bool Median_IsReady(const MedianFilter* f);

#ifdef __cplusplus
}
#endif

#endif /* MEDIAN_FILTER_H */
