#ifndef DSP_TASK_H
#define DSP_TASK_H

/**
 * @file    dsp_task.h
 * @brief   DSP task (target): sở hữu engine đo PPG, chạy ngoài GUI tick.
 *
 * Theo quy tắc pipeline "TouchGFX không được chạy DSP", engine đo không còn chạy
 * bên trong GUI/TouchGFX tick nữa. Thay vào đó một FreeRTOS thread riêng (tạo từ
 * sensor task khi scheduler đã chạy) rút sample queue lock-free, tiến engine, và
 * công bố @ref PpgResult mới nhất qua một seqlock một-ghi/một-đọc. GUI bridge chỉ
 * ĐỌC kết quả đã công bố; không bao giờ đụng trạng thái engine.
 *
 *   SensorTask -> PpgQueue -> DspTask (engine) -> PpgResult công bố -> GUI
 *
 * Target-only: nằm trong Application/ để bản build simulator (dùng mock provider)
 * không bao giờ biên dịch nó.
 */

#include "ppg_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Tạo và khởi động DSP thread (gọi một lần, sau khi scheduler đã chạy —
 *        ví dụ từ sensor task). Idempotent.
 */
void DspTask_Start(void);

/**
 * @brief Sao chép kết quả engine công bố gần nhất (đọc seqlock).
 * @param out Đích, được điền một snapshot kết quả không bị xé.
 *
 * An toàn khi gọi từ GUI thread trong lúc DSP thread vẫn đang công bố.
 */
void DspTask_GetResult(PpgResult* out);

/**
 * @brief Yêu cầu filter mode phân tích/hiển thị (áp dụng trên DSP thread).
 * @param mode RAW hoặc MOVING_AVERAGE. An toàn khi gọi từ GUI thread.
 */
void DspTask_SetFilterMode(PpgFilterMode mode);

/**
 * @brief Yêu cầu cửa sổ moving-average N mới (áp dụng trên DSP thread).
 * @param window 1..PPG_MA_WINDOW_MAX. An toàn khi gọi từ GUI thread.
 */
void DspTask_SetMaWindow(uint8_t window);

/**
 * @brief Khóa/mở khóa store lịch sử tạm (DSP ghi, GUI đọc).
 *
 * GUI thread phải bọc các lần đọc TemporaryHistory_* bằng cặp này để một lần
 * finalize trên DSP thread không xé một bản ghi khi đang copy.
 */
void DspTask_HistoryLock(void);
void DspTask_HistoryUnlock(void);

#ifdef __cplusplus
}
#endif

#endif /* DSP_TASK_H */
