# HeNhung_HELLO - Hệ thống đo SpO2 & Nhịp tim

## Tổng quan

Đây là dự án **đồ án lớn môn Hệ nhúng** — một thiết bị **đo SpO2 (nồng độ oxy trong máu) và nhịp tim (BPM)** sử dụng cảm biến quang học **MAX30102** trên nền tảng **STM32F429I-DISCO** (STM32F429ZIT6), với giao diện đồ họa **TouchGFX** chạy trên màn hình LCD 320×240px.

> ⚠️ **Lưu ý:** Dự án được ghi rõ là **KHÔNG PHẢI thiết bị y tế**, chỉ phục vụ học tập/thử nghiệm.

---

## Kiến trúc phần mềm

### 🖥️ Phần cứng

| Thành phần       | Chi tiết                                                 |
| ---------------- | -------------------------------------------------------- |
| **MCU**          | STM32F429ZIT6 (Cortex-M4, 180 MHz, LQFP144)              |
| **Board**        | STM32F429I-DISCO REV D01                                 |
| **Màn hình**     | ILI9341 LCD 320×240px RGB565, giao tiếp SPI5             |
| **RAM ngoài**    | SDRAM qua FMC (dùng cho frame buffer TouchGFX)           |
| **Cảm biến**     | MAX30102 (PPG: đo SpO2 + BPM) — I2C3, địa chỉ 0x57       |
| **RTC**          | DS1307 (clock thực) — I2C3 dùng chung, địa chỉ 0x68      |
| **Buzzer**       | PWM trên TIM10_CH1 (PF6)                                 |
| **Nút nhấn**     | B1 (PA0, EXTI0, ngắt sườn lên)                           |
| **LED cảnh báo** | PG13 (LD3), PG14 (LD4) — nháy luân phiên khi có cảnh báo |
| **UART**         | USART1 (PA9/PA10, 921600 baud) — telemetry               |
| **Chân debug**   | PE2–PE5 (VSYNC, RENDER_TIME, FRAME_RATE, MCU_ACTIVE)     |

### 🔄 Kiến trúc RTOS (FreeRTOS)

Dự án chạy trên **FreeRTOS** với các task chính:

```
┌─────────────────────────────────────────────────────┐
│                    FreeRTOS Scheduler                 │
├─────────────┬──────────────┬─────────────┬──────────┤
│ DefaultTask │   GUI_Task   │   dspTask   │ Telemetry│
│ (sensor)    │ (TouchGFX)   │ (PPG DSP)   │  Task    │
│ 128 words  │  8192 words  │ 1024 words  │ (UART)   │
│ Normal pri │  Normal pri  │ Normal pri  │          │
└─────────────┴──────────────┴─────────────┴──────────┘
```

1. **DefaultTask (Sensor Task)** — Poll FIFO MAX30102 mỗi 20ms, đọc raw sample RED/IR, đưa vào queue cho DSP. Cũng chạy buzzer, LED alert pattern, RTC poll ~1Hz. Sở hữu duy nhất bus I2C3.

2. **GUI_Task (TouchGFX)** — Render giao diện trên LCD, đọc kết quả từ DSP qua seqlock (lock-free).

3. **DSP Task** — Nhận raw samples từ queue, chạy thuật toán PPG (peak detection, BPM, SpO2), cập nhật cảnh báo y tế, publish telemetry.

4. **Telemetry Task** — Gửi dữ liệu qua UART (non-blocking).

---

## 📁 Cấu trúc thư mục

