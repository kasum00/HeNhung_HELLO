/**
 * @file    app_init.c
 * @brief   Cài đặt khởi tạo ứng dụng + sensor/RTC task (target).
 * @note    User-owned. Chủ sở hữu duy nhất của hi2c3.
 *
 * ==========================================================================
 *  GIẢI THÍCH TỔNG QUAN CHO SINH VIÊN:
 * ==========================================================================
 *
 *  File này chứa HAI chức năng chính:
 *  1. App_Init():              Khởi tạo TẤT CẢ peripheral trước khi RTOS scheduler chạy.
 *  2. App_DefaultTask():       Vòng lặp vô hạn - đọc cảm biến, xử lý RTC, điều khiển buzzer.
 *
 *  KIẾN TRÚC TASK:
 *    - App_DefaultTask chạy trong RTOS thread (FreeRTOS/CMSIS-RTOS2).
 *    - Nó đọc FIFO từ cảm biến MAX30102 qua bus I2C3.
 *    - Push mẫu thô vào PpgSampleQueue (lock-free, giải thích ở ppg_sample_queue.c).
 *    - DSP Task (tách riêng) Pop từ queue và chạy thuật toán đo BPM/SpO2.
 *
 *  I2C3 BUS CHUNG:
 *    STM32F4 chỉ có 3 bus I2C. Trong dự án này, I2C3 được DÙNG CHUNG giữa:
 *      - MAX30102 (cảm biến nhịp tim SpO2) - địa chỉ 0xAE/0xAF
 *      - DS1307   (đồng hồ thời gian thực RTC) - địa chỉ 0xD0/0xD1
 *    Vì CẢ HAI thiết bị dùng chung 1 bus I2C3 (chân PA8/PC9),
 *    cần MUTEX để đảm bảo hai thiết bị không bị giao tiếp cùng lúc.
 *    (Xem i2cLock/i2cUnlock bên dưới.)
 * ==========================================================================
 */

#include "app_init.h"
#include "hw_config.h"
#include "buzzer_melodies.h"
#include "max30102_driver.h"
#include "rtc_service.h"
#include "ppg_sample_queue.h"
#include "dsp_task.h"
#include "status_led_driver.h"
#include "physical_input_service.h"
#include "medical_alert_service.h"
#include "alert_led_pattern.h"
#include "alert_buzzer.h"
#include "telemetry_service.h"
#include "cmsis_os2.h"

/* --------------------------------------------------------------------------
 *  BIẾN TOÀN CỰC
 * --------------------------------------------------------------------------
 *  HW_SENSOR_I2C: handle I2C3 từ HAL. Các driver cảm biến (MAX30102, DS1307)
 *  đều dùng handle này để giao tiếp.
 *
 *  HAL HANDLE RULE: Chỉ App_DefaultTask được quyền sử dụng HW_SENSOR_I2C,
 *  không task nào khác được gọi HAL I2C trực tiếp. Mutex bảo vệ truy cập.
 * -------------------------------------------------------------------------- */
extern I2C_HandleTypeDef HW_SENSOR_I2C;

volatile int g_sensorOk = 0;            /* 1 = cảm biến hoạt động OK, 0 = lỗi */
volatile uint8_t g_max30102PartId = 0U; /* Part ID đọc lúc init (mong đợi 0x15) */
volatile uint32_t g_fifoOverflowTotal = 0U; /* Tổng số lần FIFO bị tràn */

/* --------------------------------------------------------------------------
 *  I2C MUTEX - BẢO VỆ BUS I2C3 DÙNG CHUNG
 * --------------------------------------------------------------------------
 *  VÌ SAO CẦN MUTEX CHO I2C?
 *  - I2C là bus serial: chỉ MỘT thiết bị được nói chuyện tại một thời điểm.
 *  - Nếu MAX30102 đang bị đọc FIFO mà DS1307 cùng lúc được poll时间,
 *    bus sẽ bị CONFLICT: hai thiết bị cùng kéo SDA/SCL => dữ liệu hỗn loạn.
 *
 *  MUTEX LÀ GÌ?
 *  - Mutual Exclusion (loại trừ lẫn nhau): như một "chìa khóa nhà vệ sinh".
 *    Task nào lấy được mutex trước thì được quyền dùng bus I2C.
 *    Task khác muốn dùng phải ĐỢI (blocking) cho đến khi task trước trả mutex.
 *
 *  CMSIS-RTOS2 osMutexAcquire(osWaitForever): nếu mutex đang bị giữ,
 *    task GỌI sẽ bị BLOCK (đứng chờ) cho đến khi mutex được giải phóng.
 *    osWaitForever = đợi vô hạn (không timeout).
 *
 *  s_i2cMutex = NULL ban đầu. Nó được tạo TRONG App_DefaultTask (sau khi
 *  RTOS scheduler đã chạy) vì osMutexNew() cần scheduler đang hoạt động.
 *  Trước scheduler (App_Init), chưa có task nào cạnh tranh nên không cần mutex.
 * --------------------------------------------------------------------------
 */
