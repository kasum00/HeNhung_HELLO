# Xử lý tín hiệu PPG, lọc nhiễu, phát hiện đỉnh, tính BPM, SpO2 và SQI

---

## 1. Các file cốt lõi DSP

| File                      | Dòng | Mô tả                                                                                                                |
| ------------------------- | ---- | -------------------------------------------------------------------------------------------------------------------- |
| `DSP/ppg_types.h`         | —    | Định nghĩa kiểu: `PpgRawSample`, `PpgState`, `PpgInvalidReason`, `PpgFilterMode`, `PpgResult`                        |
| `DSP/ppg_measurement.h`   | —    | Header engine PPG — API: `Ppg_Init`, `Ppg_PushSample`, `Ppg_GetResult`, `Ppg_SetFilterMode`                          |
| `DSP/ppg_measurement.c`   | ~882 | **Engine chính** — finger detection, DC tracking, chuỗi lọc, peak detection, BPM, SpO2, SQI, waveform, state machine |
| `DSP/measurement_types.h` | —    | `MeasurementResultStatus`, `MeasurementEndReason`, `MeasurementHistoryRecord`                                        |

### `ppg_measurement.c` chi tiết

| Chức năng        | Mô tả                                                                                                 |
| ---------------- | ----------------------------------------------------------------------------------------------------- |
| Finger detection | Hysteresis với DC tracking (slow EMA), ngưỡng on/off xác nhận nhiều mẫu liên tiếp                     |
| DC baseline      | Adaptive EMA cho IR & RED, centering                                                                  |
| Chuỗi lọc        | RAW, Moving Average, Median, Lowpass Butterworth, Median+Lowpass cascade                              |
| Peak detection   | Rising-edge local max, amplitude threshold, prominence check, interval gating 300–1500ms (40–200 BPM) |
| BPM              | Median 5 RR intervals cuối → instant BPM, track min/max/average phiên                                 |
| SQI              | `100 × acceptedPeaks / (acceptedPeaks + rejectedPeaks)`                                               |
| SpO2             | Feed RAW RED/IR vào `Spo2_Process` mỗi mẫu, track avg/min/max phiên                                   |
| Waveform         | Auto-ranging, map vào `[0..1000]`, ring buffer 240 điểm + peak flags                                  |
| State machine    | IDLE → WAIT_FINGER → STABILIZING → MEASURING → RESULT_READY                                           |
| Quản lý phiên    | Reset finger-on, finalize finger-off (VALID/PARTIAL/INVALID)                                          |

---

## 2. Thuật toán lọc nhiễu

| File                                 | Dòng | Thuật toán                                                     | Độ phức tạp    |
| ------------------------------------ | ---- | -------------------------------------------------------------- | -------------- |
| `DSP/moving_average_filter.h` / `.c` | ~71  | Running-sum sliding window — trừ cũ, cộng mới, chia count      | O(1)/mẫu       |
| `DSP/median_filter.h` / `.c`         | ~61  | Copy ring buffer → insertion sort → lấy phần tử giữa           | O(N log N)/mẫu |
| `DSP/lowpass_filter.h` / `.c`        | ~54  | Butterworth bậc 2, fc=4Hz, fs=100Hz, Direct Form II Transposed | O(1)/mẫu       |

### Moving Average Filter

| Thành phần | Chi tiết                                               |
| ---------- | ------------------------------------------------------ |
| Cấu trúc   | Ring buffer, running sum, capacity, count, writeIndex  |
| API        | `MovingAverage_Init`, `_Reset`, `_Process`, `_IsReady` |
| Hành vi    | Trả `NOT_READY` khi đang đầy, `OK` khi cửa sổ đầy      |
| DC bias    | Không có (chia nguyên, đối xứng)                       |

### Median Filter

| Thành phần | Chi tiết                                               |
| ---------- | ------------------------------------------------------ |
| Cấu trúc   | Ring buffer + sort buffer, capacity, count, writeIndex |
| API        | `Median_Init`, `_Process`, `_Reset`, `_IsReady`        |
| Ưu điểm    | Loại nhiễu xung (spikes) mà không làm mờ đỉnh          |

### Lowpass Filter (Butterworth)

