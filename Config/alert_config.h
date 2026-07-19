#ifndef ALERT_CONFIG_H
#define ALERT_CONFIG_H

/**
 * @file    alert_config.h
 * @brief   Ngưỡng và định thời cho cảnh báo y tế (LED PG13/PG14).
 *
 * KHÔNG PHẢI THIẾT BỊ Y TẾ. Các giá trị dưới đây chỉ phục vụ học tập / thử
 * nghiệm, không được coi là chính xác về mặt lâm sàng và không thay thế thiết bị
 * y tế được chứng nhận. Ngưỡng nằm tập trung ở đây (CONFIG RULE); driver LED và
 * MedicalAlertService không được hard-code ngưỡng.
 *
 * Cảnh báo chỉ kích hoạt khi giá trị hợp lệ, có duy trì vượt ngưỡng đủ lâu
 * (confirmation) và tự tắt sau khi trở lại bình thường đủ lâu (clear) để tránh
 * nhấp nháy do một peak sai hoặc dao động ngắn. User-owned (ngoài generated).
 */

#include <stdint.h>

/* -------------------------------------------------------------------------- */
/* Ngưỡng nhịp tim (BPM) và nồng độ oxy (SpO2 %) - chỉ để thử nghiệm            */
/* -------------------------------------------------------------------------- */
/** Dưới ngưỡng này coi là nhịp thấp (bradycardia) — chỉ để thử nghiệm.        */
#define ALERT_BPM_LOW_THRESHOLD     45.0F
/** Trên ngưỡng này coi là nhịp cao (tachycardia) — chỉ để thử nghiệm.         */
#define ALERT_BPM_HIGH_THRESHOLD    120.0F
/** Dưới ngưỡng này coi là SpO2 thấp (hypoxia) — chỉ để thử nghiệm.            */
#define ALERT_SPO2_LOW_THRESHOLD    92.0F

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