static osMutexId_t s_i2cMutex = NULL;

/* --------------------------------------------------------------------------
 *  DS1307 RTC - KIỂM TRA SỰ CÓ MẶT
 * --------------------------------------------------------------------------
 *  DS1307 là chip đồng hồ thời gian thực (RTC). Khi boot, ta thử đọc DS1307.
 *  Nếu DS1307 PHẢN HỒI đúng (RTC_STATUS_OK), s_rtcPresent = true.
 *
 *  VÌ SAO QUAN TRỌNG?
 *  - Nếu DS1307 KHÔNG CÓ trên bus (board không hàn chip RTC),
 *    poll DS1307 mỗi giây sẽ GỬI LỆNH I2C vô ích => chiếm bus I2C3,
 *    gây chậm trễ việc đọc MAX30102.
 *  - s_rtcPresent = false => BỎ qua hoàn toàn việc poll RTC, giữ bus I2C
 *    sạch cho MAX30102 (thiết bị quan trọng nhất).
 * --------------------------------------------------------------------------
 */
static bool s_rtcPresent = false;

/**
 * @brief  Khóa bus I2C3 (lấy mutex, block nếu đang bị giữ).
 *
 *  Wrapper mỏng: kiểm tra mutex != NULL trước vì osMutexAcquire()
 *  sẽ fault nếu truyền NULL. Kiểm tra NULL bảo vệ an toàn cho giai đoạn
 *  TRƯỚC scheduler (khi mutex chưa được tạo).
 *
 *  MẪU CODE THƯỜNG DÙNG trong embedded RTOS:
 *    i2cLock();           // Lấy mutex - block nếu bus đang bận
 *    // ... thao tác I2C an toàn ...   (MAX30102 hoặc DS1307)
 *    i2cUnlock();         // Trả mutex - cho task khác dùng
 */
static void i2cLock(void)
{
    if (s_i2cMutex != NULL) { (void)osMutexAcquire(s_i2cMutex, osWaitForever); }
}

/**
 * @brief  Mở khóa bus I2C3 (trả mutex).
 * @note   (void) casting: osMutexRelease trả osStatus_t nhưng ta bỏ qua.
 *         Trong thực tế nếu mutex đã bị release nhầm, giá trị trả về
 *         sẽ là osErrorParameter nhưng ta chấp nhận rủi ro này.
 */
static void i2cUnlock(void)
{
    if (s_i2cMutex != NULL) { (void)osMutexRelease(s_i2cMutex); }
}

