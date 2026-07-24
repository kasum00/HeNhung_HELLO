/**
 * @file    moving_average_filter.c
 * @brief   Cài đặt moving average running-sum trên buffer cố định.
 * @note    User-owned. O(1) mỗi sample, không cấp phát.
 */

#include "moving_average_filter.h"

MovingAverageStatus MovingAverage_Init(MovingAverageFilter* filter,
                                       int32_t* backingBuffer,
                                       size_t capacity)
{
    if ((filter == NULL) || (backingBuffer == NULL) || (capacity == 0U))
    {
        return MOVING_AVERAGE_STATUS_INVALID_ARGUMENT;
    }
    filter->buffer = backingBuffer;
    filter->capacity = capacity;
    filter->count = 0U;
    filter->writeIndex = 0U;
    filter->sum = 0;
    return MOVING_AVERAGE_STATUS_OK;
}

void MovingAverage_Reset(MovingAverageFilter* filter)
{
    if (filter == NULL)
    {
        return;
    }
    filter->count = 0U;
    filter->writeIndex = 0U;
    filter->sum = 0;
}

MovingAverageStatus MovingAverage_Process(MovingAverageFilter* filter,
                                          int32_t input,
                                          int32_t* output)
{
    if ((filter == NULL) || (output == NULL) || (filter->buffer == NULL) ||
        (filter->capacity == 0U))
    {
        return MOVING_AVERAGE_STATUS_INVALID_ARGUMENT;
    }

    if (filter->count == filter->capacity)
    {
        /* Cửa sổ đầy: vị trí ghi đang giữ sample cũ nhất; loại nó ra. */
        filter->sum -= (int64_t)filter->buffer[filter->writeIndex];
    }
    else
    {
        ++filter->count;
    }

    filter->buffer[filter->writeIndex] = input;
    filter->sum += (int64_t)input;
    filter->writeIndex = (filter->writeIndex + 1U) % filter->capacity;

    /* Chia nguyên (cắt về 0) — đối xứng, không gây lệch DC. */
    *output = (int32_t)(filter->sum / (int64_t)filter->count);

    return (filter->count == filter->capacity) ? MOVING_AVERAGE_STATUS_OK
                                               : MOVING_AVERAGE_STATUS_NOT_READY;
}

bool MovingAverage_IsReady(const MovingAverageFilter* filter)
{
    return (filter != NULL) && (filter->count == filter->capacity);
}
