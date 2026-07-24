# Báo cáo BTL Hệ Nhúng Nhóm Hello
## Cấu hình chân STM32F429I-DISCO

### Bảng cấu hình chân

| Thiết bị/chức năng | Ngoại vi | Chân STM32 | Cấu hình trong STM32CubeMX | Thông số chi tiết |
|---|---|---|---|---|
| Cảm biến nhịp tim và SpO₂ – SCL | I2C3 | PA8 | `I2C3_SCL` | Standard Mode, 100 kHz, địa chỉ 7-bit |
| Cảm biến nhịp tim và SpO₂ – SDA | I2C3 | PC9 | `I2C3_SDA` | Dùng chung bus I2C3 với RTC DS1307 |
| RTC DS1307 – SCL | I2C3 | PA8 | `I2C3_SCL` | Standard Mode, 100 kHz |
| RTC DS1307 – SDA | I2C3 | PC9 | `I2C3_SDA` | Dùng chung bus I2C3 với cảm biến |
| Còi thụ động | TIM10 Channel 1 | PF6 | `TIM10_CH1` – PWM Generation CH1 | PWM 2 kHz; Prescaler 179, Period 499, Pulse 250 nếu TIM10 clock = 180 MHz |
| LED cảnh báo 1 | GPIO | PG13 | `GPIO_Output` | Output Push Pull, No Pull, Low Speed, mức ban đầu Low |
| LED cảnh báo 2 | GPIO | PG14 | `GPIO_Output` | Output Push Pull, No Pull, Low Speed, mức ban đầu Low |
| Nút nhấn B1 | EXTI | PA0 | `GPIO_EXTI0` | Rising Edge, No Pull; bật `EXTI line0 interrupt` trong NVIC |
| USART truyền dữ liệu | USART1 TX | PA9 | `USART1_TX` | Asynchronous, 115200 baud, 8 data bits, no parity, 1 stop bit |
| USART nhận dữ liệu | USART1 RX | PA10 | `USART1_RX` | Receive and Transmit, Oversampling 16, Flow Control Disable |

## Thiết lập ngoại vi

| Ngoại vi | Thiết lập |
|---|---|
| I2C3 | Mode: I2C; Speed Mode: Standard; Clock Speed: 100000 Hz; Addressing Mode: 7-bit; Dual Address: Disable; General Call: Disable; No Stretch: Disable |
| TIM10 | Clock Source: Internal Clock; Channel 1: PWM Generation CH1; Counter Mode: Up; Clock Division: DIV1; Auto-reload Preload: Disable |
| TIM10 PWM CH1 | PWM Mode 1; Pulse: 250; Output Polarity: High; Fast Mode: Disable |
| GPIO PG13 | Output Push Pull; No Pull; Low Speed; Initial Level: Low; User Label: `LED_GREEN` |
| GPIO PG14 | Output Push Pull; No Pull; Low Speed; Initial Level: Low; User Label: `LED_RED` |
| GPIO PA0 | External Interrupt Mode with Rising Edge; No Pull; User Label: `B1` |
| NVIC | Enable `EXTI line0 interrupt`; không cần bật ngắt TIM10 |
| USART1 | Mode: Asynchronous; Baud Rate: 115200; Word Length: 8 Bits; Parity: None; Stop Bits: 1; Data Direction: Receive and Transmit; Oversampling: 16; Hardware Flow Control: Disable |

> Giữ nguyên các chân và ngoại vi đang dùng cho TouchGFX, LTDC, FMC/SDRAM, DMA2D và LCD. Không sử dụng `Clear Pinouts` hoặc `Reset Configuration`.


## Thiết kế giao diện TouchGFX

### Tổng quan giao diện

Giao diện của hệ thống được xây dựng bằng TouchGFX trên màn hình của bo STM32F429I-DISCO. Giao diện gồm ba màn hình chính, cho phép người dùng lựa chọn chế độ hiển thị, theo dõi các thông số đo theo thời gian thực và quan sát sự thay đổi của dữ liệu dưới dạng đồ thị.

Các màn hình sử dụng bố cục đơn giản, màu sắc thống nhất và các nút điều hướng rõ ràng nhằm giúp người dùng dễ dàng thao tác trong quá trình đo. Dữ liệu nhịp tim, SpO₂ và thời gian được cập nhật liên tục từ cảm biến và RTC.

## Danh sách màn hình

