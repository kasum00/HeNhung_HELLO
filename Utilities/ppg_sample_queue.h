#ifndef PPG_SAMPLE_QUEUE_H
#define PPG_SAMPLE_QUEUE_H

/**
 * @file    ppg_sample_queue.h
 * @brief   Ring lock-free single-producer / single-consumer chứa RAW sample.
 *
 * Sensor task là producer duy nhất (đẩy RAW sample MAX30102); DSP task là
 * consumer duy nhất (lấy ra và nạp cho engine đo). Trên Cortex-M một nhân với
 * head/tail volatile, cấu trúc này không cần mutex. Target-only.
 */

#include "ppg_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Làm rỗng queue. */
void PpgQueue_Reset(void);

/**
 * @brief Producer: đưa một sample vào queue (sensor task).
 * @param sample Sample cần lưu.
 * @return True nếu đã lưu; false nếu đầy (tính là một lần drop).
 */
bool PpgQueue_Push(const PpgRawSample* sample);

/**
 * @brief Consumer: lấy một sample ra khỏi queue (DSP task).
 * @param[out] sample Đích ghi.
 * @return True nếu đã trả về một sample; false nếu rỗng.
 */
bool PpgQueue_Pop(PpgRawSample* sample);

/** @brief Tổng số sample bị bỏ do queue đầy. */
uint32_t PpgQueue_DroppedCount(void);

#ifdef __cplusplus
}
#endif

#endif /* PPG_SAMPLE_QUEUE_H */
