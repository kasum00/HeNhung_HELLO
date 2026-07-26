# CÂU HỎI THẦY CÓ THỂ HỎI - Phần: FreeRTOS, Task, Queue, I2C, RTC DS1307, Lưu lịch sử đo

> **Lưu ý:** File này tổng hợp các câu hỏi thầy có thể hỏi khi báo cáo, kèm gợi ý trả lời tham khảo.
> Phần của Mai: Quản lý task FreeRTOS, queue dữ liệu, đồng bộ I2C, xử lý RTC DS1307 và lưu lịch sử đo.

---

## MỤC LỤC

1. [FreeRTOS & Task Management](#1-freertos--task-management)
2. [Queue & Đồng bộ dữ liệu](#2-queue--đồng-hó-dữ-liệu)
3. [Mutex & Cơ chế đồng bộ](#3-mutex--cơ-chế-đồng-bộ)
4. [I2C Communication](#4-i2c-communication)
5. [RTC DS1307](#5-rtc-ds1307)
6. [Lưu lịch sử đo (History Store)](#6-lưu-lịch-sử-đo-history-store)
7. [Câu hỏi tổng hợp / liên kết](#7-câu-hỏi-tổng-hợp--liên-kết)

---

## 1. FreeRTOS & Task Management

### 1.1. Hệ thống có bao nhiêu task? Chúng chạy song song như thế nào?

**Gợi ý:** 4 task:
| Task | Ưu tiên | Stack | Vai trò |
|------|---------|-------|---------|
| `defaultTask` (Sensor Task) | `osPriorityNormal` | 512 bytes | Đọc cảm biến MAX30102, poll RTC, điều khiển buzzer/LED |
| `GUI_Task` | `osPriorityNormal` | 32 KB | Hiển thị TouchGFX |
| `dspTask` | `osPriorityNormal` | 4 KB | Xử lý tín hiệu PPG (BPM, SpO2) |
| `telemetryTask` | `osPriorityBelowNormal` | 2 KB | Gửi dữ liệu UART |

> **Lưu ý:** Tất cả task đều có cùng priority `osPriorityNormal` (trừ telemetry), FreeRTOS sẽ time-slice round-robin giữa chúng.

---

### 1.2. Tại sao `defaultTask` tạo `dspTask` và `telemetryTask` sau khi scheduler đã chạy, thay vì tạo trong `main()`?

**Gợi ý:** Mô hình **lazy task creation**:

- `defaultTask` cần tạo mutex `s_i2cMutex` trước khi các task khác có thể dùng I2C
- Tránh race condition: nếu tạo task trước mutex, DSP task có thể cố truy cập I2C trước khi mutex sẵn sàng
- Kiểm soát khởi tạo theo thứ tự: mutex -> sensor init -> start DSP -> start telemetry

---

### 1.3. Tại sao `telemetryTask` có priority thấp hơn các task khác?

**Gợi ý:** Telemetry chỉ là **后台 (background) output**, không ảnh hưởng đến chất lượng đo. Nếu telemetry bị chậm, queue sẽ buffer lại. Trong khi đó, DSP task cần chạy đúng thời gian (100 Hz) để không mất mẫu tín hiệu PPG. Sensor task cũng cần đúng 50 Hz để đọc FIFO đúng lúc.

---

### 1.4. `configTICK_RATE_HZ = 1000` có ý nghĩa gì? Tại sao chọn 1000 Hz?

**Gợi ý:**

- Tick = 1ms => độ phân giải thời gian tối thiểu = 1ms
- Phù hợp cho PPG processing (DSP chạy mỗi 10ms = 100 Hz, sensor mỗi 20ms = 50 Hz)
- `osDelay(1)` = chờ 1ms, đủ nhỏ cho timing chính xác
- Nếu tick rate quá thấp (ví dụ 100 Hz), mỗi tick = 10ms, không đủ độ phân giải

---

### 1.5. Tại sao dùng CMSIS-RTOS V2 wrapper thay vì FreeRTOS API trực tiếp?

**Gợi ý:**

- CMSIS-RTOS V2 là chuẩn ARM, code dễ port sang MCU khác
- API thống nhất: `osThreadNew()`, `osMutexNew()`, `osMessageQueueNew()` ...
- STM32CubeMX tự generate code với CMSIS-RTOS V2
- Project chỉ dùng `xQueueCreate`/`xQueueSend` qua CMSIS wrapper, **không gọi trực tiếp** FreeRTOS queue API

---

### 1.6. `configSUPPORT_STATIC_ALLOCATION = 1` và `configSUPPORT_DYNAMIC_ALLOCATION = 1` - tại sao dùng cả hai?

**Gợi ý:**

- **Static** (dspTask, telemetryTask): dùng khi muốn kiểm soát vùng nhớ, tránh fragment heap, đảm bảo task không bị fail do hết RAM
- **Dynamic** (defaultTask, GUI_Task): tạo qua CubeMX, thuận tiện
- DSP task dùng static (`StaticTask_t` + `uint32_t s_dspStack[1024]`) vì nó được tạo từ trong task khác (lazy creation)

---

### 1.7. Nếu tất cả task đều chạy được, CPU Cortex-M4F xử lý thế nào? Có bottleneck không?

**Gợi ý:**

- Cortex-M4F @ 180 MHz, có FPU và DSP指令
- DSP task xử lý median filter, Butterworth filter, peak detection - đều tận dụng được DSP指令
- Bottleneck tiềm ẩn: I2C bus (100 kHz) - nhiều thiết bị share, mỗi giao dịch mất ~0.5-1ms
- Giải pháp: mutex chỉ lock khi cần, I2C timeout 100ms để không block quá lâu

---

## 2. Queue & Đồng bộ dữ liệu

### 2.1. Hệ thống dùng những loại queue nào? Phân biệt chúng.

**Gợi ý:**

| Cơ chế                             | Vị trí                | Loại                         | Sync                     |
| ---------------------------------- | --------------------- | ---------------------------- | ------------------------ |
| **SPSC Ring Buffer**               | `ppg_sample_queue.c`  | Lock-free SPSC, 256 entries  | `__DMB()` memory barrier |
| **CMSIS Message Queue** (event)    | `telemetry_service.c` | `osMessageQueueNew(24, ...)` | FreeRTOS kernel          |
| **CMSIS Message Queue** (waveform) | `telemetry_service.c` | `osMessageQueueNew(64, ...)` | FreeRTOS kernel          |

---

### 2.2. SPSC Ring Buffer hoạt động như thế nào? Tại sao không dùng mutex?

**Gợi ý:**

- **SPSC** = Single Producer, Single Consumer: chỉ 1 task ghi (Sensor), 1 task đọc (DSP)
- Dùng `__DMB()` (Data Memory Barrier) để đảm bảo thứ tự truy cập bộ nhớ trên Cortex-M4
- **Không cần mutex** vì mỗi biến chỉ có 1 writer: `head` chỉ Sensor write, `tail` chỉ DSP write
- Ưu điểm: **không context switch overhead**, **không deadlock**, **latency thấp** (đọc/ghi trong vài clock cycle)
- Nhược điểm: chỉ áp dụng được khi mỗi biến có đúng 1 producer + 1 consumer

---

### 2.3. `__DMB()` là gì? Tại sao cần nó trong lock-free queue?

**Gợi ý:**

- `__DMB()` = Data Memory Barrier (ARM instruction)
- Cortex-M4 có **write buffer** và **pipeline**, lệnh có thể thực hiện ngoài thứ tự
- Producer ghi data trước, rồi ghi head sau. Nếu CPU reorder, consumer có thể thấy head mới nhưng data cũ (torn read)
- `__DMB()` sau ghi data đảm bảo data visible trước khi head được cập nhật
- Reader cũng cần `__DMB()` sau đọc head để ensure đọc data mới nhất

---

### 2.4. Khi queue đầy, xảy ra điều gì? Tại sao chọn drop thay vì block?

**Gợi ý:**

- `PpgQueue_Push()` khi full: sample bị **đổ bỏ** (drop), tăng `s_dropped` counter
- **Lý do không block:**
  - Sensor task phải đọc FIFO đúng 50 Hz, nếu block sẽ mất sample tiếp theo
  - DSP task bị chậm = mất nhịp đo BPM
  - Drop 1-2 sample trong 256 là chấp nhận được, DSP có median filter để xử lý
- **Chỉ số drop quá cao** => hệ thống bị overload, cần debug

---

### 2.5. Seqlock (DSP -> GUI) hoạt động như thế nào?

**Gợi ý:**

- Biến `s_pubGen` (generation counter):
  - **Ghi (DSP task):** `s_pubGen++` (trở thành số lẻ) -> `DMB` -> copy data -> `DMB` -> `s_pubGen++` (trở thành số chẵn)
  - **Đọc (GUI task):** Đọc gen1 -> copy data -> đọc gen2. Nếu gen1 != gen2 hoặc cả hai đều lẻ => data đang bị ghi => **retry** (tối đa 8 lần)
- Ưu điểm: GUI **không cần mutex**, không block DSP task
- Nhược điểm: có thể retry nhiều lần, nhưng với data nhỏ (PpgResult ~64 bytes) thì gần như luôn thành công lần 1

---

### 2.6. CMSIS Message Queue dùng cho telemetry - tại sao chia thành 2 queue (event + waveform)?

**Gợi ý:**

- **Event queue (24 slots):** Kết quả BPM, SpO2, alert, history record - **quyết định ưu tiên cao**
- **Waveform queue (64 slots):** Dữ liệu sóng PPG - **thấp ưu tiên**, có thể bỏ nếu quá tải
- Telemetry task: **drain event trước** (không bỏ event quan trọng), rồi waveform burst tối đa 8/cycle
- Nếu merge 1 queue: waveform có thể lấn event, gây mất dữ liệu quan trọng

---

## 3. Mutex & Cơ chế đồng bộ

### 3.1. Hệ thống dùng mấy mutex? Mục đích từng cái?

**Gợi ý:**
| Mutex | File | Mục đích |
|-------|------|----------|
| `s_i2cMutex` | `app_init.c:82` | Serialize truy cập I2C3 (MAX30102 + DS1307) |
| `s_histMutex` | `dsp_task.c:212` | Bảo vệ đọc/ghi TemporaryHistory |

---

### 3.2. Tại sao cần mutex cho I2C? Nếu không có mutex thì sao?

**Gợi ý:**

- I2C3 bus **share** giữa MAX30102 và DS1307
- Sensor task đọc MAX30102 (FIFO) và poll DS1307 (RTC) **trong cùng 1 task**
- Nếu task khác (ví dụ DSP task) cũng cố truy cập I2C => **data corruption**, bus bị treo
- Mutex đảm bảo: **một lúc chỉ có 1 thiết bị giao tiếp trên bus**
- Thiếu mutex: bus SDA/SCL bị giữ低 (bus lockup), cần bus recovery

---

### 3.3. `osMutexAcquire(s_i2cMutex, osWaitForever)` - tại sao dùng `osWaitForever`? Có nguy hiểm không?

**Gợi ý:**

- `osWaitForever`: task sẽ block mãi cho đến khi có mutex
- **Trong trường hợp này an toàn** vì:
  - Chỉ có Sensor task giữ mutex và release ngay sau mỗi I2C transaction (~1ms)
  - Không có task nào giữ mutex quá lâu
  - Không có nested mutex (không lock mutex này khi đang giữ mutex khác)
- **Nguy hiểm nếu:** deadlock (task A giữ mutex1 rồi chờ mutex2, task B ngược lại) - nhưng hệ thống này **không có nested locking**

---

### 3.4. Double buffer trong RTC Service hoạt động thế nào? Tại sao cần?

**Gợi ý:**

- `s_buf[0]` và `s_buf[1]`: Sensor task ghi vào buffer `active ^ 1` (non-active), rồi flip `active`
- GUI đọc từ `s_buf[s_active]`
- **Không cần mutex** vì chỉ 1 writer, reader luôn đọc buffer ổn định
- Tương tự SPSC nhưng ở mức snapshot (full DateTime struct)
- Nếu không dùng double buffer: GUI có thể đọc半 (half-written) DateTime

---

### 3.5. Volatile sentinel variables (DSP mode/window requests) hoạt động thế nào?

**Gợi ý:**

```c
volatile uint32_t s_filterModeRequest;   // GUI -> DSP
volatile uint32_t s_filterWindowRequest; // GUI -> DSP
```

- GUI ghi giá trị mới, DSP đọc và xử lý (xóa request = set 0)
- Đơn giản vì: **không cần atomic** (viết/đọc uint32_t là atomic trên Cortex-M4)
- `volatile` đảm bảo compiler không cache giá trị cũ
- Ưu điểm: **không cần mutex cho request đơn giản 1 chiều**

---

## 4. I2C Communication

### 4.1. Các thiết bị nào share I2C3? Địa chỉ mỗi cái?

**Gợi ý:**
| Thiết bị | 7-bit Address | 8-bit Write | 8-bit Read |
|----------|--------------|-------------|------------|
| MAX30102 | 0x57 | 0xAE | 0xAF |
| DS1307 | 0x68 | 0xD0 | 0xD1 |
| STMPE811 | 0x41 | 0x82 | 0x83 |

---

### 4.2. I2C bus timeout 100ms - tại sao chọn giá trị này?

**Gợi ý:**

- I2C3 @ 100 kHz Standard Mode
- MAX30102 FIFO burst read (16 samples): ~200 bytes -> ~2ms
- DS1307 DateTime read: 7 bytes -> <1ms
- **100ms đủ lớn** cho mọi transaction bình thường
- **Nhỏ enough** để task không bị block quá lâu nếu bus treo
- Nếu timeout quá nhỏ: false timeout. Quá lớn: task bị treo dài

---

### 4.3. Bus recovery (`i2cRecoverBus()`) hoạt động thế nào? Khi nào cần?

**Gợi ý:**

- **Khi cần:** SDA bị giữ low (bus lockup) do lỗi phần cứng hoặc interrupted transaction
- **Cách làm:**
  1. DeInit I2C3 (`HAL_I2C_DeInit`)
  2. ReInit I2C3 (`HAL_I2C_Init`)
  3. Reconfigure MAX30102 (vì bị reset bởi power cycle)
- Trigger: `failStreak % 10 == 0` (mỗi 10 lỗi liên tiếp, thử recovery)
- Sau recovery: `failStreak` vẫn tiếp tục count

---

### 4.4. Cơ chế fault tolerance với `failStreak` hoạt động thế nào?

**Gợi ý:**

- Mỗi lần I2C lỗi: `failStreak++`
- `failStreak < 5`: **tolerant** - bỏ qua lỗi, thử lại lần sau (transient errors)
- `failStreak >= 5`: set `g_sensorOk = 0` - **báo lỗi cảm biến** trên UI
- `failStreak % 10 == 0`: **bus recovery** (deinit + reinit I2C)
- Thành công: reset `failStreak = 0`
- Mục đích: phân biệt lỗi tạm thời (nhiễu, glitch) và lỗi kéo dài (mất kết nối, lỗi phần cứng)

---

### 4.5. HAL I2C handle `HW_SENSOR_I2C` - tại sao chỉ có Sensor task mới được dùng trực tiếp?

**Gợi ý:**

- HAL I2C handle **không thread-safe**: `HAL_I2C_Mem_Read()` giữ state nội bộ (hi2c->State, errorCode...)
- Nếu nhiều task gọi HAL_I2C cùng lúc: state machine bị phá vỡ => bus lockup
- Giải pháp: **mutex + single owner pattern** - Sensor task là "bus master", task khác phải qua mutex
- DSP task **không truy cập I2C trực tiếp** - nó nhận data từ queue

---

## 5. RTC DS1307

### 5.1. DS1307 hoạt động thế nào? Register map?

**Gợi ý:**

- RTC (Real-Time Clock) với I2C interface, clock 32.768 kHz
- 8 thanh ghi (0x00-0x07): Giây, Phút, Giờ, Ngày trong tuần, Ngày, Tháng, Năm, Control
- Dữ liệu lưu dạng **BCD** (Binary Coded Decimal)
- Bit CH (Clock Halt) ở register 0x00: bit7 = 1 => oscillator dừng => RTC mất giờ
- Battery backup: có thể chạy khi mất điện (pin CR2032)

---

### 5.2. BCD conversion hoạt động thế nào? Tại sao phải dùng BCD?

**Gợi ý:**

- `bcd2bin()`: Ví dụ 0x25 (BCD) => 25 (binary): `(byte >> 4) * 10 + (byte & 0x0F)`
- `bin2bcd()`: Ví dụ 25 (binary) => 0x25 (BCD): `((value / 10) << 4) | (value % 10)`
- **BCD dễ hiển thị** trên LCD: mỗi nibble = 1 chữ số thập phân
- DS1307 **bắt buộc** BCD - phần cứng lưu trữ theo BCD

---

### 5.3. `DS1307_SetDateTime()` có readback verification - tại sao cần?

**Gợi ý:**

- Sau khi ghi, đọc lại 7 bytes và so sánh
- **Cho phép sai lệch 3 giây** (`READBACK_SEC_TOLERANCE = 3`) vì:
  - Trong lúc ghi-readback, RTC đã chạy tiếp 1-2 giây
  - Giây (seconds) thay đổi liên tục => không thể match chính xác 100%
- Nếu sai > 3 giây: có thể bus lỗi, data bị corrupt => báo lỗi
- Các trường khác (phút, giờ, ngày) phải match chính xác vì ít thay đổi

---

### 5.4. `RtcService_Poll()` và `RtcService_GetSnapshot()` - tại sao phải poll?

**Gợi ý:**

- DS1307 **không có interrupt** để báo "tôi đã cập nhật thời gian mới"
- **Polling** = đọc định kỳ (1 Hz trong sensor task)
- Poll ghi vào buffer `[active ^ 1]`, rồi flip => GUI luôn đọc được snapshot ổn định
- **1 Hz đủ cho RTC** (chỉ cần hiển thị giờ, không cần sub-second accuracy)
- Nếu polling quá nhanh: lãng phí bus I2C. Quá chậm: hiển thị giờ sai

---

### 5.5. `RtcService_RequestSet()` - flow đặt giờ từ GUI hoạt động thế nào?

**Gợi ý:**

```
GUI: RtcService_RequestSet(dateTime) -> copy vào s_setRequest, s_setPending = true
                                      -> (không gọi I2C trực tiếp!)

Sensor Task: RtcService_ProcessPendingSet()
            -> check s_setPending == true?
            -> copy s_setRequest (local copy)
            -> i2cLock -> DS1307_SetDateTime() -> i2cUnlock
            -> s_setGeneration++ (báo hiệu hoàn thành)
            -> s_setPending = false

GUI: RtcService_GetLastSetResult() -> check s_setGeneration thay đổi => hoàn thành
```

- **Decouple GUI và I2C:** GUI không gọi trực tiếp DS1307_SetDateTime (vì GUI task không có I2C permission)
- **An toàn:** copy request trước khi xử lý, nếu GUI gọi SetRequest mới thì request cũ bị overwrite (acceptable)

---

### 5.6. DS1307_Init() kiểm tra gì?

**Gợi ý:**

- `HAL_I2C_IsDeviceReady()` với 3 retries
- Nếu không ready: RTC có thể chưa được solder, hoặc bị lỗi
- `DS1307_IsOscillatorRunning()`: kiểm tra bit CH
  - Nếu CH=1 (oscillator đã dừng): tự động gọi `DS1307_StartOscillator()` để clear CH
- Lưu handle I2C vào driver state

---

## 6. Lưu lịch sử đo (History Store)

### 6.1. `TemporaryHistoryStore` hoạt động thế nào? Cấu trúc dữ liệu?

**Gợi ý:**

- **Circular buffer** cố định 20 records (`TEMP_HISTORY_CAPACITY = 20`)

```c
typedef struct {
    MeasurementHistoryRecord records[20]; // Mảng cố định
    size_t   count;        // Số record hiện có (0..20)
    size_t   writeIndex;   // Vị trí ghi tiếp theo (wrap around)
    uint32_t nextRecordId; // ID tự tăng
    uint32_t overwriteCount; // Số lần bị overwrite (diagnostic)
} TemporaryHistoryStore;
```

- Khi đầy: ghi đè record cũ nhất, `overwriteCount++`
- **Không giải phóng RAM** - mọi thứ static allocation

---

### 6.2. `TemporaryHistory_GetByNewestIndex()` hoạt động thế nào? Công thức?

**Gợi ý:**

- Truy cập theo thứ tự **newest first**: index 0 = mới nhất, index 19 = cũ nhất
- Công thức: `(writeIndex + 2*CAPACITY - 1 - newestIndex) % CAPACITY`
- **Tại sao `+ 2*CAPACITY`?** Để tránh negative number khi `newestIndex > writeIndex`
  - Ví dụ: writeIndex=3, newestIndex=5 => `(3 + 40 - 1 - 5) % 20 = 37 % 20 = 17` (record cũ nhất hợp lệ)

---

### 6.3. Thread safety của History Store - mutex ở đâu?

**Gợi ý:**

- History store **không tự lock** - rely on external mutex
- `s_histMutex` trong `dsp_task.c`:
  - DSP task: `DspTask_HistoryLock()` -> `TemporaryHistory_Add()` -> `DspTask_HistoryUnlock()`
  - GUI task: `DspTask_HistoryLock()` -> `TemporaryHistory_GetByNewestIndex()` -> `DspTask_HistoryUnlock()`
- **Tại sao không lock trong store?** Vì lịch sử hàng đầu (thiết kế), store không biết ai gọi nó

---

### 6.4. `MeasurementHistoryRecord` chứa những gì? Tại sao cần nhiều field?

**Gợi ý:**
| Nhóm | Fields |
|------|--------|
| Thời gian | `startDateTime`, `endDateTime`, `durationMs` |
| BPM | `averageBpm`, `minimumBpm`, `maximumBpm` |
| SpO2 | `averageSpo2`, `minimumSpo2`, `maximumSpo2` |
| Chất lượng | `averageSqi` (Signal Quality Index) |
| Peak stats | `acceptedPeakCount`, `rejectedPeakCount` |
| Diagnostic | `droppedSampleCount`, `fifoOverflowCount` |
| Validity | `bpmValid`, `spo2Valid`, `MeasurementResultStatus` |

- **Minimum/Maximum:** Cần cho y tế - bác sĩ cần biết BPM dao động thế nào
- **SQI:** Đánh giá chất lượng tín hiệu - SQI thấp = kết quả không đáng tin
- **Rejected peaks:** Số peak bị loại (do filter, do amplitude không đạt) => chỉ số chất lượng
- **Dropped samples & FIFO overflow:** Diagnostic - số mẫu bị mất => độ tin cậy

---

### 6.5. `handleFinalize()` trong DSP task - flow lưu lịch sử?

**Gợi ý:**

1. DSP state machine chuyển sang `RESULT_READY`
2. `handleFinalize()` được gọi
3. Lock history mutex
4. Tạo `MeasurementHistoryRecord` từ kết quả đo hiện tại (BPM stats, SpO2 stats, thời gian...)
5. Gọi `TemporaryHistory_Add(record)`
6. Unlock history mutex
7. Publish kết quả cho GUI và telemetry

---

### 6.6. Tại sao dung lượng chỉ 20 records? Nếu cần nhiều hơn thì sao?

**Gợi ý:**

- **RAM limited:** STM32F429 có 256 KB SRAM, mỗi record ~80-100 bytes => 20 records ≈ 2 KB
- TouchGFX framebuffer đã chiếm ~150 KB (240x320x2 bytes), SDRAM 2MB cho GUI
- 20 records ≈ 20 phép đo gần nhất, đủ cho hiển thị "lịch sử đo" trên UI
- Nếu cần hơn: mở rộng RAM bằng SDRAM, hoặc lưu ra EEPROM/Flash
- `overwriteCount` cho biết có bao nhiêu record bị mất - useful cho debugging

---

## 7. Câu hỏi tổng hợp / liên kết

### 7.1. Trình bày toàn bộ luồng dữ liệu từ cảm biến đến hiển thị.

**Gợi ý:**

```
MAX30102 (100 Hz) -> FIFO -> Sensor Task (50 Hz)
    -> [SPSC Queue lock-free] -> DSP Task (100 Hz)
        -> PPG Engine: filter -> peak detect -> BPM/SpO2
        -> [Seqlock] -> GUI Task -> LCD
        -> [CMSIS Queue] -> Telemetry Task -> UART 921600 -> PC
```

---

### 7.2. Hệ thống có thể bị deadlock không? Phân tích.

**Gợi ý:**

- **Không có deadlock** trong hệ thống hiện tại vì:
  1. Chỉ có 2 mutex: `s_i2cMutex` và `s_histMutex`
  2. Không có nested locking (không lock mutex này khi đang giữ mutex khác)
  3. Lock ordering nhất quán: DSP task luôn lock `s_histMutex` riêng, Sensor task luôn lock `s_i2cMutex` riêng
  4. Lock duration ngắn (I2C transaction ~1ms, history add ~0.1ms)
- **Tuy nhiên**, nếu将来 mở rộng: cần cảnh báo deadlock nếu lock order không nhất quán

---

### 7.3. Nếu Sensor task bị chậm (blocked quá lâu), hệ thống ảnh hưởng thế nào?

**Gợi ý:**

1. FIFO sensor bị đầy => MAX30102 overflow => mất sample
2. RTC không được poll => đồng hồ hiển thị sai
3. SPSC queue không có data mới => DSP task chạy nhưng không có sample mới
4. DSP task có thể detect "no finger" nếu queue trống quá lâu
5. Giải pháp: I2C timeout 100ms, `failStreak` fault tolerance

---

### 7.4. So sánh các cơ chế đồng bộ trong hệ thống: Khi nào dùng gì?

**Gợi ý:**

| Cơ chế                | Khi nào dùng                                          | Ưu điểm                           | Nhược điểm                     |
| --------------------- | ----------------------------------------------------- | --------------------------------- | ------------------------------ |
| **Mutex**             | Nhiều reader/writer share resource (I2C bus, history) | Đơn giản, an toàn                 | Context switch overhead        |
| **SPSC Ring Buffer**  | 1 producer, 1 consumer, high throughput               | Lock-free, latency thấp           | Chỉ 1P1C                       |
| **Seqlock**           | 1 writer, nhiều reader, data nhỏ                      | Lock-free reader, writer priority | Reader có thể retry            |
| **Double Buffer**     | Snapshot pattern, 1 writer                            | Lock-free reader, consistent read | Copy data twice                |
| **Volatile sentinel** | Simple request/response 1 chiều                       | Zero overhead                     | Chỉ dùng cho uint32_t đơn giản |
| **Message Queue**     | Multi-producer, 1 consumer, priority                  | FreeRTOS built-in, priority       | Heap usage                     |

---

### 7.5. Nếu thêm cảm biến mới (ví dụ: ADC nhiệt độ) share I2C3, cần thay đổi gì?

**Gợi ý:**

1. Thêm driver mới với I2C address riêng
2. **Không cần thay đổi mutex** - `s_i2cMutex` đã bao quát tất cả I2C3 traffic
3. Sensor task thêm `i2cLock -> ADC_ReadTemp -> i2cUnlock` trong loop
4. Cân nhắc: thêm data vào queue hoặc publish trực tiếp
5. Kiểm tra: bus bandwidth đủ không? (100 kHz shared giữa 3-4 thiết bị)

---

### 7.6. Đánh giá hiệu năng: memory usage, CPU time, latency.

**Gợi ý:**

**Memory:**
| Component | RAM |
|-----------|-----|
| defaultTask stack | 512 bytes |
| GUI_Task stack | 32,768 bytes |
| dspTask stack | 4,096 bytes |
| telemetryTask stack | 2,048 bytes |
| SPSC ring buffer | 256 × 16 = 4,096 bytes |
| Event queue (telemetry) | 24 × sizeof(TelemetryMessage) |
| Waveform queue (telemetry) | 64 × sizeof(TelemetryMessage) |
| History store | 20 × ~100 bytes = 2,000 bytes |
| **Total app** | **~50 KB** (trong 64 KB heap + SDRAM) |

**CPU:** DSP task nặng nhất (median filter O(N log N), Butterworth, peak detection). Cortex-M4F @ 180 MHz đủ.

**Latency:** Sensor -> LCD: ~30-50ms (20ms sensor + 10ms DSP + GUI render time)

---

### 7.7. Tại sao dùng FreeRTOS thay vì Super Loop (bare-metal)?

**Gợi ý:**
| Tiêu chí | Super Loop | FreeRTOS |
|----------|------------|----------|
| Đơn giản | ✅ Đơn giản hơn | ❌ Phức tạp hơn |
| Real-time | ❌ Timing phụ thuộc vào loop length | ✅ Time-slicing, priority |
| Debug | ✅ Dễ trace | ❌ Khó debug context switch |
| Nhiều task | ❌ Phải tự quản lý timing | ✅ OS quản lý |
| Memory | ✅ Ít overhead | ❌ ~10 KB kernel overhead |

- **Dự án này cần FreeRTOS** vì: 4 task chạy đồng thời, cần timing chính xác (PPG 100 Hz), cần queue/sp sync giữa DSP và Sensor

---

### 7.8. Nếu muốn thêm chức năng "alarm-clock" (báo thức), cần tích hợp thế nào với RTC?

**Gợi ý:**

- DS1307 **không có alarm register** (khác DS3231)
- Giải pháp: **Software alarm** trong Sensor Task
  - Mỗi lần `RtcService_Poll()` (1 Hz): so sánh `DateTime.now` với `alarmTime`
  - Nếu match => set flag -> trigger buzzer via `AlertBuzzer_Process()`
- Hoặc: dùng DS1307 SQW output (register 0x07) với external interrupt, nhưng phức tạp hơn

---

## BẢNG TÓM TẮT - GỢI Ý TRẢ LỜI NHANH

| Câu hỏi            | Trả lời ngắn                                                    |
| ------------------ | --------------------------------------------------------------- |
| Bao nhiêu task?    | 4: Sensor, GUI, DSP, Telemetry                                  |
| Tại sao lock-free? | SPSC queue cho sensor->DSP: không cần mutex vì 1P1C             |
| Mutex cho gì?      | I2C bus sharing + history store read/write                      |
| DS1307 format?     | BCD, 7 bytes, 8 registers                                       |
| Queue capacity?    | SPSC: 256 samples, Event: 24, Waveform: 64, History: 20 records |
| Fault tolerance?   | failStreak < 5: tolerate, >= 5: error flag, %10: bus recovery   |
| seqlock?           | DSP writes, GUI reads. Gen counter odd=writing, even=stable     |
| Tick rate?         | 1000 Hz = 1ms resolution                                        |

---

_M File generated for report preparation. Good luck!_
