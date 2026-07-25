# BÁO CÁO ĐỒ ÁN - MÔN HỆ NHúng

---

## GIỚI THIỆU

### Đề bài / Mục tiêu sản phẩm

Thiết kế và hiện thực hóa hệ thống đo lường nhịp tim (BPM) và nồng độ oxy trong máu (SpO2) sử dụng cảm biến quang học PPG trên nền tảng vi điều khiển STM32F4, với giao diện đồ họa TouchGFX trên màn hình LCD 240x320 px.

### Hướng tiếp cận

Hệ thống sử dụng cảm biến quang học MAX30102 để thu tín hiệu PPG (Photoplethysmogram) từ ngón tay người dùng. Tín hiệu thô được xử lý bởi chuỗi lọc số (DSP) gồm nhiều bộ lọc: trung bình trượt, trung vị, và Butterworth bậc 2 bậc thấp. Kết quả BPM, SpO2 và chất lượng tín hiệu (SQI) được hiển thị trực tiếp lên màn hình LCD thông qua giao diện TouchGFX. Toát độ thời gian thực được đảm bảo bởi hệ điều hành FreeRTOS với 4 luồng chạy song song: thu thập cảm biến, xử lý tín hiệu, hiển thị GUI và truyền telemettry qua UART.

### Sản phẩm

1. **Đo nhịp tim (BPM):** Nhận diện đỉnh PPG, tính BPM trung vị từ 5 khoảng RR, phạm vi 40-200 BPM.
2. **Đo nồng độ oxy máu (SpO2):** Thuật toán ratio-of-ratios với cửa sổ trượt 4 giây, phạm vi 70-100%.
3. **Đánh giá chất lượng tín hiệu (SQI):** Chỉ số 0-100%, phân loại Poor/Fair/Good.
4. **Hiển thị sóng PPG thời gian thực:** Vẽ đồ thị 240 điểm, chọn kênh IR/RED, 5 chế độ lọc.
5. **Lịch sử đo:** Bộ nhớ RAM vòng 20 bản ghi với timestamp RTC.
6. **Cảnh báo y tế:** BPM thấp/thấp, SpO2 thấp với cơ chế xác nhận theo thời gian.
7. **Phản hồi âm thanh:** Buzzer PWM với giai điệu cho các trạng thái khác nhau.

**Ảnh chụp minh họa:**

![Ảnh minh họa](../docs/images/boot_screen.png)
_Màn hình khởi động với thanh tiến trình_

![Ảnh minh họa](../docs/images/home_screen.png)
_Màn hình chính với 4 nút điều hướng_

![Ảnh minh họa](../docs/images/dashboard_screen.png)
_Màn hình đo BPM / SpO2 / SQI_

![Ảnh minh họa](../docs/images/waveform_screen.png)
_Màn hình sóng PPG thời gian thực_

> **Lưu ý:** Nếu chưa có ảnh chụp thực tế, hãy thêm ảnh vào thư mục `docs/images/` và cập nhật đường dẫn ở trên. Có thể chụp từimulator hoặc từ thiết bị thật.

---

## MÔI TRƯỜNG HOẠT ĐỘNG

### Phần cứng

- **Vi điều khiển:** STM32F429ZIT6 (ARM Cortex-M4, 180 MHz, LQFP144) trên board **STM32F429I-DISCO REV D01**
- **RAM ngoài:** SDRAM qua FMC Bank 2 (16-bit bus) - dùng làm frame buffer cho TouchGFX
- **Màn hình:** ILI9341 TFT LCD 240x320 px, RGB565, giao tiếp song song LTDC
- **IDE:** STM32CubeIDE (chính), hỗ trợ IAR EWARM v8.50.9+ và Keil MDK-ARM

### Bill of Materials

| STT | Tên linh kiện    | Ý nghĩa                                                   |
| --: | ---------------- | --------------------------------------------------------- |
|   1 | STM32F429I-DISCO | Board development kit chính, chứa MCU + LCD + SDRAM + LED |
|   2 | MAX30102         | Cảm biến quang học PPG đo SpO2 và nhịp tim                |
|   3 | DS1307           | Module RTC (Real-Time Clock) giữ giờ thực                 |
|   4 | Buzzer thụ động  | Phản hồi âm thanh (cảnh báo, thông báo)                   |
|   5 | Nguồn USB 5V     | Cung cấp điện cho board và cảm biến                       |

### Phần mềm

