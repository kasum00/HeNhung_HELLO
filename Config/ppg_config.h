#ifndef PPG_CONFIG_H
#define PPG_CONFIG_H

/**
 * @file    ppg_config.h
 * @brief   Các ngưỡng cấu hình cho phát hiện ngón tay, ổn định tín hiệu, căn
 *          giữa waveform và phát hiện peak/BPM sơ bộ.
 *
 * Theo CONFIG RULE, không ngưỡng nào được hard-code trong engine. Các giá trị ở
 * đây là điểm khởi đầu và PHẢI được tinh chỉnh theo module MAX30102 thật + dòng
 * LED thực tế. Đơn vị là RAW IR 18-bit trừ khi ghi chú khác.
 */

#include <stdint.h>

/* -------- Sampling -------- */
#define PPG_SAMPLE_RATE_HZ         100U

/* -------- Phát hiện ngón tay (mức IR DC, có hysteresis) -------- */
#define PPG_FINGER_ON_THRESHOLD    50000U   /**< IR DC vượt ngưỡng -> có ngón tay.   */
#define PPG_FINGER_OFF_THRESHOLD   30000U   /**< IR DC dưới ngưỡng -> đã nhấc ra.     */
#define PPG_FINGER_ON_SAMPLES      15U      /**< Số sample liên tiếp để xác nhận đặt. */
#define PPG_FINGER_OFF_SAMPLES     25U      /**< Số sample liên tiếp để xác nhận nhấc.*/

/* -------- Ổn định tín hiệu -------- */
#define PPG_STABILIZE_MIN_MS       2500U    /**< Thời gian ổn định tối thiểu.         */
#define PPG_STABILIZE_MAX_MS       8000U    /**< Cửa sổ kiểm tra lại / bỏ cuộc.       */
#define PPG_SATURATION_LEVEL       255000U  /**< Gần mức 18-bit tối đa -> bão hòa.    */
#define PPG_MIN_AC_AMPLITUDE       150      /**< Biên độ đỉnh-đỉnh centered tối thiểu.*/
#define PPG_DC_MIN                 40000U   /**< IR DC phải luôn cao hơn giá trị này. */

/* -------- Baseline / căn giữa -------- */
/* Hệ số baseline thích nghi khi MEASURING, biểu diễn bằng dịch bit: baseline di
   chuyển theo (raw - baseline) >> PPG_BASELINE_SHIFT mỗi sample (shift lớn hơn =
   chậm hơn). */
#define PPG_BASELINE_SHIFT         9        /**< ~ alpha = 1/512 (chỉ bù trôi chậm).  */

/* -------- Bộ lọc moving average -------- */
/* Kích thước cửa sổ N mặc định của moving average RED/IR tại PPG_SAMPLE_RATE_HZ.
   Nhỏ để làm mượt nhiễu mà không trễ peak hay giảm biên độ AC (SpO2 cần biên độ
   AC). Group delay ~ (N-1)/2 sample; là hằng số nên triệt tiêu khi lấy hiệu RR
   interval. Đây chỉ là bước làm mượt, KHÔNG phải tín hiệu lọc hoàn chỉnh / y tế.
   Cửa sổ chỉnh được lúc chạy (UI thông số lọc) tối đa _MAX, giá trị này định cỡ
   các backing buffer tĩnh. */
#define PPG_MOVING_AVERAGE_WINDOW  5U
#define PPG_MA_WINDOW_MAX          15U

/* -------- Waveform hiển thị -------- */
#define PPG_WAVE_POINTS            240U     /**< Số điểm vẽ (bằng bề rộng màn hình).  */
#define PPG_WAVE_ZERO              500      /**< Đường tâm trong dải 0..1000.         */
#define PPG_WAVE_FULL_SCALE        1000     /**< Khớp với gui WAVEFORM_FULL_SCALE.    */
#define PPG_DISPLAY_RANGE_MIN      200      /**< Biên độ centered +/- nhỏ nhất hiển thị.*/
#define PPG_DISPLAY_RANGE_MAX      120000   /**< Biên độ centered +/- lớn nhất hiển thị.*/
/* Biên an toàn khi auto-scale: dải +/- hiển thị = nửa biên độ tín hiệu * NUM/DEN
   (>1), nên CẢ đỉnh lẫn đáy nằm ở mức DEN/NUM của nửa chiều cao và luôn ở trong
   khung. 5/4 -> vệt sóng lấp ~80% chiều cao, chừa lề trên/dưới bằng nhau. Đồ thị
   cũng được căn theo điểm giữa tín hiệu (envelope) nên DC dư không đẩy lệch. */
