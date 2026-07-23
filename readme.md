# HeNhung_HELLO

## 📋 Tổng quan

Dự án **đồ án lớn môn Hệ nhúng** — thiết bị **đo SpO2 & nhịp tim** sử dụng cảm biến quang học **MAX30102** trên nền tảng **STM32F429I-DISCO**, với giao diện đồ họa **TouchGFX** chạy trên màn hình LCD 320×240px.

> ⚠️ **Đây KHÔNG PHẢI thiết bị y tế** — chỉ phục vụ học tập và thử nghiệm.

---

## 🔧 Phần cứng

| Thành phần       | Chi tiết                                     |
| ---------------- | -------------------------------------------- |
| **MCU**          | STM32F429ZIT6 (Cortex-M4, 180 MHz, LQFP144)  |
| **Board**        | STM32F429I-DISCO REV D01                     |
| **Màn hình**     | ILI9341 LCD 320×240px RGB565, SPI5           |
| **RAM ngoài**    | SDRAM qua FMC (frame buffer TouchGFX)        |
| **Cảm biến**     | MAX30102 (PPG: SpO2 + BPM) — I2C3, addr 0x57 |
| **RTC**          | DS1307 — I2C3 dùng chung, addr 0x68          |
| **Buzzer**       | PWM trên TIM10_CH1 (PF6)                     |
| **Nút nhấn**     | B1 (PA0, EXTI0, ngắt sườn lên)               |
| **LED cảnh báo** | PG13 (LD3), PG14 (LD4)                       |
| **UART**         | USART1 (PA9/PA10, 921600 baud)               |

---

## 🏗 Kiến trúc phần mềm

```
┌─────────────────────────────────────────────────────────────────┐
│                     FreeRTOS Scheduler                          │
├────────────────┬────────────────┬──────────────┬────────────────┤
│  DefaultTask   │   GUI_Task     │   dspTask    │  TelemetryTask │
│  (Sensor)      │   (TouchGFX)   │   (PPG DSP)  │  (UART)        │
│  128 words     │   8192 words   │   1024 words │                │
│  Normal pri    │   Normal pri   │   Normal pri │                │
└────────────────┴────────────────┴──────────────┴────────────────┘
```

**1. DefaultTask (Sensor)** — Poll FIFO MAX30102 mỗi 20ms, đọc raw RED/IR, đưa vào queue cho DSP. Điều khiển buzzer, LED alert, RTC poll ~1Hz. Sở hữu duy nhất I2C3.

**2. GUI_Task (TouchGFX)** — Render giao diện LCD, đọc kết quả DSP qua seqlock (lock-free).

**3. DSP Task** — Nhận raw samples từ queue, chạy thuật toán PPG (peak detection, BPM, SpO2), cập nhật cảnh báo y tế, publish telemetry.

**4. Telemetry Task** — Gửi dữ liệu UART (non-blocking).

---

## 📁 Cấu trúc thư mục

```
HeNhung_HELLO/
├── Application/                 → Logic ứng dụng
│   ├── app_init.c/h                 → Khởi tạo + sensor task
│   ├── dsp_task.c/h                 → DSP task (peak/BPM engine)
│   ├── medical_alert_service.c/h    → Ngưỡng BPM/SpO2 + hysteresis
│   ├── physical_input_service.c/h   → Xử lý nút B1 (debounce)
│   ├── alert_led_pattern.c/h        → Mẫu nháy LED cảnh báo
│   └── application_gui_bridge.cpp/h → Cầu nối DSP ↔ GUI (seqlock)
│
├── Config/                     → Cấu hình tập trung
│   ├── hw_config.h                  → Ánh xạ chân, bus, peripheral
│   ├── ppg_config.h                 → Ngưỡng PPG
│   ├── alert_config.h               → Ngưỡng cảnh báo y tế
│   ├── buzzer_melodies.h            → Nhạc buzzer
│   └── datetime.h                   → Kiểu thời gian
│
├── DSP/                        → Thuật toán xử lý tín hiệu số
│   ├── ppg_measurement.c/h          → Engine đo PPG (peak, BPM, waveform)
│   ├── ppg_types.h                  → Kiểu dữ liệu PPG
│   ├── moving_average_filter.c/h    → Bộ lọc trung bình trượt
│   ├── lowpass_filter.c/h           → Bộ lọc thông thấp (Butterworth)
│   ├── median_filter.c/h            → Bộ lọc trung vị (median filter)
│   ├── spo2_estimator.c/h           → Ước lượng SpO2 (ratio-of-ratios)
│   └── measurement_types.h          → Kiểu kết quả đo
│
├── Drivers/                    → Driver phần cứng
│   ├── BSP/                         → LCD ILI9341, STMPE811
│   ├── Buzzer/                      → Driver buzzer PWM
│   ├── MAX30102/                    → Driver cảm biến MAX30102
│   ├── DS1307/                      → Driver RTC
│   ├── StatusLed/                   → Driver LED trạng thái
│   ├── CMSIS/                       → ARM CMSIS
│   └── STM32F4xx_HAL_Driver/       → STM32 HAL
│
├── TouchGFX/                   → Giao diện TouchGFX
│   └── gui/src/
│       ├── boot_screen/             → Màn hình khởi động
│       ├── home_screen/             → Màn hình chính
│       ├── dashboard_screen/        → Dashboard BPM/SpO2
│       ├── waveform_screen/         → Biểu đồ PPG
│       ├── history_screen/          → Lịch sử đo
│       ├── settings_screen/         → Cài đặt
│       ├── datetimesettings_screen/ → Cài đặt ngày giờ
│       └── about_screen/            → Thông tin hệ thống
│
├── Core/                       → HAL init, main.c, FreeRTOS config
├── Middlewares/                → TouchGFX framework, FreeRTOS
└── STM32F429I_DISCO_REV_D01.ioc → Cấu hình CubeMX
```

