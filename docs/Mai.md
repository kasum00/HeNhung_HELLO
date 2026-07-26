# Hướng dẫn đọc code — FreeRTOS, Queue, I2C, RTC & History Storage

> Dựa trên mục tiêu: **"Xây dựng luồng FreeRTOS, quản lý task, queue dữ liệu, đồng bộ I2C, xử lý RTC DS1307 và lưu lịch sử đo"**

---

## 1. FreeRTOS Tasks & Khởi tạo hệ thống

| File | Đọc để hiểu |
|------|-------------|
| `main.c` | Init ngoại vi, tạo FreeRTOS tasks, cấu hình I2C3, SPI5, USART1 |
| `app_init.c` | `App_DefaultTask()` — SensorTask loop 20ms, gọi I2C, push queue |
| `dsp_task.c` | DSP FreeRTOS thread 10ms, pop queue, xử lý, seqlock publish |

## 2. Queue dữ liệu (Data Queue)

| File | Đọc để hiểu |
|------|-------------|
| `ppg_sample_queue.c` | Lock-free SPSC queue — push từ SensorTask, pop từ DSP Task |
| `telemetry_service.c` | Queue + UART TX task, format CSV |

## 3. Đồng bộ I2C (Mutex)

| File | Đọc để hiểu |
|------|-------------|
| `hw_config.h` | Địa chỉ I2C, chân GPIO, bus speed — MAX30102 `0xAE` + DS1307 `0xD0` share I2C3 |
| MAX30102 driver | Driver cảm biến PPG, truy cập I2C qua mutex |
| DS1307 driver | Driver RTC, truy cập I2C3 (dùng chung bus với MAX30102) |

## 4. Xử lý RTC DS1307

| File | Đọc để hiểu |
|------|-------------|
| `rtc_service.c` | Wrapper RTC — đọc/ghi thời gian từ DS1307 |
| DS1307 driver | Register map, init, read/write time |

## 5. Lưu lịch sử đo (History Storage)

| File | Đọc để hiểu |
|------|-------------|
| `measurement_types.h` | Kiểu dữ liệu history record |
| `temporary_history_store.c` | Lưu tạm vào RAM |
| `history_storage_interface.c` | Interface lưu trữ (chưa implement SD) |
| `dsp_task.c` | `handleFinalize()` — lưu lịch sử khi nhấc ngón tay |

---

## Thứ tự đọc khuyến nghị

| # | File | Mục đích |
|---|------|----------|
| 1 | `hw_config.h` | Hiểu cấu hình phần cứng, chân I2C, địa chỉ |
| 2 | `main.c` | Hiểu cách CubeMX init + tạo task |
| 3 | `ppg_sample_queue.c` | Hiểu cơ chế queue lock-free |
| 4 | `app_init.c` | Hiểu SensorTask loop (I2C + push queue) |
| 5 | `dsp_task.c` | Hiểu DSP task (pop queue + publish) |
| 6 | `rtc_service.c` + DS1307 driver | Hiểu RTC |
| 7 | `temporary_history_store.c` + `measurement_types.h` | Hiểu storage |
