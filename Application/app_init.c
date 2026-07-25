/**
 * @file    app_init.c
 * @brief   Cài đặt khởi tạo ứng dụng + sensor/RTC task (target).
 * @note    User-owned. Chủ sở hữu duy nhất của hi2c3.
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

/* Bus I2C cảm biến dùng chung (HAL HANDLE RULE: lớp App là chủ duy nhất). */
extern I2C_HandleTypeDef HW_SENSOR_I2C;

volatile int g_sensorOk = 0;
volatile uint8_t g_max30102PartId = 0U;
volatile uint32_t g_fifoOverflowTotal = 0U;

/* I2C3 dùng chung bởi MAX30102 và DS1307; mutex này tuần tự hóa truy cập bus. */
static osMutexId_t s_i2cMutex = NULL;

/* Chỉ true nếu có DS1307 phản hồi lúc boot. Khi false thì không bao giờ poll RTC,
   để một RTC vắng mặt không quấy nhiễu bus dùng chung mỗi giây. */
static bool s_rtcPresent = false;

static void i2cLock(void)
{
    if (s_i2cMutex != NULL) { (void)osMutexAcquire(s_i2cMutex, osWaitForever); }
}

static void i2cUnlock(void)
{
    if (s_i2cMutex != NULL) { (void)osMutexRelease(s_i2cMutex); }
}

/** Khởi tạo lại ngoại vi I2C của STM32 và cấu hình lại cảm biến sau một lỗi bus
 *  kéo dài. Caller phải đang giữ bus mutex. */
static void i2cRecoverBus(void)
{
    (void)HAL_I2C_DeInit(&HW_SENSOR_I2C);
    (void)HAL_I2C_Init(&HW_SENSOR_I2C);
    (void)MAX30102_Init(&HW_SENSOR_I2C);   /* khôi phục FIFO/mode, xóa FIFO */
}

void App_Init(void)
{
    (void)Buzzer_Init();

    /* LED cảnh báo tắt hẳn lúc boot, mẫu nháy về trạng thái nghỉ. */
    StatusLed_Init();
    AlertLed_Init();
    AlertBuzzer_Init();             /* còi cảnh báo, cùng vòng đời với đèn */
    MedicalAlert_Init(NULL);        /* NULL -> ngưỡng mặc định từ alert_config.h */

    /* Nút B1: chân + sườn do CubeMX cấu hình, hàm này bật NVIC EXTI0. */
    PhysicalInput_Init();

    /* Telemetry: cấu hình + bộ đếm (hàng đợi/task tạo sau khi scheduler chạy). */
    Telemetry_Init();

    /* Cảm biến trên bus I2C3 dùng chung (trước scheduler: chưa tranh chấp mutex). */
    const Max30102Status ms = MAX30102_Init(&HW_SENSOR_I2C);
    (void)MAX30102_ReadPartId((uint8_t*)&g_max30102PartId);
    g_sensorOk = (ms == MAX30102_OK) ? 1 : 0;

    const RtcStatus rs = RtcService_Init(&HW_SENSOR_I2C);
    s_rtcPresent = (rs == RTC_STATUS_OK);
    PpgQueue_Reset();
}

void App_DefaultTask(void)
{
    if (s_i2cMutex == NULL)
    {
        s_i2cMutex = osMutexNew(NULL);
    }

    /* TelemetryTask trước DspTask để hàng đợi sẵn sàng khi DSP bắt đầu publish. */
    Telemetry_Start();
    (void)Telemetry_PublishSystem(0U);   /* SYSTEM_BOOT */

    /* Khởi động DSP thread khi scheduler đã chạy (engine không được chạy trong
       GUI/TouchGFX tick). Nó rút sample queue mà ta đổ vào. */
    DspTask_Start();

    /* Chuông báo bật nguồn (non-blocking). */
    (void)Buzzer_PlayMelody(BUZZER_MELODY_STARTUP, BUZZER_MELODY_STARTUP_LEN);

    uint32_t seq = 0U;
    uint32_t rtcDivider = 0U;
    uint32_t failStreak = 0U;
    const uint32_t rtcEvery = 1000U / MAX30102_POLL_PERIOD_MS;  /* ~1 Hz */

    for (;;)
    {
        Buzzer_Process();

        /* Đèn cảnh báo VÀ còi cảnh báo chạy Ở ĐÂY (không phải trong DspTask):
           thuật toán BPM/SpO2 không được chạm GPIO/buzzer. Cả hai đọc CÙNG một cờ
           MedicalAlert_IsActive() lấy một lần, nên chung một vòng đời cảnh báo —
           cùng bắt đầu, cùng dừng. */
        const bool alertActive = MedicalAlert_IsActive();
        AlertLed_Process(alertActive, HAL_GetTick());
        AlertBuzzer_Process(alertActive);

        /* Poll FIFO MAX30102 và đưa RAW sample vào queue cho DSP task. */
        static Max30102Sample samples[16];
        uint8_t got = 0U;
        uint8_t overflow = 0U;
        i2cLock();
        const Max30102Status ms = MAX30102_ReadFifo(samples, 16U, &got);
        (void)MAX30102_ReadOverflowCounter(&overflow);
        i2cUnlock();

        if (ms == MAX30102_OK)
        {
            failStreak = 0U;
            g_sensorOk = 1;
            if (overflow > 0U) { g_fifoOverflowTotal += overflow; }
            const uint32_t now = HAL_GetTick();
            for (uint8_t i = 0U; i < got; ++i)
            {
                PpgRawSample rs;
                rs.sequence = seq++;
                rs.timestampMs = now;
                rs.redRaw = samples[i].red;
                rs.irRaw = samples[i].ir;
                (void)PpgQueue_Push(&rs);
            }
        }
        else
        {
            /* Dung thứ trục trặc bus lẻ tẻ: một lần đọc lỗi đơn không được làm
               trắng phép đo. Chỉ báo lỗi sau một chuỗi lỗi kéo dài, và thử phục
               hồi bus nếu vẫn kẹt. */
            ++failStreak;
            if (failStreak >= HW_SENSOR_FAULT_STREAK)
            {
                g_sensorOk = 0;
            }
            if ((failStreak % HW_I2C_RECOVER_STREAK) == 0U)
            {
                i2cLock();
                i2cRecoverBus();
                i2cUnlock();
            }
        }

        /* RTC ~1 Hz (chỉ khi có DS1307 phản hồi lúc boot): áp dụng yêu cầu cài
           đang chờ, rồi làm mới thời gian công bố. */
        if (s_rtcPresent && (++rtcDivider >= rtcEvery))
        {
            rtcDivider = 0U;
            i2cLock();
            RtcService_ProcessPendingSet();
            RtcService_Poll();
            i2cUnlock();
        }

        osDelay(MAX30102_POLL_PERIOD_MS);
    }
}
