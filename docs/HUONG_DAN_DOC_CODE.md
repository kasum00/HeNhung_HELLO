# Hướng Dẫn Đọc Code Dự Án — HeNhung_HELLO

> **Đối tượng:** Người mới tham gia dự án, chưa nắm rõ codebase.
> **Mục tiêu:** Hiểu được luồng chạy chính, biết file nào làm gì, và cách đi sâu vào từng phần.

---

## Mục Lục

1. [Tổng Quan Dự Án](#1-tổng-quan-dự-án)
2. [Phần Cứng Nhanh](#2-phen-cung-nhanh)
3. [Luồng Dữ Liệu Chính](#3-luồng-du-lieu-chinh)
4. [Cấu Trúc Thư Mục — File Nào Làm Gì](#4-cau-truc-thu-muc--file-nao-lam-gi)
5. [Hướng Đọc Từng Tầng (Layer by Layer)](#5-huồng-doc-tung-tang-layer-by-layer)
6. [Các Cơ Chế Thiết Kế Đặc Biệt](#6-cac-co-che-thiet-ke-dac-biet)
7. [FAQ & Mẹo Đọc Code](#7-faq--meo-doc-code)

---

## 1. Tổng Quan Dự Án

Dự án là thiết bị **đo SpO2 (nồng độ oxy trong máu) và Nhịp tim (BPM)** sử dụng:

| Thành phần | Chi tiết |
|---|---|
| **Cảm biến** | MAX30102 (PPG quang học) — đo RED + IR |
| **MCU** | STM32F429ZIT6 (Cortex-M4, 180 MHz) |
| **Board** | STM32F429I-DISCO (REV D01) |
| **Màn hình** | ILI9341 LCD 320×240px, RGB565 |
| **RTOS** | FreeRTOS (4 tasks) |
| **GUI** | TouchGFX (MVP pattern) |

> ⚠️ **KHÔNG PHẢI thiết bị y tế.** Chỉ phục vụ học tập.

---

## 2. Phần Cứng Nhanh

```
┌─────────────────────────────────────────────────────────────┐
│                    STM32F429I-DISCO                          │
│                                                             │
│  ┌──────────┐    I2C3 (100kHz)    ┌───────────────────┐    │
│  │ MAX30102  │◄──────────────────►│  DS1307 RTC       │    │
│  │ (0x57)    │  dùng chung bus    │  (0x68)           │    │
│  └────┬─────┘    + mutex         └───────────────────┘    │
│       │ RED + IR                                            │
│       ▼                                                    │
│  ┌──────────┐    SPI5           ┌───────────────────┐     │
│  │ DSP Task │    ──────────►    │ ILI9341 LCD       │     │
│  │ (PPG)    │                   │ 320×240 RGB565    │     │
│  └──────────┘                   └───────────────────┘     │
│                                                             │
│  B1 (PA0) ── EXTI0 ──► NVIC     USART1 (921600) ──► PC   │
│  LD3 (PG13) ◄── PWM            LD4 (PG14) ◄── PWM        │
│  Buzzer (PF6) ◄── TIM10_CH1                                   │
└─────────────────────────────────────────────────────────────┘
```

**Lưu ý quan trọng:** I2C3 bị chiếm hết (LTDC/FMC dùng chân I2C1/I2C2), nên MAX30102 và DS1307 **bắt buộc** dùng chung I2C3. Mutex FreeRTOS tuần tự hóa truy cập.

---

## 3. Luồng Dữ Liệu Chính

Đây là phần quan trọng nhất để hiểu dự án. Dữ liệu đi theo pipeline:

```
    ┌─────────────┐
    │  MAX30102    │  Cảm biến quang học: RED 18-bit + IR 18-bit
    │  (I2C3)     │
    └──────┬──────┘
           │ FIFO (32 samples)
           ▼
    ┌──────────────────────────────────────────────────┐
    │  DefaultTask (Sensor Task)                       │
    │  app_init.c → App_DefaultTask()                 │
    │  ┌──────────────────────────────────────────┐    │
    │  │ 1. Poll FIFO mỗi 20ms (MAX30102_ReadFifo)│   │
    │  │ 2. Đọc overflow counter                   │   │
    │  │ 3. Pack sample → PpgQueue_Push()          │   │
    │  │ 4. Chạy Buzzer_Process()                  │   │
    │  │ 5. Chạy AlertLed_Process()                │   │
    │  │ 6. Poll RTC ~1Hz                          │   │
    │  └──────────────────────────────────────────┘    │
    └──────────────────┬───────────────────────────────┘
                       │
                       ▼
              ┌─────────────────┐
              │   PpgQueue      │  SPSC Queue (lock-free)
              │ (SPSC queue)    │  ~100 samples缓冲
              └────────┬────────┘
                       │
                       ▼
    ┌──────────────────────────────────────────────────┐
    │  DspTask (DSP Task)                              │
    │  dsp_task.c → dspLoop()                         │
    │  ┌──────────────────────────────────────────┐    │
    │  │ 1. PpgQueue_Pop() → lấy raw sample       │   │
    │  │ 2. Ppg_PushSample() → PPG Engine:        │   │
    │  │    • Phát hiện ngón tay (DC threshold)    │   │
    │  │    • Căn giữa DC (baseline tracking)      │   │
    │  │    • Moving average filter (nếu bật)       │   │
    │  │    • Peak detection → RR interval          │   │
    │  │    • BPM = median(5 RR intervals)         │   │
    │  │    • SpO2 = ratio-of-ratios (4s window)   │   │
    │  │ 3. MedicalAlert_Update() → kiểm tra ngưỡng│  │
    │  │ 4. publishResult() → seqlock → GUI        │   │
    │  │ 5. Telemetry → hàng đợi UART             │   │
    │  └──────────────────────────────────────────┘    │
    └──────────────────┬───────────────────────────────┘
                       │ Seqlock (lock-free)
                       ▼
    ┌──────────────────────────────────────────────────┐
    │  GUI_Task (TouchGFX)                             │
    │  FrontendApplication → Model → View              │
    │  ┌──────────────────────────────────────────┐    │
    │  │ • Đọc PpgResult qua DspTask_GetResult()  │   │
    │  │ • Hiển thị BPM, SpO2, waveform, SQI      │   │
    │  │ • Dashboard, Waveform, History screens     │   │
    │  └──────────────────────────────────────────┘    │
    └──────────────────────────────────────────────────┘
```

### Trạng Thái Đo (State Machine)

```
IDLE ──► WAIT_FINGER ──► STABILIZING ──► MEASURING ──► RESULT_READY
  ▲           │                │               │              │
  │           │ (timeout)      │ (signal ok)   │ (finger off) │
  │           ▼                ▼               ▼              │
  │     WAIT_FINGER    INVALID_SIGNAL    STABILIZING         │
  │           │                                                     │
  │           ◄─────────────────────────────────────────────────────┘
  │
  └── (nếu nhấc tay quá nhanh, chưa đo được gì → về WAIT_FINGER)
```

---

## 4. Cấu Trúc Thư Mục — File Nào Làm Gì

### Tầng 1: Cấu Hình Tập Trung (`Config/`)

> **Nguyên tắc:** Mọi ngưỡng, chân GPIO, timing đều ở đây, **KHÔNG hard-code** trong driver.

| File | Chứa gì | Ai dùng |
|---|---|---|
| [hw_config.h](Config/hw_config.h) | Chân GPIO, địa chỉ I2C, timer, UART, timing | Tất cả driver |
| [ppg_config.h](Config/ppg_config.h) | Ngưỡng PPG: ngón tay, peak, BPM, SpO2, waveform | Engine PPG, SpO2 |
| [alert_config.h](Config/alert_config.h) | Ngưỡng BPM/SpO2, thời gian hysteresis, LED timing | MedicalAlert |
| [buzzer_melodies.h](Config/buzzer_melodies.h) | Nhạc buzzer startup/done/invalid | Buzzer driver |
| [datetime.h](Config/datetime.h) | Kiểu DateTime, validate ngày, leap year | RTC, History |

### Tầng 2: Driver Phần Cứng (`Drivers/`)

> **Nguyên tắc:** Driver chỉ biết HAL và hw_config.h, không biết logic ứng dụng.

| File | Chức năng |
|---|---|
| [max30102_driver.c](Drivers/MAX30102/max30102_driver.c) | Đọc/ghi thanh ghi MAX30102 qua I2C. Init, reset, đọc FIFO (18-bit sample). |
| [ds1307_driver.c](Drivers/DS1307/ds1307_driver.c) | Đọc/ghi DS1307 RTC qua I2C. BCD → DateTime, 24h. |
| [buzzer_driver.c](Drivers/Buzzer/buzzer_driver.c) | PWM TIM10_CH1. Phát giai điệu non-blocking (note-by-note). |
| [status_led_driver.c](Drivers/StatusLed/status_led_driver.c) | GPIO PG13/PG14. Set ON/OFF/AllOff. |

### Tầng 3: DSP / Thuật Toán (`DSP/`)

> **Nguyên tắc:** Không HAL, không cấp phát bộ nhớ động, O(1) mỗi sample.

| File | Chức năng | Thuật toán chính |
|---|---|---|
| [ppg_measurement.c](DSP/ppg_measurement.c) | **Trái tim dự án.** Engine đo PPG: state machine, peak detect, BPM, waveform, SpO2. | EMA baseline, peak threshold, median RR |
| [moving_average_filter.c](DSP/moving_average_filter.c) | Bộ lọc trung bình trượt. Running-sum O(1). | Circular buffer + sum |
| [spo2_estimator.c](DSP/spo2_estimator.c) | Ước lượng SpO2 ratio-of-ratios. | RMS RED/IR → R = (ACred/DCred)/(ACir/DCir) → calibration curve |
| [ppg_types.h](DSP/ppg_types.h) | Kiểu dữ liệu: `PpgState`, `PpgResult`, `PpgRawSample`. | — |
| [measurement_types.h](DSP/measurement_types.h) | Kiểu kết quả chốt: `MeasurementHistoryRecord`. | — |

### Tầng 4: Dịch Vụ Ứng Dụng (`Application/`)

| File | Chức năng | Chạy ở task nào |
|---|---|---|
| [app_init.c](Application/app_init.c) | **Khởi tạo** toàn bộ + **Sensor Task** (DefaultTask). Poll FIFO, đưa queue, buzzer, LED, RTC. | DefaultTask (FreeRTOS) |
| [dsp_task.c](Application/dsp_task.c) | **DSP Task**: nhận queue → PPG engine → alerts → publish GUI. | dspTask (FreeRTOS) |
| [medical_alert_service.c](Application/medical_alert_service.c) | Đánh giá ngưỡng BPM/SpO2 với **hysteresis thời gian** (2s bật, 3s tắt). | Được gọi từ DSP Task |
| [physical_input_service.c](Application/physical_input_service.c) | Xử lý nút B1 (EXTI0 ISR + debounce 200ms). ISR→flag pattern. | ISR + GUI_Task đọc |
| [alert_led_pattern.c](Application/alert_led_pattern.c) | Mẫu nháy LED luân phiên PG13↔PG14. | DefaultTask (Sensor) |
| [application_gui_bridge.cpp](Application/application_gui_bridge.cpp) | **Cầu nối** C↔C++ giữa DSP task và TouchGFX GUI. Đọc PpgResult → GuiSnapshot. | GUI_Task |

### Tầng 5: Telemetry (`Telemetry/`)

| File | Chức năng |
|---|---|
| [telemetry_service.c](Telemetry/telemetry_service.c) | Hàng đợi 2 ưu tiên (event + waveform), UART TX ngắt non-blocking. TelemetryTask format + gửi CSV. |
| [telemetry_formatter.c](Telemetry/telemetry_formatter.c) | Format message → dòng CSV text (256 bytes max). |

### Tầng 6: Lưu Trữ (`Storage/`)

| File | Chức năng |
|---|---|
| [temporary_history_store.c](Storage/temporary_history_store.c) | Buffer tròn RAM 20 bản ghi. Khi nhấc ngón tay → lưu kết quả phiên. |
| [history_storage_interface.c](Storage/history_storage_interface.c) | Interface trừu tượng (vtable) — mở rộng cho SD card sau này. |

### Tầng 7: RTC Service (`Services/`)

| File | Chức năng |
|---|---|
| [rtc_service.c](Services/rtc_service.c) | Đọc DS1307 mỗi giây, publish snapshot cho GUI, xử lý yêu cầu cài giờ. |

### Tầng 8: Giao Diện (`TouchGFX/`)

> **Mô hình MVP:** Model → Presenter → View. Mỗi screen có 3 file.

| Màn hình | File chính | Chức năng |
|---|---|---|
| **Boot** | `boot_screen/BootView.cpp` | Splash + progress bar khi khởi động |
| **Dashboard** | `dashboard_screen/DashboardView.cpp` | BPM, SpO2, SQI live + nút Start/Waveform |
| **Waveform** | `waveform_screen/WaveformView.cpp` | Vẽ sóng PPG real-time + peak markers + filter toggle |
| **History** | `history_screen/HistoryView.cpp` | Xem lại các phiên đo (5 bản ghi/trang, max 20) |
| **Settings** | `settings_screen/SettingsView.cpp` | Cài đặt filter mode, SQI, buzzer, LED |
| **DateTime** | `datetimesettings_screen/DateTimeSettingsView.cpp` | Đặt giờ DS1307 |
| **About** | `about_screen/AboutView.cpp` | Thông tin dự án |

**Files quan trọng nhất trong GUI:**

| File | Chức năng |
|---|---|
| [FrontendApplication.cpp](TouchGFX/gui/src/common/FrontendApplication.cpp) | **Điều khiển navigation** màn hình. Xử lý nút B1 (Dashboard↔Waveform). Deferred transition. |
| [Model.cpp](TouchGFX/gui/src/model/Model.cpp) | Tick model mỗi frame → provider.tick(). |
| [ApplicationGuiBridge.cpp](Application/application_gui_bridge.cpp) | Đọc PpgResult từ DSP → tạo GuiSnapshot cho GUI. Chuyển PpgState → MeasurementState. |

---

## 5. Hướng Đọc Từng Tầng (Layer by Layer)

### Đọc từ dưới lên (Bottom-Up)

Nếu bạn muốn hiểu từ phần cứng lên:

```
Bước 1: hw_config.h           → Biết chân nào, bus nào, timing nào
Bước 2: max30102_driver.c      → Hiểu cách đọc FIFO từ cảm biến
Bước 3: moving_average_filter.c → Hiểu bộ lọc đơn giản nhất (O(1) running sum)
Bước 4: spo2_estimator.c       → Hiểu thuật toán SpO2 ratio-of-ratios
Bước 5: ppg_measurement.c      → Hiểu TOÀN BỘ engine PPG (đây là file lớn nhất, khó nhất)
Bước 6: app_init.c             → Hiểu Sensor Task (cách đưa data vào pipeline)
Bước 7: dsp_task.c             → Hiểu DSP Task (cách đọc queue → xử lý → publish)
Bước 8: medical_alert_service.c → Hiểu hệ thống cảnh báo y tế
B_steps 9: application_gui_bridge.cpp → Hiểu cách data từ C sang C++/GUI
Bước 10: FrontendApplication.cpp     → Hiểu navigation + cách GUI nhận data
```

### Đọc theo Dữ Liệu (Data-Driven)

Nếu bạn muốn hiểu data đi từ đâu đến đâu:

```
Bước 1: PpgRawSample (ppg_types.h)  → Kiểu data thô từ cảm biến
Bước 2: PpgQueue_Push/Pop           → Hàng đợi SPSC
Bước 3: Ppg_PushSample()            → Input vào PPG engine
Bước 4: PpgResult (ppg_types.h)     → Output từ PPG engine (đủ thứ: BPM, SpO2, waveform...)
Bước 5: publishResult() (seqlock)   → Chia sẻ lock-free với GUI
Bước 6: DspTask_GetResult()         → GUI đọc kết quả
Bước 7: GuiSnapshot (GuiSnapshots.hpp) → Data cho TouchGFX render
```

### Đọc theo Tính Năng (Feature-Driven)

Nếu bạn muốn hiểu một tính năng cụ thể:

**Tính năng "Đo BPM":**
```
ppg_measurement.c → runPeakDetector()
    → addInterval() → medianInterval() → s_bpm
    → config: PPG_BPM_INTERVAL_COUNT=5, PPG_BPM_MIN/MAX
```

**Tính năng "Cảnh báo nhịp thấp":**
```
alert_config.h              → ALERT_BPM_LOW_THRESHOLD = 45.0
medical_alert_service.c     → conditionUpdate() với hysteresis 2s/3s
alert_led_pattern.c         → Nháy LED PG13↔PG14 mỗi 300ms
buzzer_driver.c             → Melody BUZZER_MELODY_ALERT
```

**Tính năng "Nút B1":**
```
physical_input_service.c    → EXTI0_IRQHandler → debounce 200ms → s_pressCount++
FrontendApplication.cpp     → handlePhysicalButton() → chuyển Dashboard↔Waveform
```

---

## 6. Các Cơ Chế Thiết Kế Đặc Biệt

### 6.1 Seqlock (Lock-Free Reader/Writer)

**Nơi:** `dsp_task.c` → `publishResult()` / `DspTask_GetResult()`

**Vấn đề:** DSP task ghi kết quả, GUI task đọc. Nếu đọc giữa chừng ghi → data bị xé.

**Giải pháp:** Seqlock — biến `s_pubGen` chẵn = ổn định, lẻ = đang ghi.

```c
// DSP thread (writer):
s_pubGen++;          // → lẻ: đang ghi
__DMB();             // Memory barrier
s_pub = *r;          // Copy cả struct
__DMB();
s_pubGen++;          // → chẵn: ổn định

// GUI thread (reader):
do {
    g0 = s_pubGen;
    __DMB();
    *out = s_pub;    // Copy cả struct
    __DMB();
} while (g0 != s_pubGen);  // Thử lại nếu thay đổi giữa chừng
```

**Tại sao không mutex?** Mutex có thể priority inversion. Seqlock nhẹ hơn, reader không bao giờ bị block.

### 6.2 Bus I2C Dùng Chung + Mutex

**Nơi:** `app_init.c` → `i2cLock()` / `i2cUnlock()`

**Vấn đề:** MAX30102 và DS1307 cùng I2C3. Nếu cùng lúc đọc → đụng bus.

**Giải pháp:** FreeRTOS mutex + cơ chế phục hồi bus khi lỗi kéo dài.

```
Sensor Task giữ mutex trong suốt vòng lặp:
  i2cLock();
  MAX30102_ReadFifo(...)    // Đọc cảm biến
  i2cUnlock();
  ...
  i2cLock();
  RtcService_Poll()          // Đọc RTC
  i2cUnlock();

Nếu lỗi liên tiếp ≥ 10 lần → i2cRecoverBus() (DeInit + Init lại I2C)
```

### 6.3 Hysteresis Thời Gian (Chống Nhấp Nháy)

**Nơi:** `medical_alert_service.c` → `conditionUpdate()`

**Vấn đề:** BPM dao động quanh ngưỡng → cảnh báo bật/tắt liên tục.

**Giải pháp:** Phải **vượt ngưỡng liên tục 2 giây** mới bật, và **bình thường liên tục 3 giây** mới tắt.

```c
// Mỗi condition có: rawExceeded + active + sinceMs
if (rawNow != c->rawExceeded) {
    c->rawExceeded = rawNow;
    c->sinceMs = nowMs;    // Reset timer
}
if (c->active && !rawExceeded && (held >= clearTime)) {
    c->active = false;     // Đã bình thường đủ lâu → tắt
}
```

### 6.4 SPSC Queue (Single Producer, Single Consumer)

**Nơi:** Sensor Task → PpgQueue → DSP Task

**Vấn đề:** Sensor task tạo sample, DSP task tiêu thụ. Đừng dùng mutex cho cái này.

**Giải pháp:** Hàng đợi SPSC lock-free. Producer chỉ push, consumer chỉ pop. Biến `head`/`tail` riêng biệt → không cần lock.

### 6.5 Không Cấp Phát Bộ Nhớ Động

**Toàn bộ dự án** dùng buffer tĩnh (`static`). Không có `malloc()`/`new`.

```c
// Ví dụ trong dsp_task.c:
static StaticTask_t s_dspCb;
static uint32_t s_dspStack[1024];    // 4 KB stack tĩnh
```

**Tại sao?** Hệ nhúng không nên cấp phát động → tránh fragmentation, deterministic.

### 6.6 Strong-Symbol Override (ISR)

**Nơi:** `physical_input_service.c` → `EXTI0_IRQHandler()`

CubeMX sinh `EXTI0_IRQHandler` với `__weak`. File user-defined override bằng symbol mạnh → CubeMX regenerate không ghi đè.

```c
// startup_stm32f429zitx.s định nghĩa weak:
void EXTI0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));

// physical_input_service.c override:
void EXTI0_IRQHandler(void) {
    HAL_GPIO_EXTI_IRQHandler(HW_B1_PIN);
}
```

---

## 7. FAQ & Mẹo Đọc Code

### Q: Nên đọc file nào đầu tiên?

**A:** Đọc theo thứ tự này:
1. `Config/hw_config.h` — 5 phút, nắm toàn bộ chân bus
2. `DSP/ppg_types.h` — 10 phút, hiểu kiểu data `PpgState`, `PpgResult`
3. `Application/app_init.c` — 15 phút, hiểu Sensor Task chạy gì
4. `Application/dsp_task.c` — 20 phút, hiểu DSP Task
5. `DSP/ppg_measurement.c` — 30 phút+, đây là file lớn nhất và quan trọng nhất

### Q: File nào lớn nhất / phức tạp nhất?

**A:** [ppg_measurement.c](DSP/ppg_measurement.c) (~750 dòng) — Toàn bộ engine PPG: state machine, peak detection, BPM, waveform, SpO2. Đọc từ `Ppg_Init()` xuống, tập trung vào `Ppg_PushSample()`.

### Q: Cómo C và C++ kết nối?

**A:** Dùng `extern "C"` trong [application_gui_bridge.cpp](Application/application_gui_bridge.cpp):
```cpp
extern "C" {
#include "dsp_task.h"     // Hàm C
#include "rtc_service.h"
}
// → Gọi thẳng hàm C từ C++ code
```

### Q: TouchGFX chạy thế nào?

**A:** TouchGFX dùng mô hình MVP:
- **Model** (`Model.cpp`): Tick mỗi frame
- **Presenter**: Scaffold, kết nối View ↔ Model
- **View** (`*View.cpp`): Code render UI thật sự

FrontendApplication.cpp quản lý navigation giữa các screen.

### Q: Khi nào dùng mutex vs seqlock?

| Cơ chế | Dùng khi | Ví dụ |
|---|---|---|
| **Mutex** | Cần đồng bộ hóa truy cập shared resource, chấp nhận blocking | I2C bus (MAX30102 + DS1307) |
| **Seqlock** | Writer ít, reader nhiều, không muốn reader bị block | DSP result → GUI |
| **SPSC Queue** | Một người tạo, một người dùng, không đụng chạm | Sensor → DSP samples |

### Q: Cấu trúc thư mục nào nên sửa khi nào?

| Muốn sửa gì | Sửa file nào |
|---|---|
| Đổi chân GPIO | `Config/hw_config.h` + `.ioc` |
| Đổi ngưỡng BPM/SpO2 | `Config/ppg_config.h` |
| Đổi ngưỡng cảnh báo | `Config/alert_config.h` |
| Thêm algorithm DSP mới | `DSP/` (thêm file mới + sửa `ppg_measurement.c`) |
| Thêm màn hình mới | `TouchGFX/gui/` + `FrontendApplication.cpp` |
| Thay cảm biến khác | `Drivers/` (thay driver) + `Config/hw_config.h` |

### Q: Làm sao debug khi chạy trên board?

**A:**
- **UART telemetry** (921600 baud): Dữ liệu CSV realtime trên USART1. Dùng Terra Term / PuTTY / CoolTerm.
- **LED debug**: PG13/PG14 nháy khi có cảnh báo
- **Breakpoint**: Dùng ST-Link qua STM32CubeIDE
- **TouchGFX Simulator**: Chạy trên PC (file `TouchGFX/simulator/main.cpp`)

---

## Phụ Lục: Bảng Map File Nhanh

```
┌─────────────────────────────────────────────────────────────────────┐
│                     BẢN ĐỒ FILE DỰ ÁN                              │
├──────────────────┬──────────────────────────────────────────────────┤
│ Config/          │ hw_config.h      → chân, bus, timing            │
│                  │ ppg_config.h     → ngưỡng PPG/BPM/SpO2         │
│                  │ alert_config.h   → ngưỡng cảnh báo y tế         │
├──────────────────┼──────────────────────────────────────────────────┤
│ Drivers/         │ max30102_driver  → cảm biến MAX30102 (I2C)      │
│                  │ ds1307_driver    → RTC DS1307 (I2C)             │
│                  │ buzzer_driver    → buzzer PWM (TIM10)           │
│                  │ status_led_driver→ LED GPIO (PG13/PG14)         │
├──────────────────┼──────────────────────────────────────────────────┤
│ DSP/             │ ppg_measurement  → ENGINE PPG (đọc kỹ nhất!)    │
│                  │ moving_average   → bộ lọc MA O(1)               │
│                  │ spo2_estimator   → SpO2 ratio-of-ratios          │
├──────────────────┼──────────────────────────────────────────────────┤
│ Application/     │ app_init         → Khởi tạo + Sensor Task       │
│                  │ dsp_task         → DSP Task (queue→engine→GUI)   │
│                  │ medical_alert    → Cảnh báo BPM/SpO2            │
│                  │ physical_input   → Nút B1 (ISR + debounce)       │
│                  │ alert_led_pattern→ Nháy LED cảnh báo             │
│                  │ gui_bridge       → Cầu nối C↔C++ (DSP→GUI)      │
├──────────────────┼──────────────────────────────────────────────────┤
│ Telemetry/       │ telemetry_service→ UART non-blocking + queue    │
│                  │ telemetry_formatter→ Format CSV                   │
├──────────────────┼──────────────────────────────────────────────────┤
│ Storage/         │ temporary_history→ Buffer RAM 20 bản ghi        │
│                  │ history_interface→ Vtable mở rộng cho SD         │
├──────────────────┼──────────────────────────────────────────────────┤
│ Services/        │ rtc_service      → Đọc DS1307, publish time     │
├──────────────────┼──────────────────────────────────────────────────┤
│ Core/Src/        │ main.c           → Entry point, init HAL, tạo task│
├──────────────────┼──────────────────────────────────────────────────┤
│ TouchGFX/gui/    │ FrontendApp      → Navigation + B1 handler      │
│                  │ DashboardView    → Màn hình BPM/SpO2 live       │
│                  │ WaveformView     → Màn hình vẽ sóng PPG         │
│                  │ HistoryView      → Màn hình lịch sử             │
│                  │ Model            → Tick model                    │
└──────────────────┴──────────────────────────────────────────────────┘
```

---

*Bản cập nhật: 2026-07-22*
*Dự án: HeNhung_HELLO — Đồ Lớn Môn Hệ Nhúng*
