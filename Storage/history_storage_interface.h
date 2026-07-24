#ifndef HISTORY_STORAGE_INTERFACE_H
#define HISTORY_STORAGE_INTERFACE_H

/**
 * @file    history_storage_interface.h
 * @brief   Interface lưu lịch sử độc lập backend (hỗ trợ SD sau này).
 *
 * Một vtable gồm các con trỏ hàm để ứng dụng lưu/đọc lịch sử mà không cần biết
 * backend. Giai đoạn này chỉ dùng TemporaryHistoryStore trong RAM; giai đoạn sau
 * thêm một cài đặt dựa trên SD với CÙNG interface, nên không caller (hay View)
 * nào phải đổi. GUI không bao giờ biết dữ liệu đến từ RAM hay SD.
 *
 * @note  User-owned. Thuần C, không HAL / bộ nhớ động.
 */

#include <stddef.h>
#include "measurement_types.h"
#include "temporary_history_store.h"   /* cho HistoryStatus */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Bảng con trỏ hàm cho một backend lưu lịch sử. */
typedef struct
{
    /** Thêm một bản ghi (backend gán/giữ id). */
    HistoryStatus (*appendRecord)(MeasurementHistoryRecord* record);

    /** Nạp một trang bản ghi (newest-first) vào buffer của caller. */
    HistoryStatus (*loadPage)(size_t pageIndex,
                              MeasurementHistoryRecord* records,
                              size_t capacity,
                              size_t* loadedCount);

    /** Xóa toàn bộ bản ghi. */
    HistoryStatus (*clearAll)(void);
} HistoryStorageInterface;

/**
 * @brief Trả về interface gắn với store tạm trong RAM.
 * @return Con trỏ tới một interface tĩnh (không bao giờ null).
 */
const HistoryStorageInterface* HistoryStorage_Temporary(void);

#ifdef __cplusplus
}
#endif

#endif /* HISTORY_STORAGE_INTERFACE_H */