#define PPG_DISPLAY_MARGIN_NUM     5
#define PPG_DISPLAY_MARGIN_DEN     4

/* -------- Phát hiện peak -------- */
#define PPG_MAX_PEAKS              12U      /**< Số marker peak mỗi cửa sổ.           */
#define PPG_PEAK_MIN_INTERVAL_MS   300U     /**< Giới hạn trên 200 BPM.               */
#define PPG_PEAK_MAX_INTERVAL_MS   1500U    /**< Giới hạn dưới 40 BPM.                */
#define PPG_PEAK_PROMINENCE        120      /**< Độ nhô tối thiểu trên ngưỡng.        */

/* -------- BPM -------- */
#define PPG_BPM_INTERVAL_COUNT     5U       /**< Số interval giữ lại để lấy median.   */
#define PPG_BPM_MIN_INTERVALS      3U       /**< Cần đủ trước khi hiển thị BPM.        */
#define PPG_BPM_MIN               40U
#define PPG_BPM_MAX               200U

/* -------- SpO2 (ratio of ratios) --------
   Tính trên RAW RED/IR qua cửa sổ trượt ghép từ các sub-block 1 giây (chỉ dùng
   running sum, không buffer lớn), cập nhật ~1 Hz. KHÔNG phải thiết bị y tế:
   calibration dưới đây là đường cong thực nghiệm MAX30102, thay được sau này. */
#define PPG_SPO2_UPDATE_MS         1000U   /**< Độ dài sub-block -> cập nhật ~1 Hz.   */
#define PPG_SPO2_BLOCKS            4U      /**< Cửa sổ = BLOCKS * UPDATE_MS (4 s).    */
#define PPG_SPO2_MIN_SAMPLES       200U    /**< Số sample tối thiểu trong cả cửa sổ.  */
#define PPG_SPO2_MIN_DC            40000   /**< DC tối thiểu (RED và IR) để hợp lệ.   */
#define PPG_SPO2_MIN_AC            15.0F   /**< AC RMS tối thiểu (RED và IR).         */
#define PPG_SPO2_SATURATION        255000  /**< DC vượt mức này => bão hòa.           */
#define PPG_SPO2_MIN_PERCENT       70.0F   /**< Sàn sinh lý khi hiển thị.             */
#define PPG_SPO2_MAX_PERCENT       100.0F  /**< Trần sinh lý khi hiển thị.            */
#define PPG_SPO2_MIN_SQI           60.0F   /**< SQI % tối thiểu để SpO2 hợp lệ.       */
/* Calibration thực nghiệm: SpO2 = A + B*R + C*R^2 (đường cong Maxim MAX30102). */
#define PPG_SPO2_CAL_A             94.845F
#define PPG_SPO2_CAL_B             30.354F
#define PPG_SPO2_CAL_C             (-45.060F)

/* -------- Phiên đo -------- */
/* Thời gian đo tối thiểu trước khi trung bình phiên được coi là hợp lệ, và một
   mốc mục tiêu chỉ dùng cho gợi ý tiến trình (đo có thể kéo dài hơn). */
#define MEASUREMENT_MIN_DURATION_MS    10000U
#define MEASUREMENT_TARGET_DURATION_MS 20000U
/* Số RR interval hợp lệ tối thiểu để trung bình BPM phiên hợp lệ. */
#define MEASUREMENT_MIN_RR_INTERVALS   5U
/* Số cửa sổ SpO2 hợp lệ tối thiểu để trung bình SpO2 phiên hợp lệ. */
#define MEASUREMENT_MIN_SPO2_WINDOWS   3U

#endif /* PPG_CONFIG_H */
