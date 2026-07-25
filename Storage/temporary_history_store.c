/**
 * @file    temporary_history_store.c
 * @brief   Cài đặt lịch sử RAM circular cỡ cố định (ghi đè bản cũ nhất).
 * @note    User-owned. Không cấp phát động.
 */

#include "temporary_history_store.h"

typedef struct
{
    MeasurementHistoryRecord records[TEMP_HISTORY_CAPACITY];
    size_t   count;
    size_t   writeIndex;
    uint32_t nextRecordId;
    uint32_t overwriteCount;
} TemporaryHistoryStore;

static TemporaryHistoryStore s_store;

HistoryStatus TemporaryHistory_Init(void)
{
    s_store.count = 0U;
    s_store.writeIndex = 0U;
    s_store.nextRecordId = 1U;
    s_store.overwriteCount = 0U;
    return HISTORY_STATUS_OK;
}

HistoryStatus TemporaryHistory_Add(MeasurementHistoryRecord* record)
{
    if (record == NULL)
    {
        return HISTORY_STATUS_INVALID_ARGUMENT;
    }

    record->recordId = s_store.nextRecordId++;

    s_store.records[s_store.writeIndex] = *record;

    if (s_store.count == TEMP_HISTORY_CAPACITY)
    {
        ++s_store.overwriteCount;    /* đã ghi đè bản cũ nhất */
    }
    else
    {
        ++s_store.count;
    }
    s_store.writeIndex = (s_store.writeIndex + 1U) % TEMP_HISTORY_CAPACITY;
    return HISTORY_STATUS_OK;
}

size_t TemporaryHistory_GetCount(void)
{
    return s_store.count;
}

HistoryStatus TemporaryHistory_GetByNewestIndex(size_t newestIndex,
                                                MeasurementHistoryRecord* record)
{
    if (record == NULL)
    {
        return HISTORY_STATUS_INVALID_ARGUMENT;
    }
    if (s_store.count == 0U)
    {
        return HISTORY_STATUS_EMPTY;
    }
    if (newestIndex >= s_store.count)
    {
        return HISTORY_STATUS_NOT_FOUND;
    }
    /* Bản mới nhất ở writeIndex-1; lùi lại newestIndex bước. */
    const size_t pos = (s_store.writeIndex + (2U * TEMP_HISTORY_CAPACITY) - 1U - newestIndex)
                       % TEMP_HISTORY_CAPACITY;
    *record = s_store.records[pos];
    return HISTORY_STATUS_OK;
}

HistoryStatus TemporaryHistory_GetById(uint32_t recordId,
                                       MeasurementHistoryRecord* record)
{
    if (record == NULL)
    {
        return HISTORY_STATUS_INVALID_ARGUMENT;
    }
    for (size_t i = 0U; i < s_store.count; ++i)
    {
        const size_t pos = (s_store.writeIndex + (2U * TEMP_HISTORY_CAPACITY) - 1U - i)
                           % TEMP_HISTORY_CAPACITY;
        if (s_store.records[pos].recordId == recordId)
        {
            *record = s_store.records[pos];
            return HISTORY_STATUS_OK;
        }
    }
    return HISTORY_STATUS_NOT_FOUND;
}

HistoryStatus TemporaryHistory_Clear(void)
{
    s_store.count = 0U;
    s_store.writeIndex = 0U;
    return HISTORY_STATUS_OK;
}

uint32_t TemporaryHistory_GetOverwriteCount(void)
{
    return s_store.overwriteCount;
}
