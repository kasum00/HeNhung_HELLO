#ifndef SPO2_ESTIMATOR_H
#define SPO2_ESTIMATOR_H

/**
 * @file    spo2_estimator.h
 * @brief   Ước lượng SpO2 bằng ratio-of-ratios trên RAW RED/IR.
 *
 * Tính DC (trung bình cửa sổ) và AC (RMS quanh trung bình) cho RED và IR trên
 * một cửa sổ trượt ghép từ các sub-block độ dài cố định (chỉ dùng running sum —
 * không buffer từng sample), rồi R = (AC_RED/DC_RED)/(AC_IR/DC_IR) và calibration
 * thực nghiệm SpO2 = A + B*R + C*R^2. Sinh giá trị mới ~mỗi block (khoảng 1 Hz).
 * O(1) mỗi sample, không cấp phát, test được trên host.
 *
 * KHÔNG phải thiết bị y tế: calibration là thực nghiệm và caller còn phải chặn
 * tính hợp lệ theo chất lượng tín hiệu (SQI), sự hiện diện ngón tay và bão hòa —
 * module này chỉ báo phép tính ratio có hợp lý về mặt số học hay không.
 *
 * @note  User-owned. Thuần C, không HAL.
 */

#include <stdint.h>
#include <stdbool.h>
#include "ppg_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Kết quả của việc nạp một sample. */
typedef enum
{
    SPO2_STATUS_NOT_READY = 0,   /**< Cửa sổ còn đang lấp; @c out không đổi. */
    SPO2_STATUS_OK,              /**< Đã tính cửa sổ mới và hợp lệ về sinh lý. */
    SPO2_STATUS_INVALID_SIGNAL   /**< Đã tính cửa sổ mới nhưng bị loại (DC/AC/ratio/dải). */
} Spo2Status;

/** @brief Hệ số calibration cho SpO2 = A + B*R + C*R^2 (C có thể bằng 0). */
typedef struct
{
    float coefficientA;
    float coefficientB;
    float coefficientC;
} Spo2Calibration;

/** @brief Giá trị cửa sổ mới tính (điền khi một block hoàn tất). */
typedef struct
{
    float spo2;    /**< SpO2 %% ước lượng (chỉ có nghĩa khi status == OK). */
    float ratio;   /**< Ratio of ratios R.                                */
    float dcRed;
    float dcIr;
    float acRed;
    float acIr;
    bool  valid;   /**< Ánh xạ của status == OK.                          */
} Spo2Result;

/** @brief Một sub-block running sum (1 giây sample). */
typedef struct
{
    int64_t  sumRed;
    int64_t  sumIr;
    int64_t  sumSqRed;
    int64_t  sumSqIr;
    uint32_t count;
} Spo2Block;

/** @brief Trạng thái estimator SpO2 (toàn bộ lưu tĩnh). */
typedef struct
{
    Spo2Calibration cal;
    Spo2Block blocks[PPG_SPO2_BLOCKS];
    uint8_t  curBlock;
    uint8_t  filledBlocks;
    uint32_t blockStartMs;
    bool     started;
    Spo2Result last;
} Spo2Estimator;

/**
 * @brief Khởi tạo estimator với một calibration (null => dùng mặc định).
 * @param est Estimator.
 * @param cal Calibration, hoặc NULL để dùng PPG_SPO2_CAL_A/B/C.
 */
void Spo2_Init(Spo2Estimator* est, const Spo2Calibration* cal);

/** @brief Xóa toàn bộ accumulator (phiên mới / nhấc ngón tay). */
void Spo2_Reset(Spo2Estimator* est);

/**
 * @brief Nạp một RAW sample; có thể hoàn tất một cửa sổ và tính SpO2 mới.
 * @param est         Estimator.
 * @param redRaw      Giá trị RAW RED.
 * @param irRaw       Giá trị RAW IR.
 * @param timestampMs Thời điểm sample (ms).
 * @param out         Nhận giá trị cửa sổ khi một block hoàn tất.
 * @return OK / INVALID_SIGNAL khi có block hoàn tất trong lần gọi này, else NOT_READY.
 */
Spo2Status Spo2_Process(Spo2Estimator* est, uint32_t redRaw, uint32_t irRaw,
                        uint32_t timestampMs, Spo2Result* out);

#ifdef __cplusplus
}
#endif

#endif /* SPO2_ESTIMATOR_H */
