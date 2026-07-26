#ifndef TEMPORARY_HISTORY_STORE_H
#define TEMPORARY_HISTORY_STORE_H

/**
 * @file    temporary_history_store.h
 * @brief   Lịch sử đo trong RAM cỡ cố định (circular, ghi đè bản cũ nhất).
 *
 * Một store singleton chứa các bản tóm tắt phép đo gần nhất. Không cấp phát động:
 * một mảng cố định TEMP_HISTORY_CAPACITY bản ghi. Khi đầy, bản ghi cũ nhất bị ghi
 * đè và một bộ đếm overwrite được tăng. Lịch sử RAM này MẤT khi reset/mất nguồn.
 *
 * GUI không bao giờ đụng mảng nội bộ — nó đọc bản sao qua các accessor này.
 *
 * @note  User-owned. Thuần C, không HAL / bộ nhớ động.
 *
 * LƯU Ý VỀ CÔNG THỨC POSITION TRONG CIRCULAR BUFFER:
 *   pos = (writeIndex + 2*CAPACITY - 1 - newestIndex) % CAPACITY
 *   - "+2*CAPACITY" đảm bảo biểu thức trước '%' LUÔN DƯƠNG.
 *   - Trong C, phép '%'' với số âm có kết quả không xác định (implementation-defined).
 *   - 2*CAPACITY = 40 đủ lớn để bù cho mọi giá trị writeIndex và newestIndex,
 *     mà 40 % 20 = 0 nên không thay đổi kết quả phép chia dư.
 *   - Chi tiết xem temporary_history_store.c (hàm GetByNewestIndex).
 */

#include <stddef.h>
#include "measurement_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Số bản ghi tối đa giữ trong RAM. */
#define TEMP_HISTORY_CAPACITY 20U

/** @brief Kết quả của một thao tác lịch sử. */
typedef enum
{
    HISTORY_STATUS_OK = 0,
    HISTORY_STATUS_INVALID_ARGUMENT,
    HISTORY_STATUS_EMPTY,
    HISTORY_STATUS_NOT_FOUND,
    HISTORY_STATUS_FULL,
    HISTORY_STATUS_ERROR
} HistoryStatus;

/** @brief Xóa store (gọi một lần lúc khởi động). */
HistoryStatus TemporaryHistory_Init(void);

/**
 * @brief Thêm một bản ghi (gán recordId mới cho @p record).
 * @param record Bản ghi để copy vào. recordId của nó bị ghi đè bằng id kế tiếp.
 * @return OK, hoặc INVALID_ARGUMENT nếu null. Ghi đè bản cũ nhất khi đầy.
 *
 * recordId của bản sao lưu trong store là id tăng đơn điệu; caller có thể đọc lại
 * từ @p record (được cập nhật tại chỗ).
 */
HistoryStatus TemporaryHistory_Add(MeasurementHistoryRecord* record);

/** @brief Số bản ghi đang giữ (0..TEMP_HISTORY_CAPACITY). */
size_t TemporaryHistory_GetCount(void);

/**
 * @brief Copy một bản ghi theo chỉ số newest-first (0 = mới nhất).
 * @param newestIndex Đánh số từ 0, 0 là mới nhất.
 * @param record      Đích ghi.
 * @return OK, EMPTY, NOT_FOUND (chỉ số ngoài dải) hoặc INVALID_ARGUMENT.
 */
HistoryStatus TemporaryHistory_GetByNewestIndex(size_t newestIndex,
                                                MeasurementHistoryRecord* record);

/** @brief Copy một bản ghi theo recordId của nó. */
HistoryStatus TemporaryHistory_GetById(uint32_t recordId,
                                       MeasurementHistoryRecord* record);

/** @brief Làm rỗng store (giữ nguyên bộ đếm id đang chạy). */
HistoryStatus TemporaryHistory_Clear(void);

/** @brief Số lần bản ghi cũ nhất đã bị ghi đè (chẩn đoán). */
uint32_t TemporaryHistory_GetOverwriteCount(void);

#ifdef __cplusplus
}
#endif

#endif /* TEMPORARY_HISTORY_STORE_H */