```
HeNhung_HELLO/
├── Core/              → HAL init, main.c, FreeRTOS config, startup
├── Application/       → Logic ứng dụng
│   ├── app_init.c/h          → Khởi tạo + sensor task
│   ├── dsp_task.c/h          → DSP task (peak/BPM engine)
│   ├── medical_alert_service.c/h → Đánh giá ngưỡng BPM/SpO2 + hysteresis
│   ├── physical_input_service.c/h → Xử lý nút B1 (EXTI ISR + debounce)
│   ├── alert_led_pattern.c/h → Mẫu nháy LED cảnh báo
│   ├── application_gui_bridge.cpp/hpp → Cầu nối DSP ↔ GUI (TouchGFX)
│   └── telemetry_service.c/h → Gửi dữ liệu UART
├── Config/            → Cấu hình tập trung
│   ├── hw_config.h           → Ánh xạ phần cứng (chân, bus, peripheral)
│   ├── ppg_config.h          → Ngưỡng PPG (finger detection, peak, BPM, SpO2)
│   ├── alert_config.h        → Ngưỡng cảnh báo y tế + timing
│   ├── buzzer_melodies.h     → Nhạc buzzer
│   └── datetime.h            → Kiểu thời gian
├── DSP/               → Thuật toán xử lý tín hiệu số
│   ├── ppg_measurement.c/h   → Engine đo PPG (peak, BPM, waveform)
│   ├── ppg_types.h           → Kiểu dữ liệu PPG
│   ├── moving_average_filter.c/h → Bộ lọc trung bình trượt
│   ├── spo2_estimator.c/h    → Ước lượng SpO2 (ratio-of-ratios)
│   └── measurement_types.h   → Kiểu kết quả đo
├── Drivers/           → Driver phần cứng
│   ├── BSP/ILI9341           → LCD driver
│   ├── BSP/STMPE811          → Cảm ứng (nếu dùng)
│   ├── Buzzer/               → Driver buzzer PWM
│   ├── MAX30102/             → Driver cảm biến MAX30102
│   ├── RTC/                  → Driver DS1307 RTC
│   ├── StatusLed/            → Driver LED trạng thái
│   └── Telemetry/            → Driver UART telemetry
├── GUI/               → Giao diện TouchGFX (screens, presenters)
├── Middlewares/        → TouchGFX framework, FreeRTOS
└── STM32F429I_DISCO_REV_D01.ioc → Cấu hình CubeMX
```

---

## 🧠 Luồng xử lý chính

```
MAX30102 (I2C3, 100kHz)
    │
    ▼ [FIFO Poll 20ms]
DefaultTask ──► PpgQueue (SPSC queue)
                    │
                    ▼ [100Hz, 10ms]
              DSP Task
              ├── PPG Engine: centered → peak detect → BPM (median RR)
              ├── SpO2 Estimator: ratio-of-ratios → SpO2%
              ├── Medical Alert: BPM/SpO2 vs thresholds + hysteresis 2s/3s
              ├── History: lưu kết quả khi nhấc ngón tay
              └── Publish: seqlock → GUI, Telemetry → UART
                    │
                    ▼
              GUI_Task (TouchGFX)
              ├── Màn hình chính: waveform PPG + BPM/SpO2 live
              ├── Màn hình lịch sử
              ├── Màn hình cấu hình
              └── Màn hình system info
```

---

## 🔑 Điểm đặc biệt trong thiết kế

1. **Bus I2C dùng chung** (MAX30102 + DS1307 trên I2C3) — tuần tự hóa bằng **mutex FreeRTOS**, có cơ chế phục hồi bus khi lỗi kéo dài.

2. **Seqlock** (lock-free reader/writer) giữa DSP task và GUI task — đảm bảo GUI không bao giờ đọc kết quả bị xé.

3. **Hysteresis thời gian** cho cảnh báo: phải vượt ngưỡng liên tục **2 giây** mới bật cảnh báo, và phải bình thường **3 giây** mới tắt — tránh nhấp nháy do peak sai.

4. **FIFO overflow tracking** — đếm số sample bị mất từ cảm biến, hiển thị trên GUI.

5. **Moving average adjustable** — người dùng có thể chỉnh cỡ cửa sổ lọc lúc chạy qua UI.

6. **Không cấp phát bộ nhớ động** — toàn bộ buffer tĩnh, phù hợp hệ nhúng.

7. **Config tập trung** — mọi ngưỡng, chân GPIO, timing đều nằm trong `Config/`, không hard-code trong driver.

---

## 📊 Thông số kỹ thuật chính

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

## IDE & Flash

- IDE mặc định: **STM32CubeIDE**
- Để đổi IDE: mở `STM32F429I_DISCO_REV_D01.ioc` bằng STM32CubeMX
- Hỗ trợ: EWARM (v8.50.9+), MDK-ARM, STM32CubeIDE
- Flash trực tiếp từ TouchGFX Designer qua GCC + STM32CubeProgrammer
- Màn hình: 320 × 240 pixels, 16bpp (RGB565)