---

## 🧠 Luồng xử lý tín hiệu

```
MAX30102 (I2C3, 100kHz)
    │
    ▼ [FIFO Poll 20ms]
DefaultTask ──► PpgQueue (SPSC)
                    │
                    ▼ [100Hz]
              DSP Task
              ├── Median Filter → Loại bỏ spike/nhiễu
              ├── Lowpass Filter → Loại thành phần cao tần
              ├── Moving Average → Làm mượt tín hiệu
              ├── PPG Engine → Peak detect → BPM (median RR)
              ├── SpO2 Estimator → Ratio-of-ratios → SpO2%
              ├── Medical Alert → BPM/SpO2 vs ngưỡng + hysteresis
              └── Publish → seqlock → GUI, Telemetry → UART
                    │
                    ▼
              GUI_Task (TouchGFX)
              ├── Dashboard: BPM/SpO2 live
              ├── Waveform: đồ thị PPG real-time
              ├── History: lịch sử đo
              └── Settings: cài đặt ngưỡng
```

---

## 🔑 Điểm đặc biệt

| #   | Đặc điểm                       | Mô tả                                                                    |
| --- | ------------------------------ | ------------------------------------------------------------------------ |
| 1   | **Bus I2C dùng chung**         | MAX30102 + DS1307 trên I2C3, tuần tự hóa bằng mutex FreeRTOS             |
| 2   | **Seqlock**                    | Lock-free reader/writer giữa DSP và GUI, không bao giờ đọc dữ liệu bị xé |
| 3   | **Hysteresis thời gian**       | Vượt ngưỡng 2s mới bật cảnh báo, bình thường 3s mới tắt                  |
| 4   | **Multi-stage filtering**      | Median → Lowpass → Moving Average — loại bỏ nhiễu hiệu quả               |
| 5   | **FIFO overflow tracking**     | Đếm sample bị mất, hiển thị trên GUI                                     |
| 6   | **Moving average adjustable**  | Người dùng chỉnh cỡ cửa sổ lọc qua UI                                    |
| 7   | **Không cấp phát bộ nhớ động** | Toàn bộ buffer tĩnh, phù hợp hệ nhúng                                    |
| 8   | **Config tập trung**           | Mọi ngưỡng, timing đều nằm trong `Config/`                               |

---

## 📊 Thông số kỹ thuật

| Thông số                    | Giá trị                 |
| --------------------------- | ----------------------- |
| Tần số lấy mẫu PPG          | 100 Hz                  |
| Chu kỳ poll sensor          | 20 ms                   |
| DSP tick                    | 10 ms (~100 Hz)         |
| Ngưỡng BPM thấp             | < 45 BPM (bradycardia)  |
| Ngưỡng BPM cao              | > 120 BPM (tachycardia) |
| Ngưỡng SpO2 thấp            | < 92% (hypoxia)         |
| Thời gian xác nhận cảnh báo | 2 giây                  |
| Thời gian gỡ cảnh báo       | 3 giây                  |
| Số điểm waveform            | 240 điểm                |
| Số peak giữ lại             | 12 peak/cửa sổ          |
| Số interval BPM cho median  | 5                       |
| Tần số UART telemetry       | 921600 baud             |

---

## 🛠 Build & Flash

- **IDE:** STM32CubeIDE (mặc định)
- **Hỗ trợ:** EWARM v8.50.9+, MDK-ARM, STM32CubeIDE
- **Flash:** từ TouchGFX Designer qua GCC + STM32CubeProgrammer
- **Màn hình:** 320 × 240 pixels, 16bpp (RGB565)

### Cài đặt

```bash
git clone https://github.com/kasum00/HeNhung_HELLO.git
cd HeNhung_HELLO
```

Mở `STM32F429I_DISCO_REV_D01.ioc` bằng STM32CubeMX để cấu hình lại (nếu cần), hoặc mở trực tiếp project trong STM32CubeIDE.

---

## 📄 License

Dự án học tập — không có license chính thức.
