#ifndef LOWPASS_FILTER_H
#define LOWPASS_FILTER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Butterworth low-pass filter bậc 2 (IIR, Direct Form II Transposed).
 *
 * Dùng để loại bỏ nhiễu tần số cao (> 4 Hz) khỏi tín hiệu PPG.
 * Tần số cắt 4 Hz phù hợp cho PPG (tín hiệu hữu ích: 0.5–4 Hz).
 * Sample rate mặc định 100 Hz (PPG_SAMPLE_RATE_HZ).
 *
 * O(1) mỗi sample, 5 phép tính điểm dấu phẩy động.
 * KHÔNG cấp phát bộ nhớ động.
 */
typedef struct
{
    /* Hệ số filter (tính offline, hard-code) */
    float b0, b1, b2;   /* Numerator coefficients */
    float a1, a2;        /* Denominator coefficients (a0 = 1.0) */

    /* Trạng thái (Direct Form II Transposed) */
    float w1, w2;        /* Đọc/thư ký nội bộ */
} LowpassFilter;

/**
 * @brief Khởi tạo filter với hệ số Butterworth bậc 2, fc=4 Hz, fs=100 Hz.
 * @param f Filter cần khởi tạo.
 */
void Lowpass_Init(LowpassFilter* f);

/**
 * @brief Khởi tạo filter với hệ số tùy chỉnh.
 * @param f  Filter.
 * @param b0, b1, b2  Hệ số numerator.
 * @param a1, a2      Hệ số denominator.
 */
void Lowpass_InitCoeffs(LowpassFilter* f, float b0, float b1, float b2,
                        float a1, float a2);

/** @brief Xóa trạng thái filter (giữ nguyên hệ số). */
void Lowpass_Reset(LowpassFilter* f);

/**
 * @brief Xử lý một sample.
 * @param f     Filter.
 * @param input Giá trị vào (int32_t, sẽ được ép về float rồi lại ép về int32_t).
 * @return Giá trị đã lọc.
 */
int32_t Lowpass_Process(LowpassFilter* f, int32_t input);

#ifdef __cplusplus
}
#endif

#endif /* LOWPASS_FILTER_H */