| Thành phần | Chi tiết                                            |
| ---------- | --------------------------------------------------- |
| Cấu trúc   | IIR coefficients (b0/b1/b2, a1/a2), state (w1/w2)   |
| API        | `Lowpass_Init`, `_InitCoeffs`, `_Reset`, `_Process` |
| Thông số   | fc = 4 Hz, fs = 100 Hz                              |
| Hệ số b    | [0.020083, 0.040167, 0.020083]                      |
| Hệ số a    | [1.0, -1.561018, 0.641352]                          |
| Chức năng  | Loại bỏ nhiễu tần số cao >4 Hz                      |

---

## 3. Đo SpO2

| File                          | Dòng | Mô tả                                     |
| ----------------------------- | ---- | ----------------------------------------- |
| `DSP/spo2_estimator.h` / `.c` | ~175 | Ratio-of-ratios trên RAW RED/IR, O(1)/mẫu |

| Thành phần | Chi tiết                                                                         |
| ---------- | -------------------------------------------------------------------------------- |
| Cấu trúc   | `Spo2Estimator` (accumulator + coefficients), `Spo2Result`, `Spo2Status`         |
| API        | `Spo2_Init`, `_Reset`, `_Process`                                                |
| Cửa sổ     | Sub-block 1s, 4 blocks = ~4 giây                                                 |
| DC         | Mean (tổng chạy / số mẫu)                                                        |
| AC         | RMS = `√(E[x²] - E[x]²)`                                                         |
| Tỷ số R    | `(AC_RED/DC_RED) / (AC_IR/DC_IR)`                                                |
| Hiệu chuẩn | **SpO2 = A + B·R + C·R²** (Maxim MAX30102)                                       |
| Hệ số      | A = 94.845, B = 30.354, C = -45.060                                              |
| Kiểm tra   | Min samples, DC range, AC magnitude, no saturation, R finite, SpO2 ∈ [70%, 100%] |

---

## 4. Cấu hình tham số

### `Config/ppg_config.h` (~101 dòng)

| Nhóm             | Tham số                  | Giá trị                   |
| ---------------- | ------------------------ | ------------------------- |
| Sampling         | `PPG_SAMPLE_RATE_HZ`     | 100 Hz                    |
| Finger detection | DC on / off              | 50 000 / 30 000           |
| Finger detection | Mẫu xác nhận on / off    | 15 / 25                   |
| Ổn định          | Min / Max                | 2.5s / 8s                 |
| DC centering     | `PPG_BASELINE_SHIFT`     | 9                         |
| Moving average   | Window mặc định / Max    | 5 / 15                    |
| Waveform         | Số điểm                  | 240                       |
| Waveform         | Margin ratio             | 5/4                       |
| Peak detection   | Min interval (→ max BPM) | 300 ms (200 BPM)          |
| Peak detection   | Max interval (→ min BPM) | 1500 ms (40 BPM)          |
| Peak detection   | Prominence               | 120                       |
| BPM              | Số intervals median      | 5                         |
| BPM              | Min intervals hiển thị   | 3                         |
| BPM              | Khoảng                   | 40–200                    |
| SpO2             | Sub-block / Cửa sổ       | 1s / 4 blocks             |
| SpO2             | Min mẫu                  | 200                       |
| SpO2             | Hiệu chuẩn A / B / C     | 94.845 / 30.354 / -45.060 |
| Phiên đo         | Min / Target             | 10s / 20s                 |
| Phiên đo         | Min RR intervals         | 5                         |
| Phiên đo         | Min SpO2 windows         | 3                         |

### `Config/hw_config.h` (~108 dòng)

| 外设     | Chi tiết                                            |
| -------- | --------------------------------------------------- |
| I2C3     | MAX30102 (0x57) + DS1307 (0x68), bus chung, 100 kHz |
| MAX30102 | Poll 20ms, dung sai 5 lỗi liên tiếp                 |
| LED      | PG13, PG14                                          |
| Buzzer   | TIM10_CH1 (PF6)                                     |
| UART     | USART1 @ 921 600 baud                               |
| Nút B1   | PA0 EXTI                                            |

### `Config/alert_config.h`

| Param          | Giá trị  |
| -------------- | -------- |
| BPM thấp       | 60       |
| BPM cao        | 100      |
| SpO2 thấp      | 90       |
| Delay xác nhận | 2 000 ms |
| Delay bỏ       | 3 000 ms |
| LED blink step | 300 ms   |

