/**
 * @file    ppg_sample_queue.c
 * @brief   Cài đặt ring RAW-sample SPSC lock-free. Target-only.
 * @note    User-owned. Một producer (sensor task), một consumer (DSP task).
 */

#include "ppg_sample_queue.h"

#define QUEUE_CAPACITY   256U           /* lũy thừa của 2 */
#define QUEUE_MASK       (QUEUE_CAPACITY - 1U)

static PpgRawSample s_ring[QUEUE_CAPACITY];
static volatile uint32_t s_head;        /* chỉ producer ghi */
static volatile uint32_t s_tail;        /* chỉ consumer ghi */
static volatile uint32_t s_dropped;

void PpgQueue_Reset(void)
{
    s_head = 0U;
    s_tail = 0U;
    s_dropped = 0U;
}

bool PpgQueue_Push(const PpgRawSample* sample)
{
    if (sample == 0)
    {
        return false;
    }
    const uint32_t head = s_head;
    const uint32_t next = (head + 1U) & QUEUE_MASK;
    if (next == s_tail)
    {
        ++s_dropped;                    /* đầy */
        return false;
    }
    s_ring[head] = *sample;
    s_head = next;                      /* publish sau khi ghi */
    return true;
}

bool PpgQueue_Pop(PpgRawSample* sample)
{
    if (sample == 0)
    {
        return false;
    }
    const uint32_t tail = s_tail;
    if (tail == s_head)
    {
        return false;                   /* rỗng */
    }
    *sample = s_ring[tail];
    s_tail = (tail + 1U) & QUEUE_MASK;  /* giải phóng slot sau khi đọc */
    return true;
}

uint32_t PpgQueue_DroppedCount(void)
{
    return s_dropped;
}
