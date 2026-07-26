# Hướng dẫn chi tiết dự án PPG Analyzer

## Mục lục

1. [Tổng quan dự án](#1-tổng-quan-dự-án)
2. [Kiến trúc phần cứng](#2-kiến-trúc-phần-cứng)
3. [Kiến trúc phần mềm](#3-kiến-trúc-phần-mềm)
4. [Dòng dữ liệu (Data Flow)](#4-dòng-dữ-liệu-data-flow)
5. [Chi tiết từng module](#5-chi-tiết-từng-module)
6. [Cấu hình và hiệu chỉnh](#6-cấu-hình-và-hiệu-chỉnh)
7. [Hướng dẫn Build và nạp chương trình](#7-hướng-dẫn-build-và-nạp-chương-trình)
8. [Debug và kiểm tra](#8-debug-và-kiểm-tra)

---

## 1. Tổng quan dự án

Dự án là một máy đo **nhịp tim (BPM)** và **nồng độ oxy máu (SpO2)** hoạt động trên-board **STM32F429I-DISCO**. Cảm biến quang **MAX30102** thu tín hiệu PPG (Photoplethysmography) từ ngón tay, DSP xử lý tín hiệu để trích xuất BPM/SpO2, và **TouchGFX** hiển thị kết quả lên màn hình LCD 240x320 px.

### Yêu cầu chức năng

| # | Chức năng | Trạng thái |
|---|----------|-----------|
| 1 | Liên tục lấy BPM và SpO2 từ cảm biến MAX30102 | ✅ |
| 2 | Hiển thị giá trị real-time dạng số (Dashboard) | ✅ |
| 3 | Hiển thị đồ thị line (Waveform oscilloscope) | ✅ |
| 4 | Nút B1 chuyển đổi giữa Dashboard ↔ Waveform | ✅ |
| 5 | Truyền dữ liệu realtime qua USART (921600 baud) | ✅ |
| 6 | Cảnh báo khi BPM/SpO2 vượt ngưỡng y tế | ✅ |
| 7 | Nháy LED PG13/PG14 khi vượt ngưỡng | ✅ |

### Cấu hình ngưỡng y tế (chỉ để học tập)

| Thông số | Ngưỡng thấp | Ngưỡng cao | Ghi chú |
|----------|-------------|-----------|---------|
| BPM | < 45 | > 120 | Bradycardia / Tachycardia |
| SpO2 | < 92% | — | Hypoxia |

> **Lưu ý:** Đây KHÔNG phải thiết bị y tế được chứng nhận.

---

## 2. Kiến trúc phần cứng

### 2.1 Sơ đồ khối tổng quát

```
┌─────────────────────────────────────────────────────────┐
│                    STM32F429ZIT6                        │
│                                                         │
│  ┌──────────┐  I2C3   ┌────────────┐                   │
│  │MAX30102  │◄────────│  Sensor    │  ← PPG quang      │
│  │(PPG)     │  PA8/PC9│  Task      │     ngón tay       │
│  └──────────┘         └─────┬──────┘                   │
│                             │ PpgQueue (lock-free)      │
│  ┌──────────┐         ┌─────▼──────┐                   │
│  │ DS1307   │◄────────│  DSP Task  │  ← Xử lý BPM/SpO2│
│  │ (RTC)    │  I2C3   │  (10ms)    │                    │
│  └──────────┘         └─────┬──────┘                   │
│                             │ PpgResult (seqlock)       │
│  ┌──────────┐         ┌─────▼──────┐                   │
│  │ ILI9341  │◄────────│ TouchGFX   │  ← LCD 240x320    │
│  │ (LCD)    │  SPI5   │ Task       │                    │
│  │ +LTDC    │         └─────┬──────┘                   │
│  │ +SDRAM   │               │                          │
│  └──────────┘               │                          │
│                             │                          │
│  ┌──────────┐         ┌─────▼──────┐                   │
│  │ USART1   │────────►│ Telemetry  │  ← CSV về PC      │
│  │ 921600   │  TX IT  │ Task       │                    │
│  └──────────┘         └────────────┘                   │
│                                                         │
│  PA0 (B1) ──EXTI──► PhysicalInput                      │
│  PG13/PG14 ◄──GPIO── AlertLed (nháy luân phiên)       │
│  PF6 (TIM10) ◄──PWM── Buzzer                          │
└─────────────────────────────────────────────────────────┘
```

### 2.2 Bảng chân GPIO

| Chân | Vai trò | Ghi chú |
|------|---------|---------|
| **PA0** | Nút B1 | EXTI rising edge, pull-down, debounce 200ms |
| **PA8** | I2C3 SCL | Dùng chung MAX30102 + DS1307, 100 kHz |
| **PA9** | USART1 TX | Telemetry CSV về PC |
| **PA10** | USART1 RX | (chưa dùng) |
| **PC9** | I2C3 SDA | |
| **PF6** | TIM10_CH1 | PWM buzzer passiv (AF3) |
| **PF7/8/9** | SPI5 | LCD ILI9341 (SCK/MISO/MOSI) |
| **PG13** | LED LD3 | Alert LED 1 (active-high) |
| **PG14** | LED LD4 | Alert LED 2 (active-high) |

### 2.3 Xung nhịp và bus

| Bus | Tốc độ | Ngoại vi |
|-----|--------|---------|
| SYSCLK | 180 MHz | PLL: HSE 8MHz → 180MHz, Over-Drive |
| APB1 | 45 MHz | TIM2-7, USART2-3 |
| APB2 | 90 MHz | TIM1, TIM10, SPI5, USART1, LTDC |
| I2C3 | 100 kHz | MAX30102 + DS1307 (dùng chung, mutex) |
| SPI5 | 5.625 Mbit/s | ILI9341 LCD |

---

## 3. Kiến trúc phần mềm

### 3.1 Sơ đồ các lớp (Layered Architecture)

```
┌───────────────────────────────────────────────────────┐
│                   PRESENTATION LAYER                  │
│  TouchGFX Views (8 screens) + Widgets (7 widgets)    │
│  ← Đọc data từ IGuiDataProvider, hiển thị           │
└───────────────────────┬───────────────────────────────┘
                        │ IGuiDataProvider interface
┌───────────────────────▼───────────────────────────────┐
│                    BRIDGE LAYER                       │
│  ApplicationGuiBridge (target) / MockGuiDataProvider  │
│  ← Map DSP types → GUI types, mỗi tick (~60 Hz)     │
└───────────────────────┬───────────────────────────────┘
                        │ PpgResult (seqlock publish)
┌───────────────────────▼───────────────────────────────┐
│                  APPLICATION LAYER                    │
│  DspTask │ SensorTask │ MedicalAlert │ AlertLed       │
│  Telemetry │ PhysicalInput │ Buzzer │ RTC Service     │
└───────────────────────┬───────────────────────────────┘
                        │ PpgRawSample (lock-free queue)
┌───────────────────────▼───────────────────────────────┐
│                     DSP LAYER                         │
│  ppg_measurement │ spo2_estimator │ filters           │
│  ← Finger detect → DC removal → Peak detect → BPM   │
└───────────────────────┬───────────────────────────────┘
                        │
┌───────────────────────▼───────────────────────────────┐
│                    DRIVER LAYER                       │
│  MAX30102 │ DS1307 │ StatusLED │ Buzzer │ STMPE811   │
│  ← HAL I2C3 / HAL GPIO / HAL TIM                     │
└───────────────────────────────────────────────────────┘
```

### 3.2 FreeRTOS Tasks

Dự án chạy **4 task** chính trên FreeRTOS (CMSIS-RTOS V2):

| Task | Stack | Priority | Vai trò | Tần suất |
|------|-------|----------|---------|---------|
| **defaultTask** | 512 bytes | Normal | Sensor polling + LED + buzzer | 20 ms (50 Hz) |
| **GUI_Task** | 32 KB | Normal | TouchGFX rendering + UI | ~16 ms (60 Hz) |
| **dspTask** | 4 KB | Normal | DSP engine PPG | 10 ms (100 Hz) |
| **telemetryTask** | 1 KB | Normal | UART TX CSV | Event-driven |

### 3.3 Nguyên tắc phân tách

1. **TouchGFX không chạy DSP** — DSP engine chạy trong task riêng, GUI chỉ đọc kết quả
2. **DSP không chạm GPIO** — DSP cập nhật cờ cảnh báo, LED task nháy theo cờ
3. **Chỉ TelemetryTask sở hữu USART1** — Các task khác publish vào queue, TelemetryTask format + truyền
4. **I2C3 dùng mutex** — MAX30102 và DS1307 share bus, truy cập tuần tự qua mutex
5. **Config集中** — Tất cả ngưỡng, chân GPIO, timing nằm ở `Config/`, không hard-code trong driver

---

## 4. Dòng dữ liệu (Data Flow)

### 4.1 Từ cảm biến đến màn hình

```
MAX30102 (100 Hz)
    │
    ▼ MAX30102_ReadFifo() — 16 samples/lần
SensorTask (20 ms)
    │
    ▼ PpgQueue_Push() — lock-free SPSC queue
PpgQueue
    │
    ▼ PpgQueue_Pop() — DSP task rút
DSP Task (10 ms)
    │
    ├──► Ppg_PushSample() — finger detect, DC removal, peak detect
    │       │
    │       ▼ Ppg_GetResult() — snapshot BPM, SpO2, waveform
    │
    ├──► MedicalAlert_Update() — so sánh ngưỡng, hysteresis 2s/3s
    │       │
    │       ▼ MedicalAlert_GetActiveFlags() — cờ BPM_LOW/BPM_HIGH/SPO2_LOW
    │
    ├──► publishResult() — seqlock → GUI chỉ đọc
    │       │
    │       ▼ DspTask_GetResult() — GUI bridge đọc
    │
    └──► Telemetry_PublishPpgSample() — hàng đợi UART

TouchGFX Task (60 Hz)
    │
    ▼ IGuiDataProvider::getMeasurementSnapshot()
ApplicationGuiBridge::tick()
    │
    ├──► DashboardView: MetricCard BPM, SpO2, SQI
    └──► WaveformView: Canvas 240px đồ thị line

SensorTask (20 ms)
    │
    └──► AlertLed_Process(MedicalAlert_IsActive())
            │
            └──► PG13/PG14 nháy luân phiên 300ms
```

### 4.2 Dòng dữ liệu UART (Telemetry)

```
DSP Task ──► Telemetry_PublishPpgSample()  ──┐
DSP Task ──► Telemetry_PublishVitalResult() ──┤
DSP Task ──► Telemetry_PublishAlert()        ──┤  Hàng đợi
DSP Task ──► Telemetry_PublishSessionStart() ──┤  (event + waveform)
DSP Task ──► Telemetry_PublishSessionEnd()   ──┤
GUI      ──► Telemetry_PublishScreenChange() ──┤
GUI      ──► Telemetry_PublishUserAction()   ──┘
                                                    │
                                            TelemetryTask
                                                    │
                                            Format CSV (snprintf)
                                                    │
                                            HAL_UART_Transmit_IT()
                                                    │
                                              USART1 @ 921600
                                                    │
                                                  PC
```

**Định dạng CSV mẫu:**

```csv
DATA,<timestamp>,<seq>,<redRaw>,<irRaw>,<redCentered>,<irCentered>,<redFiltered>,<irFiltered>,<bpm>,<spo2>,<sqi>,<state>,<filter>,<window>,<bpmValid>,<spo2Valid>
VITAL,<timestamp>,<bpm>,<avgBpm>,<spo2>,<avgSpo2>,<sqi>,<state>,<bpmValid>,<spo2Valid>
ALERT,<timestamp>,<flag>,<value>,<threshold>,<active>
SESSION_START,<timestamp>,<sessionId>
SESSION_END,<timestamp>,<sessionId>,<durationMs>,<avgBpm>,<avgSpo2>,<status>,<endReason>
```

---

## 5. Chi tiết từng module

### 5.1 DSP Layer (`DSP/`)

Đây là trái tim xử lý tín hiệu. Tất cả file thuần C, không phụ thuộc HAL, có thể test trên desktop.

#### 5.1.1 PPG Measurement Engine (`ppg_measurement.c` — 826 dòng)

**Máy trạng thái chính:**

```
IDLE ──(StartMeasurement)──► WAIT_FINGER
                                  │
                           (finger detected)
                                  │
                                  ▼
                            STABILIZING ──(2.5-8s)──► MEASURING
                                  ▲                        │
                           (weak signal)                  │
                                  └────────────────────────┘
                                                          │
                                                   (finger removed)
                                                          │
                                                          ▼
                                                    RESULT_READY
                                                          │
                                                   (next sample)
                                                          │
                                                          ▼
                                                        IDLE
```

**Xử lý mỗi sample:**

1. **Finger detection** — Theo dõi DC level của IR qua IIR chậm. On threshold: 50000 (15 sample xác nhận). Off threshold: 30000 (25 sample xác nhận). Có hysteresis để tránh nhảy cóc.

2. **DC Removal** — Trừ baseline thích nghi: `centered = raw - baseline`. Baseline di chuyển chậm qua `>> 9` (alpha ≈ 1/512).

3. **Filtering** — 4 chế độ selectable:
   - RAW: tín hiệu gốc đã trừ DC
   - MOVING_AVERAGE: cửa sổ N (3-15), O(1) mỗi sample
   - MEDIAN: cửa sổ N, loại spike
   - LOWPASS: Butterworth 2nd-order, fc=4Hz, fs=100Hz

4. **Peak Detection** — Rising-edge local maximum:
   - Biên độ > `PPG_PEAK_PROMINENCE` (120)
   - Khoảng cách ≥ 300ms (≤ 200 BPM) và ≤ 1500ms (≥ 40 BPM)
   - Loại double-peak

5. **BPM Calculation** — Ring buffer 5 RR intervals, lấy median. Chỉ hiển thị khi ≥ 3 intervals hợp lệ.

6. **SpO2 Estimation** — Ratio-of-ratios trên raw RED/IR:
   - Cửa sổ 4 sub-blocks × 1s = 4s
   - R = (AC_RED/DC_RED) / (AC_IR/DC_IR)
   - SpO2 = 94.845 + 30.354×R − 45.060×R²
   - Chỉ hợp lệ khi SQI ≥ 60%

7. **Waveform** — Map centered IR vào buffer 240 điểm, auto-scale theo envelope.

#### 5.1.2 SpO2 Estimator (`spo2_estimator.c` — 175 dòng)

```
┌─────────────────────────────────────────┐
│            Cửa sổ trượt 4 giây          │
│  ┌──────┬──────┬──────┬──────┐         │
│  │Block0│Block1│Block2│Block3│ ← ring  │
│  │ 1sec │ 1sec │ 1sec │ 1sec │  buffer │
│  └──────┴──────┴──────┴──────┘         │
│                                         │
│  Mỗi block: sum(RED), sum(IR),         │
│              sumSq(RED), sumSq(IR),     │
│              count                      │
│                                         │
│  Khi tất cả block đầy:                  │
│    DC = mean = sum / count              │
│    AC = sqrt(variance) = sqrt(sumSq/n - mean²)│
│    R = (AC_RED/DC_RED) / (AC_IR/DC_IR) │
│    SpO2 = A + B×R + C×R²              │
└─────────────────────────────────────────┘
```

#### 5.1.3 Bộ lọc

| File | Loại | Độ phức tạp | Ghi chú |
|------|------|------------|---------|
| `moving_average_filter.c` | Trung bình trượt | O(1)/sample | Ring buffer + running sum |
| `median_filter.c` | Trung vị | O(N log N)/sample | Copy + insertion sort |
| `lowpass_filter.c` | Butterworth 2nd-order IIR | O(1)/sample | Direct Form II Transposed |

**Hệ số Butterworth fc=4Hz, fs=100Hz:**
```
b = [0.020083, 0.040167, 0.020083]
a = [1.0, -1.561018, 0.641352]
```

### 5.2 Application Layer (`Application/`)

#### 5.2.1 Sensor Task (`app_init.c` → `App_DefaultTask()`)

```
Vòng lặp mỗi 20ms:
  1. Buzzer_Process()           — máy trạng thái buzzer
  2. AlertLed_Process()         — nháy LED PG13/PG14
  3. I2C_Lock() → MAX30102_ReadFifo(16 samples) → I2C_Unlock()
  4. Push mỗi sample vào PpgQueue
  5. Nếu lỗi bus liên tiếp ≥ 5 lần → g_sensorOk = 0
  6. Nếu lỗi bus ≥ 10 lần → thử re-init I2C
  7. Mỗi ~1 giây: poll DS1307 RTC (nếu có)
  8. osDelay(20ms)
```

#### 5.2.2 DSP Task (`dsp_task.c`)

```
Vòng lặp mỗi 10ms:
  1. applyPendingRequests()     — áp dụng filter mode/window từ GUI
  2. Ppg_SetSensorError()       — báo lỗi cảm biến
  3. Ppg_ReportLoss()           — thống kê sample mất
  4. PpgQueue_Pop() → Ppg_PushSample()  — xử lý tối đa 512 sample/lần
  5. Ppg_GetResult()            — lấy snapshot
  6. handleFinalize()           — lưu lịch sử khi nhấc ngón tay
  7. publishResult()            — seqlock → GUI
  8. updateAlertsAndTelemetry() — cảnh báo + telemetry
  9. osDelay(10ms)
```

**Seqlock publish (lock-free, không block GUI):**
```c
// DSP thread ghi:
s_pubGen++;           // -> lẻ: đang ghi
__DMB();              // Memory barrier
s_pub = *result;
__DMB();
s_pubGen++;           // -> chẵn: ổn định

// GUI thread đọc:
do {
    g0 = s_pubGen;
    __DMB();
    *out = s_pub;
    __DMB();
} while (g0 != s_pubGen);  // Thử lại nếu đang ghi
```

#### 5.2.3 Medical Alert Service (`medical_alert_service.c`)

```
Input:  BPM, SpO2, signalValid, measurementState
Output: MEDICAL_ALERT_BPM_LOW | BPM_HIGH | SPO2_LOW

Chống nhiễu (hysteresis thời gian):
  - Kích hoạt: phải vượt ngưỡng liên tục 2000ms
  - Gỡ bỏ: phải trở lại bình thường liên tục 3000ms
  - Chỉ đánh giá khi: measurementState == MEASURING
                      && signalValid == true
                      && bpmValid/spo2Valid == true
  - Reset ngay khi nhấc ngón tay (MedicalAlert_Reset)
```

#### 5.2.4 Alert LED Pattern (`alert_led_pattern.c`)

```
Khi MedicalAlert_IsActive() == true:
  PG13 ON  + PG14 OFF  → 300ms →
  PG13 OFF + PG14 ON   → 300ms →
  (lặp lại)

Khi MedicalAlert_IsActive() == false:
  Cả hai LED tắt
```

#### 5.2.5 Physical Input Service (`physical_input_service.c`)

```
PA0 (B1) → EXTI0 IRQ → HAL_GPIO_EXTI_Callback
  │
  ├── Debounce 200ms (so sánh timestamp)
  └── ++s_pressCount (atomic, ISR-safe)

GUI thread:
  PhysicalInput_GetEvent() → đọc s_pressCount, so sánh với s_consumedCount
  → trả về B1_PRESSED nếu có nhấn mới
  → FrontendApplication: toggle Dashboard ↔ Waveform
```

#### 5.2.6 Application GUI Bridge (`application_gui_bridge.cpp`)

Nối giữa DSP layer (C) và TouchGFX GUI (C++). Mỗi frame (~60 Hz):

```cpp
void ApplicationGuiBridge::tick(uint32_t frameCounter) {
    DspTask_GetResult(&ppg_);    // Đọc seqlock, copy kết quả
    // ... map PpgResult → GuiMeasurementSnapshot
}

bool getMeasurementSnapshot(GuiMeasurementSnapshot& s) {
    s.bpm = ppg_.bpm;
    s.spo2Percent = ppg_.spo2;
    s.state = mapState(ppg_.state);
    // ... ~45 fields
}
```

### 5.3 TouchGFX GUI (`TouchGFX/gui/`)

#### 5.3.1 8 Màn hình

| # | Screen | Chức năng | File |
|---|--------|----------|------|
| 1 | **Boot** | Splash screen, progress bar, init sequence | `boot_screen/` |
| 2 | **Home** | Menu 6 nút: Measure, Waveform, History, Settings, About, Clock | `home_screen/` |
| 3 | **Dashboard** | BPM, SpO2, SQI dạng số + Start/Stop + trạng thái | `dashboard_screen/` |
| 4 | **Waveform** | Đồ thị oscilloscope 240px, peak markers, IR/RED toggle | `waveform_screen/` |
| 5 | **History** | Danh sách phiên đo đã lưu (5 bản/trang) | `history_screen/` |
| 6 | **Settings** | Filter mode, Min SQI, Logging, Buzzer, LED, Brightness | `settings_screen/` |
| 7 | **About** | Thông tin firmware, MCU, sensor, algorithm | `about_screen/` |
| 8 | **DateTime** | Đọc/ghi RTC DS1307 (stepper +/-) | `datetimesettings_screen/` |

#### 5.3.2 7 Widget tái sử dụng

| Widget | Mô tả | Dùng ở |
|--------|-------|--------|
| `TopBar` | Header + nút back | Tất cả màn hình |
| `MetricCard` | Card: caption + value + unit | Dashboard (BPM, SpO2, SQI) |
| `StatusBadge` | Pill màu + label trạng thái | Dashboard |
| `TextButton` | Nút bấm có callback | Tất cả màn hình |
| `HistoryRow` | 2 dòng: ngày giờ/BPM, SpO2/SQI | History |
| `InfoRow` | Label trái / value phải | About |
| `PlaceholderContent` | "Chưa triển khai" stub | (không dùng) |

#### 5.3.3 Kiến trúc MVP

```
View ←── Presenter ←── Model ←── IGuiDataProvider
  │         │              │              │
  │     expose data()      │         ┌────┴────┐
  │     postCommand()      │         │         │
  │                        │    MockGui    AppGui
  │                        │    Provider   Bridge
  │                        │    (sim)      (target)
  └── tick() gọi ──────────┘
      data().getMeasurementSnapshot()
```

- **View**: Xây widgets trong `setupScreen()`, đọc data trong `handleTickEvent()`
- **Presenter**: Bridge mỏng, expose `data()` và `postCommand()`
- **Model**: Sở hữu `IGuiDataProvider`, gọi `tick()` mỗi frame
- **Commands**: Union typed — `StartMeasurement`, `SelectFilter`, `SetFilterWindow`, etc.

#### 5.3.4 Navigation

Tất cả chuyển màn hình đi qua `FrontendApplication::requestScreen(ScreenId)`:

```
Boot → Home → Dashboard ←→ Waveform
              Home → History
              Home → Settings
              Home → About
              Home → DateTimeSettings
```

Nút B1 vật lý: **chỉ toggle Dashboard ↔ Waveform** (trên các màn hình khác, B1 bị bỏ qua).

---

## 6. Cấu hình và hiệu chỉnh

### 6.1 File cấu hình tập trung

| File | Nội dung | Vai trò |
|------|---------|---------|
| [hw_config.h](Config/hw_config.h) | Chân GPIO, bus, địa chỉ I2C, timing debounce | Ánh xạ phần cứng |
| [ppg_config.h](Config/ppg_config.h) | Ngưỡng finger detect, peak, BPM, SpO2, display | DSP tuning |
| [alert_config.h](Config/alert_config.h) | Ngưỡng BPM/SpO2, hysteresis time, LED timing | Cảnh báo y tế |
| [buzzer_melodies.h](Config/buzzer_melodies.h) | Định nghĩa giai điệu buzzer | Audio feedback |

### 6.2 Các thông số DSP quan trọng

```c
// ppg_config.h

// Tần số lấy mẫu
#define PPG_SAMPLE_RATE_HZ         100U    // 100 Hz

// Phát hiện ngón tay (IR DC level)
#define PPG_FINGER_ON_THRESHOLD    50000U  // Có ngón tay
#define PPG_FINGER_OFF_THRESHOLD   30000U  // Nhấc ngón tay
#define PPG_FINGER_ON_SAMPLES      15U     // Xác nhận đặt (150ms)
#define PPG_FINGER_OFF_SAMPLES     25U     // Xác nhận nhấc (250ms)

// Ổn định tín hiệu
#define PPG_STABILIZE_MIN_MS       2500U   // Tối thiểu 2.5s
#define PPG_STABILIZE_MAX_MS       8000U   // Tối đa 8s

// Peak detection
#define PPG_PEAK_MIN_INTERVAL_MS   300U    // ≤ 200 BPM
#define PPG_PEAK_MAX_INTERVAL_MS   1500U   // ≥ 40 BPM
#define PPG_PEAK_PROMINENCE        120     // Biên độ tối thiểu

// BPM
#define PPG_BPM_INTERVAL_COUNT     5       // Số RR intervals lấy median
#define PPG_BPM_MIN_INTERVALS      3       // Cần ≥ 3 để hiển thị

// SpO2
#define PPG_SPO2_UPDATE_MS         1000U   // Sub-block 1 giây
#define PPG_SPO2_BLOCKS            4       // Cửa sổ 4 giây
#define PPG_SPO2_MIN_SQI           60.0F   // SQI tối thiểu

// Waveform
#define PPG_WAVE_POINTS            240U    // Bằng bề rộng LCD
```

### 6.3 Các ngưỡng cảnh báo

```c
// alert_config.h

#define ALERT_BPM_LOW_THRESHOLD     45.0F   // Bradycardia
#define ALERT_BPM_HIGH_THRESHOLD    120.0F  // Tachycardia
#define ALERT_SPO2_LOW_THRESHOLD    92.0F   // Hypoxia

#define ALERT_CONFIRMATION_MS       2000U   // 2 giây xác nhận
#define ALERT_CLEAR_MS              3000U   // 3 giây gỡ bỏ
#define ALERT_LED_STEP_MS           300U    // Nháy 300ms/pha
```

---

## 7. Hướng dẫn Build và nạp chương trình

### 7.1 Yêu cầu môi trường

| Tool | Phiên bản | Ghi chú |
|------|----------|---------|
| STM32CubeIDE | 1.14+ | IDE chính (Eclipse-based) |
| Keil MDK-ARM | 5.38+ | Alternative (`.uvprojx`) |
| IAR EWARM | 9.40+ | Alternative (`.ewp`) |
| STM32CubeMX | 6.14+ | Chỉ cần nếu muốn regenerate `.ioc` |
| ST-Link driver | Latest | Trên Windows, cài ST-Link Utility |

### 7.2 Build với STM32CubeIDE

1. **Mở project**: `File → Import → Existing Projects into Workspace` → chọn thư mục `HeNhung_HELLO/STM32CubeIDE/`

2. **Build**: `Project → Build All` (Ctrl+B)

3. **Nạp chương trình**: `Run → Debug As → STM32 C/C++ Application` → chọn `.elf` file

4. **Cấu hình debug**: ST-Link, SWD, speed 4000 kHz

### 7.3 Build với Makefile (GCC arm-none-eabi)

```bash
cd HeNhung_HELLO/gcc

# Build
make -j$(nproc)

# Nạp qua ST-Link
st-flash write build/HeNhung_HELLO.bin 0x08000000

# Hoặc dùng OpenOCD
openocd -f interface/stlink-v2.cfg -f target/stm32f4x.cfg \
  -c "program build/HeNhung_HELLO.elf verify reset exit"
```

### 7.4 Nạp với STM32CubeProgrammer

1. Kết nối board qua USB (ST-Link)
2. Mở STM32CubeProgrammer → `Open File` → chọn `.bin` hoặc `.hex`
3. `Start Address`: `0x08000000`
4. Click `Start Programming`

### 7.5 Memory Layout

```
FLASH: 0x08000000 — 2 MB (code + constant data)
SRAM:  0x20000000 — 192 KB (heap, stack, FreeRTOS tasks)
SDRAM: 0xD0000000 — 8 MB (LTDC framebuffer, double-buffer)
```

---

## 8. Debug và kiểm tra

### 8.1 Serial Monitor (Telemetry)

Kết nối USART1 @ **921600 baud, 8N1** với PC:

```bash
# Linux/Mac
screen /dev/ttyUSB0 921600

# Windows (PuTTY hoặc Tera Term)
# Serial: COMx, Speed: 921600, Data: 8, Stop: 1, Parity: None

# Python (pyserial)
import serial
ser = serial.Serial('COM3', 921600)
while True:
    line = ser.readline().decode('utf-8', errors='replace')
    print(line)
```

**Dòng CSV mẫu sẽ nhận được:**

```
DATA,12345,0,125000,130000,500,1000,480,950,72.5,97.1,85.0,4,1,5,1,1
VITAL,12345,72.5,71.2,97.1,96.8,85.0,4,1,1
SESSION_START,12000,1
ALERT,15000,4,85.2,92.0,1
SESSION_END,25000,1,10000,71.5,96.9,0,0
```

### 8.2 Debug LED

| LED | Ý nghĩa |
|-----|---------|
| PG13 (LD3) nháy | Có cảnh báo BPM hoặc SpO2 |
| PG14 (LD4) nháy | Cảnh báo luân phiên với PG13 |
| Cả hai tắt | Bình thường hoặc chưa đo |

### 8.3 Debug TouchGFX

Các chân PE2-PE5 được cấu hình để toggle debug (nếu cần dùng oscilloscope):

| Chân | Ý nghĩa |
|------|---------|
| PE2 | VSYNC_FREQ |
| PE3 | RENDER_TIME |
| PE4 | FRAME_RATE |
| PE5 | MCU_ACTIVE |

### 8.4 Debug I2C

Nếu cảm biến không hoạt động:

1. Kiểm tra I2C address: MAX30102 = `0xAE` (8-bit), DS1307 = `0xD0` (8-bit)
2. Kiểm tra pull-up trên PA8 (SCL) và PC9 (SDA)
3. Kiểm tra Part ID của MAX30102 (phải đọc được `0x11` hoặc `0x15`)
4. Nếu bus kẹt: firmware tự re-init sau 10 lỗi liên tiếp

### 8.5 Kiểm tra chức năng từng phần

| Chức năng | Cách kiểm tra |
|-----------|--------------|
| Cảm biến PPG | Đặt ngón tay lên MAX30102, kiểm tra serial: `state` chuyển từ `0→1→2→3` |
| BPM | Sau 5s đo, giá trị BPM xuất hiện trên Dashboard |
| SpO2 | Sau ~8s, SpO2 bắt đầu cập nhật |
| Waveform | Chuyển sang màn hình Waveform, thấy đồ thị dao động theo nhịp tim |
| Nút B1 | Nhấn B1, màn hình chuyển Dashboard ↔ Waveform |
| LED cảnh báo | Giữ ngón tay lệch vị trí, BPM/SpO2 sẽ sai → LED nháy |
| Telemetry | Mở serial monitor, thấy CSV data liên tục |
| Buzzer | Khi bắt đầu/kết thúc phiên, buzzer phát âm |

---

## Phong thư mục dự án

```
HeNhung_HELLO/
├── Application/              ← Layer ứng dụng (tasks, services, bridge)
│   ├── app_init.c/.h        ← Khởi tạo + SensorTask loop
│   ├── dsp_task.c/.h        ← DSP FreeRTOS thread
│   ├── application_gui_bridge.cpp/.hpp  ← DSP → TouchGFX
│   ├── medical_alert_service.c/.h       ← Đánh giá ngưỡng BPM/SpO2
│   ├── alert_led_pattern.c/.h           ← Nháy LED PG13/PG14
│   └── physical_input_service.c/.h      ← Nút B1 EXTI
│
├── Config/                   ← Cấu hình tập trung
│   ├── hw_config.h          ← Chân GPIO, bus, timing
│   ├── ppg_config.h         ← Ngưỡng DSP
│   ├── alert_config.h       ← Ngưỡng y tế
│   └── buzzer_melodies.h    ← Giai điệu buzzer
│
├── Core/                     ← STM32CubeMX generated
│   ├── Src/main.c           ← Init ngoại vi + tạo tasks
│   ├── Src/stm32f4xx_it.c   ← Interrupt handlers
│   └── Inc/                 ← Headers
│
├── DSP/                      ← Xử lý tín hiệu số (pure C, portable)
│   ├── ppg_measurement.c/.h ← Engine chính (826 dòng)
│   ├── spo2_estimator.c/.h  ← SpO2 ratio-of-ratios
│   ├── lowpass_filter.c/.h  ← Butterworth IIR
│   ├── median_filter.c/.h   ← Median filter
│   ├── moving_average_filter.c/.h  ← Moving average
│   ├── ppg_types.h          ← Shared data types
│   └── measurement_types.h  ← History record types
│
├── Drivers/                  ← HAL + BSP + custom drivers
│   ├── MAX30102/            ← Cảm biến PPG
│   ├── DS1307/              ← RTC
│   ├── StatusLed/           ← LED PG13/PG14
│   ├── Buzzer/              ← PWM buzzer
│   ├── BSP/Components/      ← ILI9341, STMPE811
│   ├── STM32F4xx_HAL_Driver/ ← HAL library
│   └── CMSIS/               ← ARM CMSIS headers
│
├── Services/
│   └── rtc_service.c/.h     ← DS1307 RTC wrapper
│
├── Storage/
│   ├── history_storage_interface.c/.h  ← Interface (chưa implement SD)
│   └── temporary_history_store.c/.h    ← Lưu RAM tạm
│
├── Telemetry/
│   ├── telemetry_service.c/.h    ← Queue + UART TX task
│   └── telemetry_formatter.c/.h  ← CSV format
│
├── TouchGFX/
│   ├── gui/src/             ← 8 Views + 7 Widgets (MVP)
│   ├── gui/include/gui/     ← Headers, interfaces, types
│   ├── generated/           ← TouchGFX generated code
│   ├── target/              ← HAL integration
│   └── assets/              ← Fonts, texts
│
├── Utilities/
│   └── ppg_sample_queue.c/.h ← Lock-free SPSC queue
│
├── Middlewares/              ← FreeRTOS + TouchGFX framework
├── gcc/                      ← Makefile + linker script
├── MDK-ARM/                  ← Keil project
├── EWARM/                    ← IAR project
├── STM32CubeIDE/             ← CubeIDE project
├── STM32F429I_DISCO_REV_D01.ioc  ← CubeMX config
└── docs/                     ← Tài liệu
```