/**
 * @brief  Phục hồi bus I2C khi bị treo/kẹt (bus stuck condition).
 *
 * ==========================================================================
 *  KHI NÀO BUS BỊ KẸT (I2C BUS STUCK)?
 * ==========================================================================
 *  Bus I2C gồm 2 dây: SDA (dữ liệu) và SCL (xung clock).
 *  Bus bị "kẹt" khi MỘT trong hai dây bị kéo THẤP vĩnh viễn:
 *
 *  NGUYÊN NHÂN PHỔ BIẾN:
 *  1. Slave设备 (MAX30102) bị reset giữa chừng khi đang truyền dữ liệu.
 *     -> SDA bị kéo thấp vĩnh viễn (slave giữ SDA trong khi master chờ ACK).
 *  2. Điện áp không ổn định, nhiễu EMI làm mất đồng hồ.
 *  3. Software bug: đọc quá nhiều byte mà không gửi STOP condition.
 *
 *  DẤU HIỆU: HAL_I2C_Master_Transmit/Receive trả về HAL_TIMEOUT hoặc HAL_ERROR.
 *
 * ==========================================================================
 *  CÁCH PHỤC HỒI (3 bước):
 * ==========================================================================
 *  BƯỚC 1 - HAL_I2C_DeInit(): "Tắt" peripheral I2C3 trong STM32.
 *    - Giải phóng các chân GPIO (PA8, PC9) về trạng thái GPIO thô.
 *    - Xóa tất cả cờ trạng thái (SR1, SR2), xóa các bit trong CR1/CR2.
 *    - Lúc này bus được "thả tự do".
 *
 *  BƯỚC 2 - HAL_I2C_Init(): "Bật lại" peripheral I2C3.
 *    - Cấu hình lại clock, address, speed từ tham số đã lưu.
 *    - Bắt đầu lại từ trạng thái sạch (clean slate).
 *
 *  BƯỚC 3 - MAX30102_Init(): Cấu hình lại cảm biến MAX30102.
 *    - Gửi lại các thanh ghi cấu hình (mode, FIFO, ngưỡng ngắt).
 *    - Quan trọng: MAX30102_Init cũng XÓA FIFO nội bộ, đảm bảo dữ liệu
 *      cũ/giả bị loại bỏ.
 *
 *  LƯU Ý: Caller PHẢI đang giữ i2cMutex khi gọi hàm này,
 *          vì DeInit/Init có thể mất thời gian và không được phép
 *          bị ngắt bởi task khác.
 * --------------------------------------------------------------------------
 */
static void i2cRecoverBus(void)
{
    (void)HAL_I2C_DeInit(&HW_SENSOR_I2C);  /* Bước 1: tắt peripheral I2C3 */
    (void)HAL_I2C_Init(&HW_SENSOR_I2C);    /* Bước 2: khởi động lại I2C3 */
    (void)MAX30102_Init(&HW_SENSOR_I2C);   /* Bước 3: cấu hình lại MAX30102,
                                            *         xóa FIFO cũ, khôi phục mode */
}

/**
 * @brief  Khởi tạo TẤT CẢ peripheral和服务 trước khi RTOS scheduler chạy.
 *
 * ==========================================================================
 *  THỨ TỰ KHỞI TẠI:
 * ==========================================================================
 *  1. Buzzer           -> Chuẩn bị PWM cho loa蜂鸣器 (trước LED để sẵn sàng alarm)
 *  2. Status LED       -> Đèn trạng thái: tắt đèn cảnh báo khi boot
 *  3. Alert LED        -> Mẫu nháy về trạng thái nghỉ (không nháy)
 *  4. Alert Buzzer     -> Còi cảnh báo sẵn sàng (cùng vòng đời với đèn)
 *  5. Medical Alert    -> Ngưỡng cảnh báo y tế (BPM/SpO2 từ alert_config.h)
 *  6. Physical Input   -> Nút B1: cấu hình EXTI0 ngắt ngoài
 *  7. Telemetry        -> Service遥测: bộ đếm + cấu hình (task tạo sau scheduler)
 *  8. MAX30102 sensor  -> Cảm biến nhịp tim SpO2 trên I2C3 (chưa có mutex yet)
 *  9. DS1307 RTC       -> Đồng hồ thời gian thực trên I2C3 (cùng bus)
 * 10. PPG Queue        -> Đặt lại hàng đợi (head = tail = 0 => rỗng)
 *
 *  LƯU Ý: Hàm này chạy TRƯỚC KHI scheduler khởi động.
 *  Lúc này chỉ có MỘT luồng thực thi (main thread) nên KHÔNG CÓ xung đột
 *  bus I2C, KHÔNG CẦN mutex. Mutex chỉ cần sau khi scheduler chạy và
 *  App_DefaultTask bắt đầu poll song song.
 * ==========================================================================
 */