| Màn hình | Chức năng | Nội dung hiển thị | Nút điều hướng |
|---|---|---|---|
| Screen1 – Màn hình chính | Lựa chọn chế độ hiển thị | Tên hệ thống và hai lựa chọn: hiển thị thông số hoặc hiển thị đồ thị | `THÔNG SỐ` chuyển sang Screen2; `ĐỒ THỊ` chuyển sang Screen3 |
| Screen2 – Màn hình thông số | Hiển thị dữ liệu đo dưới dạng số | Nhịp tim, SpO₂, thời gian đo và trạng thái của người dùng | Nút `<` quay về Screen1; `CHUYỂN SANG ĐỒ THỊ` chuyển sang Screen3 |
| Screen3 – Màn hình đồ thị | Hiển thị lịch sử biến đổi của dữ liệu | Giá trị nhịp tim hiện tại, đồ thị nhịp tim, giá trị SpO₂ hiện tại, đồ thị SpO₂ và các mốc thời gian | Nút `<` quay về Screen1; `XEM THÔNG SỐ` chuyển sang Screen2 |

### Màn hình 1 – Màn hình lựa chọn

Screen1 là màn hình được hiển thị đầu tiên khi hệ thống khởi động. Màn hình có vai trò giới thiệu chức năng chính của thiết bị và cho phép người dùng lựa chọn kiểu hiển thị dữ liệu.

Phần giữa màn hình gồm hai nút chức năng:

- **THÔNG SỐ:** mở màn hình hiển thị dữ liệu theo thời gian thực dưới dạng số.
- **ĐỒ THỊ:** mở màn hình hiển thị dữ liệu dưới dạng đồ thị đường.

Thiết kế của Screen1 được tối giản để người dùng có thể nhận biết và lựa chọn chức năng ngay khi khởi động hệ thống.

### Màn hình 2 – Hiển thị thông số

Screen2 hiển thị trực tiếp các giá trị được thu thập từ cảm biến và RTC. Các thông số được bố trí thành từng vùng riêng biệt nhằm tăng khả năng quan sát.

| Thông tin | Nội dung |
|---|---|
| Nhịp tim | Hiển thị giá trị nhịp tim hiện tại theo đơn vị BPM |
| SpO₂ | Hiển thị nồng độ oxy trong máu theo đơn vị phần trăm |
| Thời gian | Hiển thị thời gian thực theo định dạng giờ, phút và giây |
| Trạng thái | Hiển thị trạng thái bình thường hoặc trạng thái cảnh báo |

Các giá trị BPM, SpO₂, thời gian và trạng thái được khai báo dưới dạng wildcard trong TouchGFX để có thể thay đổi trong quá trình chương trình hoạt động.

Khi dữ liệu nằm ngoài giới hạn cho phép, nội dung trạng thái được thay đổi để thông báo cho người dùng. Đồng thời, hệ thống kích hoạt LED và còi cảnh báo theo logic điều khiển của chương trình.

### Màn hình 3 – Hiển thị đồ thị

Screen3 được sử dụng để biểu diễn sự thay đổi của nhịp tim và SpO₂ theo thời gian. Màn hình gồm hai đồ thị đường được bố trí theo chiều dọc.

| Thành phần | Nội dung |
|---|---|
| Đồ thị nhịp tim | Hiển thị sự thay đổi của giá trị BPM trong các lần đo gần nhất |
| Đồ thị SpO₂ | Hiển thị sự thay đổi của nồng độ oxy trong máu |
| Giá trị hiện tại | Hiển thị BPM và SpO₂ mới nhất bên cạnh tên từng đồ thị |
| Trục tung | Biểu diễn khoảng giá trị của BPM hoặc SpO₂ |
| Trục hoành | Biểu diễn các mốc thời gian đo |

Đồ thị nhịp tim sử dụng khoảng giá trị từ 40 đến 140 BPM. Đồ thị SpO₂ sử dụng khoảng giá trị từ 85% đến 100%. Các điểm dữ liệu mới được bổ sung liên tục để người dùng có thể theo dõi xu hướng biến đổi của hai thông số.

Do hai đồ thị được cập nhật cùng thời điểm, các mốc thời gian được bố trí thống nhất nhằm giúp người dùng dễ dàng so sánh dữ liệu.

### Luồng chuyển màn hình

| Màn hình hiện tại | Thao tác | Màn hình tiếp theo |
|---|---|---|
| Screen1 | Nhấn `THÔNG SỐ` | Screen2 |
| Screen1 | Nhấn `ĐỒ THỊ` | Screen3 |
| Screen2 | Nhấn `CHUYỂN SANG ĐỒ THỊ` | Screen3 |
| Screen2 | Nhấn nút `<` | Screen1 |
| Screen3 | Nhấn `XEM THÔNG SỐ` | Screen2 |
| Screen3 | Nhấn nút `<` | Screen1 |

Ngoài các nút cảm ứng trên màn hình, nút vật lý B1 được sử dụng để chuyển đổi giữa màn hình thông số và màn hình đồ thị trong quá trình hệ thống hoạt động.

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
