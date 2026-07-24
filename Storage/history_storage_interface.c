/**
 * @file    history_storage_interface.c
 * @brief   Cài đặt interface lưu lịch sử bằng RAM.
 * @note    User-owned. Bọc TemporaryHistoryStore phía sau vtable.
 */

#include "history_storage_interface.h"

static HistoryStatus ramAppend(MeasurementHistoryRecord* record)
{
    return TemporaryHistory_Add(record);
}

static HistoryStatus ramLoadPage(size_t pageIndex,
                                 MeasurementHistoryRecord* records,
                                 size_t capacity,
                                 size_t* loadedCount)
{
    if ((records == NULL) || (loadedCount == NULL) || (capacity == 0U))
    {
        return HISTORY_STATUS_INVALID_ARGUMENT;
    }
    *loadedCount = 0U;

    const size_t total = TemporaryHistory_GetCount();
    if (total == 0U)
    {
        return HISTORY_STATUS_EMPTY;
    }

    const size_t start = pageIndex * capacity;   /* offset newest-first */
    for (size_t j = 0U; j < capacity; ++j)
    {
        MeasurementHistoryRecord rec;
        const HistoryStatus s = TemporaryHistory_GetByNewestIndex(start + j, &rec);
        if (s != HISTORY_STATUS_OK)
        {
            break;   /* NOT_FOUND: đã qua cuối trang này */
        }
        records[j] = rec;
        ++(*loadedCount);
    }
    return HISTORY_STATUS_OK;
}

static HistoryStatus ramClearAll(void)
{
    return TemporaryHistory_Clear();
}

const HistoryStorageInterface* HistoryStorage_Temporary(void)
{
    static const HistoryStorageInterface iface = {
        ramAppend,
        ramLoadPage,
        ramClearAll,
    };
    return &iface;
}