void App_Init(void)
{
    /* --- BƯỚC 1: Buzzer --- */
    (void)Buzzer_Init();                /* Khởi tạo PWM cho buzzer蜂鸣器 */

    /* --- BƯỚC 2-4: LED và còi cảnh báo --- */
    StatusLed_Init();                   /* LED trạng thái (chờ: xanh, lỗi: đỏ) */
    /* LED cảnh báo tắt hẳn lúc boot, mẫu nháy về trạng thái nghỉ. */
    AlertLed_Init();                    /* LED nháy cảnh báo (sẽ sáng khi BPM/SpO2 nguy hiểm) */
    AlertBuzzer_Init();                 /* còi cảnh báo, cùng vòng đời với đèn */
    /* NULL => dùng ngưỡng mặc định từ alert_config.h
     * (ví dụ: SpO2 < 90% hoặc BPM > 120 hoặc BPM < 40) */
    MedicalAlert_Init(NULL);

    /* --- BƯỚC 5: Nút vật lý --- */
    /* Nút B1: chân + sườn do CubeMX cấu hình, hàm này bật NVIC EXTI0. */
    PhysicalInput_Init();

    /* --- BƯỚC 6: Telemetry --- */
    /* Telemetry: cấu hình + bộ đếm (hàng đợi/task tạo sau khi scheduler chạy). */
    Telemetry_Init();

    /* --- BƯỚC 7: Khởi tạo cảm biến MAX30102 trên I2C3 --- */
    /* Lúc này CHƯA CÓ scheduler => chỉ có main thread => KHÔNG CẦN mutex.
     * Bus I2C3 hoàn toàn độc quyền, an toàn cho MAX30102_Init(). */
    const Max30102Status ms = MAX30102_Init(&HW_SENSOR_I2C);
    (void)MAX30102_ReadPartId((uint8_t*)&g_max30102PartId);
    /* g_sensorOk = 1 nếu MAX30102 phản hồi OK, 0 nếu lỗi wiring/I2C. */
    g_sensorOk = (ms == MAX30102_OK) ? 1 : 0;

    /* --- BƯỚC 8: Khởi tạo RTC DS1307 trên cùng bus I2C3 --- */
    /* DS1307 có thể KHÔNG CÓ trên board. Nếu Init trả OK => RTC có mặt.
     * Sau đó App_DefaultTask sẽ poll RTC mỗi giây.
     * Nếu DS1307 không có => s_rtcPresent = false => bỏ qua RTC hoàn toàn. */
    const RtcStatus rs = RtcService_Init(&HW_SENSOR_I2C);
    s_rtcPresent = (rs == RTC_STATUS_OK);

    /* --- BƯỚC 9: Reset queue mẫu PPG --- */
    /* Đặt head = tail = 0 => queue rỗng, sẵn sàng cho sensor task Push(). */
    PpgQueue_Reset();
}

/**
 * @brief  Vòng lặp vô hạn của task sensor + RTC + buzzer.
 *
 * ==========================================================================
 *  HÀM NÀY CHẠY TRONG RTOS THREAD - KHÔNG BAO GIỜ TRẢ VỀ!
 * ==========================================================================
 *  Cấu trúc chung:
 *    1. Khởi tạo mutex I2C (lần đầu tiên gọi)
 *    2. Bắt đầu telemetry + DSP tasks
 *    3. Phát nhạc khởi động
 *    4. Vòng lặp vô hạn: đọc cảm biến -> push queue -> kiểm tra lỗi -> poll RTC
 * ==========================================================================
 */