- **Hệ điều hành:** FreeRTOS V10.0.1 (CMSIS-RTOS V2)
- **GUI Framework:** TouchGFX (X-CUBE-TOUCHGFX 4.26.1)
- **HAL:** STM32Cube FW_F4 V1.28.1
- **Ngôn ngữ:** C (drivers, DSP, application) + C++ (TouchGFX GUI)

---

## SO ĐỒ SCHEMATIC

### Bảng kết nối linh kiện

| STM32F429 Pin | Module ngoại vi   | Chức năng               |
| ------------- | ----------------- | ----------------------- |
| **PA8**       | MAX30102 / DS1307 | I2C3_SCL (chung bus)    |
| **PC9**       | MAX30102 / DS1307 | I2C3_SDA (chung bus)    |
| **PA9**       | UART Telemetry    | USART1_TX (921600 baud) |
| **PA10**      | UART Telemetry    | USART1_RX (921600 baud) |
| **PF6**       | Buzzer thụ động   | TIM10_CH1 PWM (AF3)     |
| **PA0**       | Nút B1 trên board | EXTI0 (nhấn.start/stop) |
| **PG13**      | LED LD3 (xanh lá) | ALERT_LED_1 - cảnh báo  |
| **PG14**      | LED LD4 (đỏ)      | ALERT_LED_2 - cảnh báo  |

### Giao tiếp I2C3 (chung bus, 100 kHz)

```
STM32F429 (I2C3)
  ├── PA8 (SCL) ──┬── MAX30102 SCL  (địa chỉ 0x57)
  │                └── DS1307 SCL    (địa chỉ 0x68)
  │
  └── PC9 (SDA) ──┬── MAX30102 SDA
                   └── DS1307 SDA
```

> **Lưu ý:** Hai thiết bị I2C3 được truy xuất tuần tự bởi mutex FreeRTOS, đảm bảo không xung đột bus.

### Giao tiếp LCD (LTDC + SPI5)

```
STM32F429 ── LTDC (song song RGB565) ── ILI9341 LCD 240x320
             SPI5 (PF7/PF8/PF9) ── LCD init registers
             PC1 (CS), PC2 (RESET), PD13 (D/C)
```

### Giao tiếp SDRAM (FMC)

```
STM32F429 ── FMC Bank 2 (16-bit) ── SDRAM MT48LC4M16A2
             PF0-PF5, PF11-PF15, PG0-PG1, PG4-PG5, PG8, PG15
             PD0-PD1, PD8-PD10, PD14-PD15, PE0-PE15, PC0, PB5-PB6
```

![Sơ đồ khối hệ thống](../docs/images/system_block_diagram.png)
_Sơ đồ khối tổng quan hệ thống_

---

## TÍCH HỢP HỆ THỐNG

### Kiến trúc tổng thể

Hệ thống được thiết kế theo mô hình **pipeline song song** với 4 luồng FreeRTOS:

```
┌─────────────┐     ┌─────────────┐     ┌─────────────┐     ┌─────────────┐
│  SensorTask  │────▶│   DspTask   │────▶│  GUI_Task   │     │  Telemetry  │
│  (Thu thập)  │     │  (Xử lý)   │     │  (Hiển thị) │     │   (UART)    │
└─────────────┘     └─────────────┘     └─────────────┘     └─────────────┘
       │                   │                   │                    │
   MAX30102            DSP Filters          TouchGFX           USART1
   I2C FIFO          BPM/SpO2/SQI         LCD 240x320        921600 baud
```

### Thành phần phần cứng và vai trò

| Thành phần        | Vai trò                                                                      |
| ----------------- | ---------------------------------------------------------------------------- |
| **STM32F429ZIT6** | Vi điều khiển chính, Cortex-M4@180MHz, điều phối toàn bộ hệ thống            |
| **MAX30102**      | Cảm biến quang PPG, phát tia hồng ngoại/đỏ, thu tín hiệu phản xạ từ ngón tay |
| **DS1307**        | Đồng hồ thời gian thực, giữ timestamp cho bản ghi đo                         |
| **ILI9341 LCD**   | Màn hình hiển thị giao diện đồ họa 240x320 px                                |
| **SDRAM**         | Bộ nhớ đệm frame buffer cho TouchGFX (double buffering)                      |
| **Buzzer**        | Phát âm thanh cảnh báo và thông báo                                          |
| **LED LD3/LD4**   | Đèn cảnh báo nhấp nháy khi vượt ngưỡng BPM/SpO2                              |

