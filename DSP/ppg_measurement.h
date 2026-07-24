#ifndef PPG_MEASUREMENT_H
#define PPG_MEASUREMENT_H

/**
 * @file    ppg_measurement.h
 * @brief   Engine đo PPG thuần: phát hiện ngón tay, ổn định tín hiệu, căn giữa
 *          DC và phát hiện peak/BPM sơ bộ.
 *
 * Không HAL và không cấp phát: nhận RAW sample và sinh ra @ref PpgResult. Cùng
 * một engine chạy trên target (được sensor task nạp) và trong simulator/host
 * test (nạp tổng hợp). Điều kiện hóa tín hiệu gồm trừ baseline DC cộng một
 * moving average tùy chọn (chọn qua @ref Ppg_SetFilterMode); không có
 * Butterworth/median/band-pass. Peak, BPM và waveform chạy trên tín hiệu đã
 * chọn. Dữ liệu RAW không bao giờ bị sửa.
 */

#include "ppg_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Đặt lại engine về IDLE/WAIT_FINGER. Gọi một lần lúc khởi động. */
void Ppg_Init(void);

/**
 * @brief Đánh dấu cảm biến lỗi (hoặc xóa cờ lỗi).
 * @param error True -> state thành SENSOR_ERROR và sample bị bỏ qua.
 */
void Ppg_SetSensorError(bool error);

/**
 * @brief Cộng thêm vào phần chẩn đoán dropped-sample / FIFO-overflow báo cáo.
 * @param droppedDelta   Số sample mất từ lần báo trước.
 * @param overflowDelta  Số lần FIFO overflow từ lần báo trước.
 */
void Ppg_ReportLoss(uint32_t droppedDelta, uint32_t overflowDelta);

/**
 * @brief Nạp một RAW sample và tiến state machine.
 * @param sample RAW sample (RED/IR/sequence/timestampMs).
 */
void Ppg_PushSample(const PpgRawSample* sample);

/**
 * @brief Chọn tín hiệu IR dùng cho peak/BPM/waveform (toàn cục).
 * @param mode RAW (đã centered DC) hoặc MOVING_AVERAGE (đã làm mượt).
 */
void Ppg_SetFilterMode(PpgFilterMode mode);

/**
 * @brief Đặt cửa sổ moving-average N (chặn trong 1..PPG_MA_WINDOW_MAX).
 * @param window Kích thước cửa sổ mới; khởi tạo lại các filter RED/IR.
 */
void Ppg_SetMaWindow(uint8_t window);

/**
 * @brief Sao chép snapshot phép đo hiện tại.
 * @param[out] out Đích ghi.
 */
void Ppg_GetResult(PpgResult* out);

#ifdef __cplusplus
}
#endif

#endif /* PPG_MEASUREMENT_H */
