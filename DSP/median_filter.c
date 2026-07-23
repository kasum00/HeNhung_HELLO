#include "median_filter.h"
#include <string.h>  /* memcpy */

/* ---- Internal: insertion sort on sortBuf ---- */
static void insertionSort(int32_t* arr, size_t n)
{
    for (size_t i = 1U; i < n; ++i)
    {
        int32_t key = arr[i];
        size_t j = i;
        while (j > 0U && arr[j - 1U] > key)
        {
            arr[j] = arr[j - 1U];
            --j;
        }
        arr[j] = key;
    }
}

void Median_Init(MedianFilter* f, int32_t* buf, int32_t* sortBuf, size_t n)
{
    if (f == NULL || buf == NULL || sortBuf == NULL || n == 0U) { return; }
    f->buffer    = buf;
    f->sortBuf   = sortBuf;
    f->capacity  = n;
    f->count     = 0U;
    f->writeIndex= 0U;
    memset(buf, 0, n * sizeof(int32_t));
}

void Median_Reset(MedianFilter* f)
{
    if (f == NULL) { return; }
    f->count      = 0U;
    f->writeIndex = 0U;
    memset(f->buffer, 0, f->capacity * sizeof(int32_t));
}

int32_t Median_Process(MedianFilter* f, int32_t input)
{
    if (f == NULL || f->buffer == NULL || f->capacity == 0U) { return 0; }

    /* Ghi sample mới vào ring buffer */
    f->buffer[f->writeIndex] = input;
    f->writeIndex = (f->writeIndex + 1U) % f->capacity;
    if (f->count < f->capacity) { ++f->count; }

    /* Copy sang sortBuf và sắp xếp */
    memcpy(f->sortBuf, f->buffer, f->count * sizeof(int32_t));
    insertionSort(f->sortBuf, f->count);

    /* Lấy median (phần tử giữa) */
    return f->sortBuf[f->count / 2U];
}

bool Median_IsReady(const MedianFilter* f)
{
    return (f != NULL) && (f->count >= f->capacity);
}