### Thành phần phần mềm và vai trò

| Thành phần             | Vị trí                                   | Vai trò                                                                         |
| ---------------------- | ---------------------------------------- | ------------------------------------------------------------------------------- |
| **SensorTask**         | `Core/Src/main.c` → `App_DefaultTask()`  | Đọc FIFO MAX30102 mỗi 20ms, đọc RTC 1Hz, điều khiển buzzer/LED                  |
| **DspTask**            | `Application/dsp_task.c`                 | Chạy PPG engine 100Hz: phát hiện đỉnh, tính BPM, SpO2, SQI, lọc tín hiệu        |
| **GUI_Task**           | TouchGFX framework                       | Render giao diện, đọc kết quả DSP qua seqlock, hiển thị lên LCD                 |
| **TelemetryTask**      | `Telemetry/telemetry_service.c`          | Định dạng CSV và gửi dữ liệu qua USART1                                         |
| **MAX30102 Driver**    | `Drivers/MAX30102/`                      | Giao tiếp I2C với cảm biến, cấu hình FIFO, đọc raw samples                      |
| **DS1307 Driver**      | `Drivers/DS1307/`                        | Giao tiếp I2C với RTC, đọc/ghi thời gian BCD                                    |
| **DSP Filters**        | `DSP/`                                   | Moving Average, Median, Lowpass Butterworth bậc 2                               |
| **PPG Engine**         | `DSP/ppg_measurement.c`                  | Phát hiện đỉnh, khoảng RR, tính BPM trung vị                                    |
| **SpO2 Estimator**     | `DSP/spo2_estimator.c`                   | Tính SpO2 bằng ratio-of-ratios với calibration thực nghiệm                      |
| **Medical Alert**      | `Config/alert_config.h`                  | Cảnh báo BPM thấp/thấp, SpO2 thấp với hysteresis thời gian                      |
| **TouchGFX GUI**       | `TouchGFX/gui/`                          | 8 màn hình: Boot, Home, Dashboard, Waveform, History, DateTime, Settings, About |
| **Application Bridge** | `Application/application_gui_bridge.cpp` | Kết nối giữa DSP task và GUI qua seqlock (lock-free)                            |

### Đồng bộ hóa giữa các luồng

| Cơ chế              | Vị trí                   | Mục đích                                                        |
| ------------------- | ------------------------ | --------------------------------------------------------------- |
| **I2C3 Mutex**      | `Application/app_init.c` | Truy xuất tuần tự MAX30102 + DS1307 trên cùng bus               |
| **PpgQueue (SPSC)** | `DSP/ppg_types.h`        | Hàng đợi lock-free chuyển mẫu thô từ Sensor → DSP               |
| **Seqlock**         | `Application/dsp_task.c` | DSP ghi, GUI đọc PpgResult không bị xé dữ liệu (dùng `__DMB()`) |
| **History Mutex**   | `Application/dsp_task.c` | Đồng bộ ghi/đọc lịch sử đo giữa DSP và GUI                      |
| **Atomic Word**     | `DSP/dsp_task.c`         | Chuyển chế độ lọc và cửa sổ filter từ GUI → DSP                 |

---

## ĐẶC TẢ HÀM

### 1. Hàm khởi tạo MAX30102

```c
/**
 *  Khởi tạo cảm biến MAX30102 ở chế độ SpO2.
 *  Cấu hình FIFO: không lọc trung bình,rollover enabled, almost-full=15.
 *  Cấu hình LED: RED + IR, dòng ~6.4 mA mỗi kênh.
 *  Tốc độ lấy mẫu: 100 Hz, độ phân giải 18-bit.
 *  @return HAL_OK nếu thành công, HAL_ERROR nếu PART_ID không khớp (0x15)
 */
HAL_StatusTypeDef MAX30102_Init(I2C_HandleTypeDef *hi2c);
```

### 2. Hàm đọc FIFO MAX30102

```c
/**
 *  Đọc tối đa `count` mẫu từ FIFO của MAX30102.
 *  Mỗi mẫu gồm 6 byte: 3 byte RED (18-bit) + 3 byte IR (18-bit).
 *  Dữ liệu được lưu vào mảng `red` và `ir` (uint32_t).
 *  @param hi2c   Con trỏ I2C handle
 *  @param red    Mảng đầu ra giá trị RED
 *  @param ir     Mảng đầu ra giá trị IR
 *  @param count  Số mẫu tối đa cần đọc
 *  @return Số mẫu thực tế đã đọc từ FIFO
 */
uint32_t MAX30102_ReadFIFO(I2C_HandleTypeDef *hi2c,
                            uint32_t *red, uint32_t *ir,
                            uint32_t count);
```

