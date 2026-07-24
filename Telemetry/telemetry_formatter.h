#ifndef TELEMETRY_FORMATTER_H
#define TELEMETRY_FORMATTER_H

/**
 * @file    telemetry_formatter.h
 * @brief   Chuyển TelemetryMessage thành một dòng text CSV để gửi UART.
 *
 * Tách khỏi TelemetryService để tách biệt "làm gì" (đóng gói + hàng đợi) với
 * "trông thế nào" (định dạng). Chỉ TelemetryTask gọi các hàm này; chúng dùng
 * snprintf có kiểm tra biên (không malloc, không tràn). Mỗi dòng kết thúc bằng
 * '\n'. Cột đầu tiên là loại dòng (DATA / VITAL / STATE / EVENT / ALERT /
 * SESSION_START / SESSION_END / HISTORY / SYSTEM). User-owned.
 */

#include <stddef.h>
#include "telemetry_service.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Ghi dòng header CSV (gửi một lần khi bắt đầu stream).
 * @return Số byte đã ghi (không tính '\0'); 0 nếu buffer quá nhỏ.
 */
size_t TelemetryFormatter_Header(char* buf, size_t bufSize);

/**
 * @brief Format một thông điệp thành một dòng CSV.
 * @param msg     Thông điệp nguồn.
 * @param buf     Buffer đích.
 * @param bufSize Kích thước buffer.
 * @return Số byte đã ghi (không tính '\0'); 0 nếu lỗi hoặc bị cắt.
 */
size_t TelemetryFormatter_Format(const TelemetryMessage* msg, char* buf, size_t bufSize);

#ifdef __cplusplus
}
#endif

#endif /* TELEMETRY_FORMATTER_H */
