#ifndef ALERT_CONFIG_H
#define ALERT_CONFIG_H

/**
 * @file    alert_config.h
 * @brief   Ngưỡng và định thời cho cảnh báo y tế (LED PG13/PG14).
 *
 * Cảnh báo chỉ kích hoạt khi giá trị hợp lệ, có duy trì vượt ngưỡng đủ lâu
 * (confirmation) và tự tắt sau khi trở lại bình thường đủ lâu (clear) để tránh
 * nhấp nháy do một peak sai hoặc dao động ngắn. User-owned (ngoài generated).
 */

#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Ngưỡng nhịp tim (BPM) và nồng độ oxy (SpO2 %)           					    */
/* -------------------------------------------------------------------------- */
#define ALERT_BPM_LOW_THRESHOLD     60.0F
#define ALERT_BPM_HIGH_THRESHOLD    100.0F
#define ALERT_SPO2_LOW_THRESHOLD    90.0F

/* -------------------------------------------------------------------------- */
/* Định thời xác nhận / gỡ cảnh báo (hysteresis theo thời gian)                 */
/* -------------------------------------------------------------------------- */
/** Phải vượt ngưỡng liên tục trong khoảng này mới bật cảnh báo (ms).          */
#define ALERT_CONFIRMATION_MS       2000U
/** Phải trở lại bình thường liên tục trong khoảng này mới tắt cảnh báo (ms).  */
#define ALERT_CLEAR_MS              3000U

/* -------------------------------------------------------------------------- */
/* Nhịp nháy LED luân phiên                                                     */
/* -------------------------------------------------------------------------- */
/** Thời lượng mỗi pha (PG13 sáng / PG14 sáng) khi có cảnh báo (ms).           */
#define ALERT_LED_STEP_MS           300U

#endif /* ALERT_CONFIG_H */