### 3. Hàm xử lý DSP chính

```c
/**
 *  Hàm xử lý một mẫu PPG thô qua chuỗi lọc và phát hiện đỉnh.
 *  Pipeline: DC tracking → Optional Filter (Median/Lowpass/MA) → Peak Detection
 *  @param redRaw   Giá trị RED thô từ cảm biến
 *  @param irRaw    Giá trị IR thô từ cảm biến
 *  @param result   Kết quả đầu ra (BPM, SpO2, SQI, trạng thái)
 *  @return true nếu có kết quả mới, false nếu đang chờ đủ dữ liệu
 */
bool Dsp_ProcessSample(uint32_t redRaw, uint32_t irRaw, PpgResult *result);
```

### 4. Hàm tính SpO2

```c
/**
 *  Tính SpO2 bằng thuật toán ratio-of-ratios.
 *  Sử dụng cửa sổ trượt 4 giây, mỗi giây chia thành 1 sub-block.
 *  Tính ratio R = (AC_RED/DC_RED) / (AC_IR/DC_IR).
 *  Polynomial calibration: SpO2 = 94.845 + 30.354*R - 45.060*R^2
 *  @param state   Trạng thái PPG engine (chứa mẫu IR/RED đã lưu)
 *  @return Giá trị SpO2 (70-100%), hoặc 0 nếu chưa đủ dữ liệu
 */
float SpO2_Estimate(PpgEngineState *state);
```

### 5. Hàm lọc trung bình trượt

```c
/**
 *  Áp dụng bộ lọc trung bình trượt (moving average) với cửa sổ N.
 *  Triển khai bằng running-sum, độ phức tạp O(1)/mẫu.
 *  @param state   Trạng thái bộ lọc (chứa buffer vòng và tổng chạy)
 *  @param input   Giá trị đầu vào (mẫu thô hoặc đã lọc)
 *  @return Giá trị đầu ra sau khi lọc
 */
float MovingAverage_Process(MovingAverageState *state, float input);
```

### 6. Hàm lọc Butterworth bậc thấp

```c
/**
 *  Áp dụng bộ lọc Butterworth bậc 2, tần số cắt 4 Hz, fs = 100 Hz.
 *  Hệ số: b = [0.020083, 0.040167, 0.020083], a = [1.0, -1.561018, 0.641352]
 *  Triển khai Direct Form II Transposed (2 biến trạng thái).
 *  @param state   Trạng thái bộ lọc (2 biến trạng thái)
 *  @param input   Giá trị đầu vào
 *  @return Giá trị đầu ra sau khi lọc
 */
float Lowpass_Process(LowpassState *state, float input);
```

### 7. Hàm hiển thị MetricCard (TouchGFX)

```c
/**
 *  Widget hiển thị một chỉ số đo lớn (BPM, SpO2, SQI).
 *  Gồm: thanh accent màu ở trên (3px), nhãn phụ, giá trị lớn, đơn vị.
 *  @param value       Giá trị cần hiển thị (int hoặc float)
 *  @param unit        Chuỗi đơn vị ("bpm", "%")
 *  @param accentColor Màu thanh accent (xanh, xanh lá, vàng)
 */
void MetricCard::update(int value, const char *unit, touchgfx::Color::ColorName accentColor);
```

### 8. Hàm vẽ sóng PPG (Waveform)

```c
/**
 *  Vẽ đồ thị PPG 240 điểm lên CanvasWidget.
 *  Tín hiệu được nghịch quanh tâm để đỉnh systolic hiển thị lên trên.
 *  Hiển thị đường lưới mờ, đánh dấu đỉnh phát hiện được (tối đa 12 đỉnh).
 *  @param canvas   Con trỏ canvas để vẽ
 *  @param width    Chiều rộng vùng vẽ (240 px)
 *  @param height   Chiều dài vùng vẽ (156 px)
 *  @param data     Mảng 240 giá trị PPG đã xử lý
 *  @param peaks    Mảng chỉ số đỉnh, numPeaks đỉnh
 */
void WaveformView::drawLineGraph(CanvasWidget *canvas,
                                  int width, int height,
                                  const int16_t *data,
                                  const uint16_t *peaks, uint8_t numPeaks);
```