void App_DefaultTask(void)
{
    /* --------------------------------------------------------------------------
     *  GIAI ĐOẠN 1: KHỞI TẠO (chỉ chạy MỘT LẦN)
     * --------------------------------------------------------------------------
     */
    /* Tạo mutex I2C lần đầu. osMutexNew() cần RTOS scheduler đang chạy.
     * Sau lần tạo, s_i2cMutex != NULL nên lần gọi sau sẽ skip qua. */
    if (s_i2cMutex == NULL)
    {
        s_i2cMutex = osMutexNew(NULL);
    }

    /* BẮT ĐẦU CÁC TASK CON:
     * Telemetry_Start() TRƯỚC DspTask_Start() vì:
     * - Telemetry task quản lý hàng đợi遥测 và publish thống kê.
     * - DSP task sẽ bắt đầu Pop() từ sample queue ngay khi chạy.
     * - Telemetry phải sẵn sàng TRƯỚC để không mất dữ liệu ban đầu. */
    Telemetry_Start();
    (void)Telemetry_PublishSystem(0U);   /* 0U = SYSTEM_BOOT: thông báo hệ thống khởi động */

    /* Khởi động DSP thread khi scheduler đã chạy (engine không được chạy trong
       GUI/TouchGFX tick). Nó rút sample queue mà ta đổ vào.
       DSP task chạy thuật toán tính BPM và SpO2 từ các mẫu PPG thô. */
    DspTask_Start();

    /* Chuông báo bật nguồn (non-blocking): Buzzer_PlayMelody() chỉ load dữ liệu
     * melod vào buffer, rồi Buzzer_Process() (gọi mỗi vòng lặp) sẽ phát dần. */
    (void)Buzzer_PlayMelody(BUZZER_MELODY_STARTUP, BUZZER_MELODY_STARTUP_LEN);

    /* --------------------------------------------------------------------------
     *  GIAI ĐOẠN 2: VÒNG LẶP VÔ HẠN
     * --------------------------------------------------------------------------
     *  seq:          Số thứ tự tăng dần cho mỗi mẫu PPG (0, 1, 2, 3, ...).
     *                Giúp DSP task kiểm tra thứ tự mẫu và phát hiện mẫu bị mất.
     *
     *  rtcDivider:   Bộ đếm vòng lặp để poll RTC ~1 Hz.
     *                Mỗi vòng lặp = MAX30102_POLL_PERIOD_MS = 20ms.
     *                rtcEvery = 1000/20 = 50 => poll RTC mỗi 50 vòng lặp.
     *
     *  failStreak:   Đếm số lần đọc MAX30102 liên tiếp thất bại.
     *                < 5: dung thứ (tolerate) - lỗi lẻ tẻ không làm mất phép đo.
     *                >= 5: báo lỗi cảm biến (g_sensorOk = 0).
     *                Mỗi 10 lần: thử phục hồi bus (i2cRecoverBus).
     * -------------------------------------------------------------------------- */
    uint32_t seq = 0U;                  /* số thứ tự mẫu, tăng mỗi Push */
    uint32_t rtcDivider = 0U;           /* bộ đếm cho poll RTC */
    uint32_t failStreak = 0U;           /* số lần đọc lỗi liên tiếp */
    const uint32_t rtcEvery = 1000U / MAX30102_POLL_PERIOD_MS;  /* ~1 Hz: 1000/20 = 50 */

    for (;;)
    {
        /* ==================================================================
         *  BƯỚC 1: BUZZER_PROCESS - Phát nhạc蜂鸣器
         * ==================================================================
         *  Buzzer_Process() phải được gọi LIÊN TỤC mỗi vòng lặp.
         *  Nó đọc buffer melod và toggles GPIO để tạo sóng âm.
         *  Nếu không gọi liên tục => nhạc bị gián đoạn / chậm nhịp. */
        Buzzer_Process();

        /* ==================================================================
         *  BƯỚC 2: ALERT LED + ALERT BUZZER
         * ==================================================================
         *  Đèn cảnh báo VÀ còi cảnh báo chạy Ở ĐÂY (không phải trong DspTask):
         *  thuật toán BPM/SpO2 không được chạm GPIO/buzzer. Cả hai đọc CÙNG một cờ
         *  MedicalAlert_IsActive() lấy một lần, nên chung một vòng đời cảnh báo —
         *  cùng bắt đầu, cùng dừng.
         *
         *  MedicalAlert_IsActive(): trả về true nếu BPM hoặc SpO2 nằm ngoài
         *  ngưỡng an toàn (do DspTask cập nhật).
         *
         *  AlertLed_Process(): điều khiển mẫu nháy LED (ví dụ: nháy nhanh = nguy hiểm).
         *  AlertBuzzer_Process(): điều khiển còi蜂鸣器 (ví dụ: beep liên tục).
         *
         *  HAL_GetTick(): số milliseconds kể từ khi boot, dùng cho timing nháy LED.
         * ================================================================== */
        const bool alertActive = MedicalAlert_IsActive();
        AlertLed_Process(alertActive, HAL_GetTick());     /* LED nháy theo mẫu */
        AlertBuzzer_Process(alertActive);                 /* Còi蜂鸣器 theo trạng thái */

        /* ==================================================================
         *  BƯỚC 3: ĐỌC FIFO MAX30102 VÀ PUSH VÀO QUEUE
         * ==================================================================
         *  Đây là công việc CHÍNH của task này: đọc dữ liệu thô từ cảm biến
         *  và đưa vào PpgSampleQueue cho DSP task xử lý.
         *
         *  samples[16]: buffer tĩnh chứa tối đa 16 mẫu một lần đọc.
         *  (static để không allocate stack mỗi vòng lặp - quan trọng trên MCU
         *  có stack hạn chế, thường 4-8 KB cho mỗi RTOS task.)
         *
         *  QUY TRÌNH:
         *  1. i2cLock()   -> Lấy mutex I2C (đảm bảo DS1307 không nói chuyện cùng lúc)
         *  2. ReadFifo()  -> Đọc tối đa 16 mẫu từ FIFO của MAX30102
         *  3. ReadOverflowCounter() -> Đếm số lần FIFO bị tràn (không đủ bộ nhớ)
         *  4. i2cUnlock() -> Trả mutex, cho phép task khác dùng I2C
         * ================================================================== */
        static Max30102Sample samples[16]; /* buffer tĩnh, không allocate trên stack */
        uint8_t got = 0U;                  /* số mẫu thực sự đọc được (0-16) */
        uint8_t overflow = 0U;             /* số mẫu bị tràn FIFO (bị mất) */
        i2cLock();                         /* Lấy mutex I2C - block nếu bus đang bận */
        const Max30102Status ms = MAX30102_ReadFifo(samples, 16U, &got);
        (void)MAX30102_ReadOverflowCounter(&overflow);
        i2cUnlock();                       /* Trả mutex I2C */

        if (ms == MAX30102_OK)
        {
            /* --- ĐỌC THÀNH CÔNG --- */
            failStreak = 0U;               /* Reset bộ đếm lỗi: đây là lần đọc OK */
            g_sensorOk = 1;                /* Cảm biến hoạt động bình thường */
            if (overflow > 0U) { g_fifoOverflowTotal += overflow; }
            /* Lấy thời gian hiện tại (ms từ boot) cho tất cả mẫu trong batch này.
             * Tất cả mẫu trong 1 batch có chung timestamp vì chúng được đọc
             * cùng một lúc trong ReadFifo(). */
            const uint32_t now = HAL_GetTick();
            /* Push từng mẫu thô vào queue cho DSP task.
             * (void)cast: bỏ qua giá trị trả về vì queue có thể đầy
             * trong trường hợp DSP xử lý chậm - chấp nhận drop. */
            for (uint8_t i = 0U; i < got; ++i)
            {
                PpgRawSample rs;
                rs.sequence = seq++;      /* số thứ tự tăng dần, giúp DSP kiểm tra order */
                rs.timestampMs = now;     /* thời gian đọc (ms) */
                rs.redRaw = samples[i].red;   /* giá trị LED đỏ thô (ADC counts) */
                rs.irRaw = samples[i].ir;     /* giá trị LED hồng ngoại thô (ADC counts) */
                (void)PpgQueue_Push(&rs);     /* đẩy vào queue (có thể drop nếu đầy) */
            }
        }
        else
        {
            /* ==================================================================
             *  XỬ LÝ LỖI: CHIẾN THUẬT DUNG THỨ (FAULT TOLERANCE)
             * ==================================================================
             *
             *  TRÊN THỰC TẾ: Bus I2C thi thoảng bị lỗi do nhiễu điện từ (EMI),
             *  tiếp xúc kém, hoặc timing race. Nếu mỗi lần lỗi đều báo "SENSOR ERROR"
             *  thì hệ thống sẽ thường xuyên hiển thị lỗi gây hoang mang người dùng.
             *
             *  GIẢI PHÁP: Dung thứ lỗi lẻ tẻ (tolerant to transient faults).
             *
             *  failStreak < HW_SENSOR_FAULT_STREAK (5):
             *    Tolerate: một vài lần đọc lỗi liên tiếp không đáng lo.
             *    Ví dụ: lần 23 đọc OK, lần 24 lỗi => failStreak = 1 => không báo lỗi.
             *    Người dùng KHÔNG thấy thông báo lỗi, phép đo vẫn tiếp tục.
             *
             *  failStreak >= HW_SENSOR_FAULT_STREAK (5):
             *    SENSOR ERROR: đọc lỗi 5 lần liên tiếp => cảm biến có vấn đề thực sự.
             *    g_sensorOk = 0 => bridge (GUI) hiển thị "Lỗi cảm biến".
             *
             *  failStreak % HW_I2C_RECOVER_STREAK (10) == 0:
             *    Mỗi 10 lần lỗi liên tiếp => thử phục hồi bus (i2cRecoverBus).
             *    Lần 10: failStreak = 10 => DeInit + ReInit + MAX30102 reconfigure.
             *    Lần 20: failStreak = 20 => thử lại lần nữa.
             *    Nếu bus vẫn kẹt sau Recovery => tiếp tục lỗi và thử lại mỗi 10 lần.
             *
             *  TẠI SAO KHÔNG PHỤC HỒI NGAY LẦN ĐẦU TIÊN?
             *    DeInit/Init mất ~5-10ms, trong khi một lần đọc lỗi chỉ mất 1ms.
             *    Nếu lỗi là ngẫu nhiên (transient) => lần đọc tiếp theo thường sẽ OK.
             *    Phục hồi quá sớm => lãng phí thời gian và có thể gây gián đoạn bus.
             * ================================================================== */
            ++failStreak;
            if (failStreak >= HW_SENSOR_FAULT_STREAK)    /* >= 5 lần liên tiếp */
            {
                g_sensorOk = 0;              /* Báo lỗi cảm biến cho bridge/GUI */
            }
            if ((failStreak % HW_I2C_RECOVER_STREAK) == 0U) /* mỗi 10 lần */
            {
                /* Thử phục hồi bus I2C: DeInit -> ReInit -> MAX30102 reconfigure */
                i2cLock();
                i2cRecoverBus();
                i2cUnlock();
            }
        }

        /* ==================================================================
         *  BƯỚC 4: POLL RTC DS1307 (~1 Hz)
         * ==================================================================
         *  Chỉ poll RTC nếu:
         *  1. DS1307 có mặt trên board (s_rtcPresent == true).
         *     Nếu DS1307 không có => skip hoàn toàn để tránh nhiễu bus I2C.
         *
         *  2.rtcDivider >= rtcEvery (đã đủ 50 vòng lặp = 1 giây).
         *     rtcEvery = 1000ms / 20ms = 50 => ~1 Hz poll rate.
         *
         *  HAI CÔNG VIỆC TRONG MỘT LẦN POLL RTC:
         *  - ProcessPendingSet(): Kiểm tra xem có yêu cầu cài đặt giờ
         *    đang chờ (từ bridge/GUI) không. Nếu có => gửi lệnh ghi giờ
         *    xuống DS1307.
         *  - Poll(): Đọc thời gian hiện tại từ DS1307 và cập nhật
         *    thời gian công bố (broadcast) cho các thành phần khác.
         *
         *  LƯU Ý: rtcDivider đặt lại = 0 sau mỗi lần poll,
         *          đảm bảo mỗi lần poll cách nhau đúng 50 vòng lặp.
         * ================================================================== */
        if (s_rtcPresent && (++rtcDivider >= rtcEvery))
        {
            rtcDivider = 0U;               /* Reset bộ đếm cho chu kỳ tiếp theo */
            i2cLock();                     /* Lấy mutex I2C (DS1307 cần bus chung) */
            RtcService_ProcessPendingSet(); /* Xử lý yêu cầu cài giờ đang chờ */
            RtcService_Poll();              /* Đọc thời gian từ DS1307 */
            i2cUnlock();                   /* Trả mutex I2C */
        }

        /* ==================================================================
         *  BƯỚC 5: DELAY 20ms
         * ==================================================================
         *  osDelay(20): nhường CPU cho các task khác trong 20ms.
         *  - Buzzer_Process(): cập nhật GPIO蜂鸣器
         *  - TouchGFX GUI task: cập nhật màn hình
         *  - Telemetry task: gửi dữ liệu遥测
         *  - DSP task: xử lý thuật toán BPM/SpO2
         *
         *  20ms = 50 Hz => đọc MAX30102 với tần số ~50 Hz.
         *  (MAX30102 sample rate thường đặt 100 Hz, FIFO sẽ tích lũy
         *  ~2 mẫu mỗi lần ReadFifo().)
         * ================================================================== */
        osDelay(MAX30102_POLL_PERIOD_MS);  /* 20ms delay, nhường CPU cho task khác */
    }
}