---

## 5. Cấp dữ liệu thô từ cảm biến

| File                                        | Dòng | Mô tả                                                                                                    |
| ------------------------------------------- | ---- | -------------------------------------------------------------------------------------------------------- |
| `Drivers/MAX30102/max30102_driver.h` / `.c` | ~214 | Driver I2C MAX30102 — init, FIFO burst read 18-bit RED/IR, overflow counter. 100 Hz, 18-bit, LED ~6.4 mA |
| `Utilities/ppg_sample_queue.h` / `.c`       | ~65  | Queue SPSC lock-free, ring buffer 256 mẫu power-of-2, `__DMB()`, không cần mutex trên Cortex-M           |

---

## 6. Task điều phối (FreeRTOS)

| File                                         | Dòng | Mô tả                                                                                                      |
| -------------------------------------------- | ---- | ---------------------------------------------------------------------------------------------------------- |
| `Application/dsp_task.h` / `.c`              | ~406 | FreeRTOS thread 10ms tick — consume queue → feed PPG engine → publish seqlock → finalization → alert →遥测 |
| `Application/app_init.c`                     | ~173 | Sensor task poll FIFO 20ms → đẩy mẫu vào queue, quản lý mutex I2C, bus recovery                            |
| `Application/medical_alert_service.h` / `.c` | —    | Alert BPM/SpO2 với hysteresis thời gian                                                                    |

---

## 7. Giao diện GUI (TouchGFX)

| File                                  | Dòng | Mô tả                                                                                                  |
| ------------------------------------- | ---- | ------------------------------------------------------------------------------------------------------ |
| `IGuiDataProvider.hpp`                | —    | Interface: `getMeasurementSnapshot`, `getWaveformSnapshot`, `getHistoryPage`, `tick`, `postCommand`    |
| `GuiTypes.hpp`                        | ~298 | Enum: `MeasurementState`, `FilterMode`, `MockScenario`, `SignalQuality`. Hằng số: WAVEFORM 240/12/1000 |
| `GuiSnapshots.hpp`                    | —    | `GuiMeasurementSnapshot` (BPM, SpO2, SQI), `GuiWaveformSnapshot` (IR/RED 240 điểm, peaks)              |
| `application_gui_bridge.hpp` / `.cpp` | ~296 | Bridge thực — đọc PpgResult qua seqlock, ánh xạ enum PPG → GUI, điền snapshots                         |

---

## 8. Luồng dữ liệu

```
MAX30102 (I2C)
    ↓
app_init.c — sensor task poll FIFO mỗi 20ms
    ↓
PpgRawSample (RED/IR 18-bit) → PpgQueue (SPSC lock-free 256 mẫu)
    ↓
dsp_task.c — FreeRTOS thread 10ms
    ↓
ppg_measurement.c — Engine chính
    ├── DC baseline tracking + centering
    ├── Chuỗi lọc (MA / Median / Lowpass Butterworth / Cascade)
    ├── runPeakDetector (rising edge + prominence + interval gating)
    ├── BPM = median(RR intervals)
    ├── SpO2 = spo2_estimator.c (ratio-of-ratios trên RAW)
    ├── SQI = accepted / (accepted + rejected peaks) × 100
    └── Waveform 240 điểm + peak markers
    ↓
Seqlock publish
    ↓
ApplicationGuiBridge → IGuiDataProvider
    ↓
TouchGFX Views (DashboardView, WaveformView, HistoryRow)
```

---

## 9. Thứ tự đọc gợi ý

| Bước | File                          | Mục đích                 |
| ---- | ----------------------------- | ------------------------ |
| 1    | `Config/ppg_config.h`         | Tất cả ngưỡng và tham số |
| 2    | `DSP/ppg_types.h`             | Kiểu dữ liệu chung       |
| 3    | `DSP/moving_average_filter.c` | Lọc trung bình trượt     |
| 4    | `DSP/median_filter.c`         | Lọc trung vị             |
| 5    | `DSP/lowpass_filter.c`        | Lọc Butterworth          |
| 6    | `DSP/spo2_estimator.c`        | Đo SpO2 ratio-of-ratios  |
| 7    | `DSP/ppg_measurement.c`       | Engine tổng hợp          |
| 8    | `Application/dsp_task.c`      | Task FreeRTOS            |