### 9. Hàm cảnh báo y tế

```c
/**
 *  Đánh giá BPM và SpO2 hiện tại so với ngưỡng cảnh báo.
 *  Ngưỡng: BPM thấp < 45, BPM cao > 120, SpO2 thấp < 92%.
 *  Hysteresis thời gian: xác nhận 2 giây để kích hoạt, 3 giây để tắt.
 *  Khi cảnh báo.active: LED LD3/LD4 nhấp nháy xen kẽ 300ms.
 *  @param bpm    Giá trị BPM hiện tại
 *  @param spo2   Giá trị SpO2 hiện tại
 *  @param result Kết quả cảnh báo (loại, trạng thái active/inactive)
 */
void MedicalAlert_Evaluate(float bpm, float spo2, AlertResult *result);
```

### 10. Hàm telemettry

```c
/**
 *  Khởi tạo dịch vụ telemettry UART.
 *  Tạo 2 hàng đợi FreeRTOS: event_queue (24 phần tử, ưu tiên cao)
 *  và waveform_queue (64 phần tử, cho phép rơi mẫu).
 *  Định dạng dữ liệu: CSV qua USART1 ở 921600 baud.
 *  Tất cả lệnh gọi publish đều non-blocking (timeout=0).
 *  @return HAL_OK nếu khởi tạo thành công
 */
HAL_StatusTypeDef Telemetry_Start(void);
```

---

## KẾT QUẢ

### Màn hình Boot

![Boot Screen](../docs/images/boot_screen.png)
_Màn hình khởi động với logo "PPG Analyzer", phiên bản Firmware v1.0.0 và thanh tiến trình 7 giai đoạn: Starting system → Initializing display → Checking sensor → Checking RTC → Checking storage → Loading configuration → Ready_

### Màn hình chính (Home)

![Home Screen](../docs/images/home_screen.png)
_Màn hình chính với thanh trạng thái (trạng thái cảm biến, đồng hồ RTC) và 4 nút điều hướng: Measure (đo), Waveform (sóng), History (lịch sử), Clock (đồng hồ)_

### Màn hình đo (Dashboard)

![Dashboard Screen](../docs/images/dashboard_screen.png)
_Màn hình đo chính hiển thị 3 thẻ chỉ số: BPM (nhịp tim), SpO2 (oxy máu), SQI (chất lượng tín hiệu). Thanh trạng thái đo ở trên cùng. Nút Start/Stop điều khiển bắt đầu/dừng đo_

### Màn hình sóng PPG (Waveform)

![Waveform Screen](../docs/images/waveform_screen.png)
_Màn hình hiển thị sóng PPG thời gian thực. 3 nút điều khiển: chọn kênh (IR/RED), chế độ lọc (Raw/MovingAvg/Median/Lowpass/Med+LP), kích thước cửa sổ lọc. Đỉnh phát hiện được đánh dấu bằng chấm vàng_

### Màn hình lịch sử (History)

![History Screen](../docs/images/history_screen.png)
_Màn hình lịch sử đo với phân trang, mỗi trang 5 bản ghi. Mỗi bản ghi hiển thị: thời gian, BPM, SpO2, SQI, trạng thái hợp lệ_

### Màn hình cài đặt giờ (DateTime)

![DateTime Screen](../docs/images/datetime_screen.png)
_Màn hình cài đặt RTC với 6 trường (Năm/Tháng/Ngày/Giờ/Phút/Giây), nút bấm +/- để điều chỉnh. Hiển thị thời gian RTC hiện tại_

### Kết quả chạy thực tế

![Kết quả đo thực tế](../docs/images/actual_measurement.png)
_Hình ảnh đo thực tế với ngón tay đặt trên cảm biến MAX30102, hiển thị BPM và SpO2 trên LCD_

> **Lưu ý:** Chụp thêm ảnh thực tế từ thiết bị hoặc simulator và đặt vào thư mục `docs/images/` để bổ sung vào báo cáo.

---

## TÀI LIỆU THAM KHẢO

1. STM32F429I-DISCO Reference Manual (RM0090)
2. MAX30102 Datasheet - Maxim Integrated
3. DS1307 Datasheet - Maxim Integrated
4. TouchGFX Documentation - STMicroelectronics
5. FreeRTOS Reference Manual - Real Time Engineers Ltd.
6. ILI9341 Datasheet - Ilitek

---
