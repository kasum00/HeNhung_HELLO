/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/*
 * ============================================================================
 * TỔNG QUAN FILE main.c - Điểm vào chính của chương trình
 * ============================================================================
 * Chuỗi khởi động (boot sequence):
 *   1. HAL_Init()        -> Reset ngoại vi, cấu hình Flash & SysTick
 *   2. SystemClock_Config() -> Cấu hình xung nhịp: HSE 8MHz -> PLL -> 180MHz
 *   3. MX_GPIO_Init()    -> Cấu hình các chân GPIO (đèn LED, nút nhấn, chip select...)
 *   4. MX_CRC_Init()     -> Khởi tạo CRC (TouchGFX cần để kiểm tra dữ liệu)
 *   5. MX_I2C3_Init()    -> Khởi tạo bus I2C3 (giao tiếp cảm biến MAX30102, DS1307 RTC)
 *   6. MX_SPI5_Init()    -> Khởi tạo bus SPI5 (giao tiếp LCD ILI9341)
 *   7. MX_FMC_Init()     -> Khởi tạo FMC控制器 để truy cập SDRAM (bộ đệm framebuffer)
 *   8. MX_LTDC_Init()    -> Khởi tạo LTDC controller (giao diện hiển thị LCD song song)
 *   9. MX_DMA2D_Init()   -> Khởi tạo DMA2D (tăng tốc vẽ hình ảnh 2D cho TouchGFX)
 *  10. MX_USART1_UART_Init() -> Khởi tạo UART1 (gửi dữ liệu telemtry qua serial)
 *  11. App_Init()        -> Khởi tạo ứng dụng (cảm biến, RTC, buzzer...)
 *  12. osKernelInitialize() + osThreadNew() + osKernelStart() -> Khởi động RTOS
 *
 * Sau khi osKernelStart() được gọi, hàm main() KHÔNG BAO GIỜ trở lại.
 * RTOS sẽ phân bổ CPU cho các task theo lịch lập kế hoạch (scheduler).
 * ============================================================================
 */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"    /* CMSIS-RTOS v2 API: osKernelInitialize, osThreadNew, osKernelStart... */
#include "app_touchgfx.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "Components/ili9341/ili9341.h"  /* Driver LCD ILI9341 (chip điều khiển màn hình TFT) */
#include "app_init.h"                   /* Hàm App_Init() và App_DefaultTask() từ thư mục App */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/*
 * SDRAM (IS42S16400J) cần được làm mới định kỳ để dữ liệu không bị mất.
 * REFRESH_COUNT = (64ms / 4096 rows) * tCK = 1386
 * => SDRAM tự làm mới mỗi ~64ms, đảm bảo không mất dữ liệu trong bộ nhớ.
 */
#define REFRESH_COUNT           ((uint32_t)1386)   /* SDRAM refresh counter */
#define SDRAM_TIMEOUT           ((uint32_t)0xFFFF) /* Thời gian chờ tối đa khi gửi lệnh tới SDRAM */

/**
  * @brief  FMC SDRAM Mode definition register defines
  *
  * Các hằng số này dùng để cấu hình thanh ghi Mode Register (MR) của SDRAM.
  * Thanh ghi MR kiểm soát hành vi của SDRAM: độ dài burst, CAS latency,
  * chế độ ghi burst... Giá trị này sẽ được ghi vào SDRAM ở Step 5 của
  * chuỗi khởi động (xem hàm BSP_SDRAM_Initialization_Sequence).
  *
  * Cấu trúc bit của Mode Register (12 bit):
  *   [10]    = Write Burst Mode
  *   [9:7]   = Operating Mode
  *   [6:4]   = CAS Latency (độ trễ đọc)
  *   [3]     = Burst Type (0=Sequential, 1=Interleaved)
  *   [2:0]   = Burst Length
  */
#define SDRAM_MODEREG_BURST_LENGTH_1             ((uint16_t)0x0000) /* Burst length 1: mỗi lần chỉ đọc/ghi 1 word */
#define SDRAM_MODEREG_BURST_LENGTH_2             ((uint16_t)0x0001) /* Burst length 2: đọc/ghi 2 word liên tiếp */
#define SDRAM_MODEREG_BURST_LENGTH_4             ((uint16_t)0x0002) /* Burst length 4: đọc/ghi 4 word liên tiếp */
#define SDRAM_MODEREG_BURST_LENGTH_8             ((uint16_t)0x0004) /* Burst length 8: đọc/ghi 8 word liên tiếp */
#define SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL      ((uint16_t)0x0000) /* Burst tuần tự: địa chỉ tăng dần */
#define SDRAM_MODEREG_BURST_TYPE_INTERLEAVED     ((uint16_t)0x0008) /* Burst xen kẽ: địa chỉ theo bank */
#define SDRAM_MODEREG_CAS_LATENCY_2              ((uint16_t)0x0020) /* CAS Latency = 2 chu kỳ clock (nhanh hơn) */
#define SDRAM_MODEREG_CAS_LATENCY_3              ((uint16_t)0x0030) /* CAS Latency = 3 chu kỳ clock (ổn định hơn) */
#define SDRAM_MODEREG_OPERATING_MODE_STANDARD    ((uint16_t)0x0000) /* Chế độ hoạt động chuẩn */
#define SDRAM_MODEREG_WRITEBURST_MODE_PROGRAMMED ((uint16_t)0x0000) /* Ghi burst theo program (giữ burst length) */
#define SDRAM_MODEREG_WRITEBURST_MODE_SINGLE     ((uint16_t)0x0200) /* Ghi đơn lẻ (bỏ qua burst khi ghi) */

/* Thời gian chờ tối đa cho bus truyền thông (đơn vị: tick của HAL) */
#define I2C3_TIMEOUT_MAX                    0x3000 /* Timeout cho I2C3 - dài hơn vì I2C chậm (100kHz) */
#define SPI5_TIMEOUT_MAX                    0x1000 /* Timeout cho SPI5 - ngắn hơn vì SPI nhanh hơn I2C */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/*
 * Các biến "Handle" (cấu trúc điều khiển) cho từng ngoại vi.
 * Mỗi ngoại vi STM32 có 1 struct Handle chứa trạng thái, cài đặt, và con trỏ tới thanh ghi.
 * HAL library sử dụng Handle này để truyền thông tin giữa các hàm HAL.
 */
CRC_HandleTypeDef hcrc;    /* CRC: kiểm tra dữ liệu (TouchGFX dùng để verify framebuffer) */
DMA2D_HandleTypeDef hdma2d; /* DMA2D: tăng tốc vẽ hình 2D (blit, copy, blend pixel) */
I2C_HandleTypeDef hi2c3;   /* I2C3: bus truyền thông cho cảm biến MAX30102 và RTC DS1307 */
LTDC_HandleTypeDef hltdc;  /* LTDC: LCD-TFT Display Controller - điều khiển màn hình LCD */
SPI_HandleTypeDef hspi5;   /* SPI5: bus SPI cho LCD ILI9341 và cảm biến gia tốc/LCD driver */
UART_HandleTypeDef huart1; /* USART1: cổng serial tốc độ cao để gửi dữ liệu telemtry */
SDRAM_HandleTypeDef hsdram1; /* FMC SDRAM: bộ nhớ ngoài SDRAM (2MB) làm framebuffer cho LCD */

/*
 * === CẤU HÌNH RTOS (CMSIS-RTOS v2) ===
 *
 * Hệ điều hành thời gian thực (RTOS) cho phép chạy nhiều tác vụ đồng thời.
 * Mỗi task (luồng) có: tên, kích thước stack (bộ nhớ đệm), và độ ưu tiên.
 *
 * Stack size tính bằng byte: giá trị * 4 bytes (vì RTOS tính đơn vị là 4-byte words).
 *
 * Hai task chính trong hệ thống:
 *   - defaultTask: đọc cảm biến MAX30102, xử lý SpO2, điều khiển buzzer/LED cảnh báo
 *   - GUI_Task: chạy giao diện TouchGFX trên màn hình LCD
 */

/* Task mặc định: đọc cảm biến, xử lý tín hiệu PPG */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,   /* 128 words = 512 bytes - đủ cho cảm biến và logic cơ bản */
  .priority = (osPriority_t) osPriorityNormal, /* Ưu tiên bình thường, chạy xen kẽ với GUI */
};

/* Task giao diện: TouchGFX điều khiển hiển thị trên LCD */
osThreadId_t GUI_TaskHandle;
const osThreadAttr_t GUI_Task_attributes = {
  .name = "GUI_Task",
  .stack_size = 8192 * 4,  /* 8192 words = 32KB - lớn vì TouchGFX cần nhiều RAM cho giao diện */
  .priority = (osPriority_t) osPriorityNormal, /* Cùng ưu tiên với defaultTask, chia sẻ CPU */
};
/* USER CODE BEGIN PV */
uint8_t isRevD = 0; /* Applicable only for STM32F429I DISCOVERY REVD and above */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);       /* Cấu hình xung nhịp hệ thống (HSE -> PLL -> 180MHz) */
static void MX_GPIO_Init(void);      /* Khởi tạo chân GPIO (LED, nút nhấn, chip select...) */
static void MX_CRC_Init(void);       /* Khởi tạo bộ tính CRC (cho TouchGFX) */
static void MX_I2C3_Init(void);      /* Khởi tạo bus I2C3 (cảm biến, RTC) */
static void MX_SPI5_Init(void);      /* Khởi tạo bus SPI5 (LCD ILI9341) */
static void MX_FMC_Init(void);       /* Khởi tạo FMC controller + SDRAM */
static void MX_LTDC_Init(void);      /* Khởi tạo LCD display controller (LTDC) */
static void MX_DMA2D_Init(void);     /* Khởi tạo DMA2D (tăng tốc vẽ 2D) */
static void MX_USART1_UART_Init(void); /* Khởi tạo UART1 (telemetry serial) */
void StartDefaultTask(void *argument);  /* Task đọc cảm biến (được gọi bởi RTOS) */
extern void TouchGFX_Task(void *argument); /* Task TouchGFX (định nghĩa ở file TouchGFX) */

/* USER CODE BEGIN PFP */

/* ---原型声明: SDRAM init sequence (6 bước khởi động theo datasheet IS42S16400J) --- */
static void BSP_SDRAM_Initialization_Sequence(SDRAM_HandleTypeDef *hsdram, FMC_SDRAM_CommandTypeDef *Command);



/*
 * --- Hàm桥接 I2C3 (giao tiếp cảm biến & RTC) ---
 * Đây là các hàm "bridge" (cầu nối) nằm giữa HAL library và driver cảm biến.
 * Các driver cảm biến (MAX30102, STMPE811) gọi IOE_Read/IOE_Write,
 * còn IOE_Read/IOE_Write gọi các hàm I2C3_xxx này, cuối cùng gọi HAL_I2C_Mem_Read/Write.
 */
static uint8_t            I2C3_ReadData(uint8_t Addr, uint8_t Reg);   /* Đọc 1 byte từ thanh ghi I2C */
static void               I2C3_WriteData(uint8_t Addr, uint8_t Reg, uint8_t Value); /* Ghi 1 byte vào thanh ghi I2C */
static uint8_t            I2C3_ReadBuffer(uint8_t Addr, uint8_t Reg, uint8_t *pBuffer, uint16_t Length); /* Đọc nhiều byte từ I2C */

/* --- Hàm桥接 SPI5 (giao tiếp LCD ILI9341) --- */
static void               SPI5_Write(uint16_t Value);   /* Ghi 1 byte qua SPI5 (gửi dữ liệu/lệnh tới LCD) */
static uint32_t           SPI5_Read(uint8_t ReadSize);  /* Đọc dữ liệu từ LCD qua SPI5 */
static void               SPI5_Error(void);             /* Xử lý lỗi SPI (hiện tại là placeholder) */

/* --- LCD IO bridge functions (cầu nối giữa driver ILI9341 và SPI5) ---
 * Các hàm này được driver ILI9341 gọi để giao tiếp vật lý với LCD.
 * ILI9341 dùng giao thức SPI với 1 chân WRX (Data/Command select):
 *   - WRX = LOW  -> gửi lệnh (command)
 *   - WRX = HIGH -> gửi dữ liệu (data/pixel)
 *   - CS = LOW   -> chọn chip LCD (chip select active low)
 */
void                      LCD_IO_Init(void);             /* Khởi tạo IO cho LCD (toggle chân reset) */
void                      LCD_IO_WriteData(uint16_t RegValue);  /* Ghi dữ liệu pixel/hoặc giá trị thanh ghi */
void                      LCD_IO_WriteReg(uint8_t Reg);  /* Ghi địa chỉ thanh ghi LCD (lệnh) */
uint32_t                  LCD_IO_ReadData(uint16_t RegValue, uint8_t ReadSize); /* Đọc dữ liệu từ LCD */
void                      LCD_Delay(uint32_t delay);     /* Hàm delay (ms) cho LCD */

/* --- IOExpander bridge functions (cầu nối cho STMPE811 touchscreen controller) ---
 * STMPE811 là chip điều khiển cảm ứng điện dung, kết nối qua I2C3.
 * Các hàm IOE_xxx là "glue code" nối driver STMPE811 với bus I2C3 thực tế.
 */
void                      IOE_Init(void);        /* Khởi tạo IOE (đã cấu hình trong CubeMX nên để trống) */
void                      IOE_ITConfig(void);    /* Cấu hình ngắt IOE (không dùng trong dự án này) */
void                      IOE_Delay(uint32_t Delay);    /* Delay cho IOE (gọi HAL_Delay) */
void                      IOE_Write(uint8_t Addr, uint8_t Reg, uint8_t Value);  /* Ghi 1 byte qua I2C */
uint8_t                   IOE_Read(uint8_t Addr, uint8_t Reg);  /* Đọc 1 byte qua I2C */
uint16_t                  IOE_ReadMultiple(uint8_t Addr, uint8_t Reg, uint8_t *pBuffer, uint16_t Length); /* Đọc nhiều byte */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* Con trỏ tới driver LCD (ILI9341). Được gán trong MX_LTDC_Init().
 * LCD_DrvTypeDef chứa các hàm pointer: Init(), DisplayOn(), DisplayOff(), SetCursor()...
 * Cho phép thay đổi chip LCD mà không cần sửa code ở đây (muster pattern).
 */
static LCD_DrvTypeDef* LcdDrv;

/* Biến đếm timeout cho bus truyền thông.
 * Nếu HAL_I2C_xxx hoặc HAL_SPI_xxx không phản hồi trong thời gian này,
 * hàm sẽ trả về lỗi thay vì chờ vô hạn (tránh treo hệ thống).
 */
uint32_t I2c3Timeout = I2C3_TIMEOUT_MAX; /* Timeout cho I2C3: ~12288 ticks */
uint32_t Spi5Timeout = SPI5_TIMEOUT_MAX; /* Timeout cho SPI5: ~4096 ticks */
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
/**
  * @brief  The application entry point.
  *         Hàm entry point - điểm bắt đầu chạy của整个 chương trình.
  *         Sau khi STM32 reset, startup code sẽ gọi hàm này.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /*
   * BƯỚC 1: HAL_Init() - Khởi tạo Hardware Abstraction Layer
   * - Reset tất cả ngoại vi về trạng thái mặc định
   * - Cấu hình Flash interface (thiết lập等待周期 cho đọc Flash)
   * - Bật SysTick timer (cung cấp hàm HAL_Delay() và timebase cho RTOS)
   */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /*
   * BƯỚC 2: SystemClock_Config() - Cấu hình xung nhịp hệ thống
   * Chuyển từ HSI (internal 16MHz) sang HSE (external 8MHz) rồi qua PLL:
   *   HSE 8MHz -> PLL (M=8, N=360, P=2) -> SYSCLK = 180MHz
   *   AHB = 180MHz (DIV1), APB1 = 45MHz (DIV4), APB2 = 90MHz (DIV2)
   * Tốc độ 180MHz là maximum cho STM32F429 (Over-Drive mode)
   */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /*
   * BƯỚC 3: Khởi tạo tất cả các ngoại vi
   * Thứ tự khởi động quan trọng: GPIO trước (vì các ngoại vi khác dùng GPIO),
   * FMC phải trước LTDC (vì LTDC cần framebuffer trong SDRAM).
   */
  MX_GPIO_Init();       /* LED debug (PD12-13), LED cảnh báo (PG12-13), nút B1, chip select... */
  MX_CRC_Init();        /* CRC cho TouchGFX (kiểm tra tính toàn vẹn dữ liệu) */
  MX_I2C3_Init();       /* I2C3 @100kHz: bus cho cảm biến MAX30102 (SpO2) và RTC DS1307 */
  MX_SPI5_Init();       /* SPI5 @9MHz: bus cho LCD ILI9341 và STMPE811 touchscreen */
  MX_FMC_Init();        /* FMC + SDRAM: bộ nhớ ngoài 2MB làm framebuffer (khung hình đệm) */
  MX_LTDC_Init();       /* LTDC: giao diện LCD song song (kết nối ILI9341 qua SDRAM) */
  MX_DMA2D_Init();      /* DMA2D: tăng tốc vẽ 2D (copy, blend pixel, làm mờ) */
  MX_USART1_UART_Init(); /* UART1 @921600 baud: gửi dữ liệu telemtry ra cổng serial */
  MX_TouchGFX_Init();   /* TouchGFX: khởi tạo framework giao diện đồ họa */
  /* Call PreOsInit function */
  MX_TouchGFX_PreOSInit(); /* TouchGFX: khởi tạo trước khi tạo RTOS tasks */
  /* USER CODE BEGIN 2 */
  /*
   * BƯỚC 4: App_Init() - Khởi tạo ứng dụng tùy chỉnh
   * Bao gồm: khởi tạo cảm biến MAX30102, RTC DS1307, buzzer, LED cảnh báo...
   * Được gọi TRƯỚC khi RTOS start vì một số khởi tạo cần chạy trong main loop.
   */
  App_Init();
  /* USER CODE END 2 */

  /*
   * BƯỚC 5: Khởi động RTOS (CMSIS-RTOS v2)
   * osKernelInitialize() -> tạo tasks -> osKernelStart()
   * Sau osKernelStart(), control chuyển sang RTOS scheduler
   * và hàm main() KHÔNG BAO GIỜ trở lại (trừ khi có lỗi nghiêm trọng).
   */
  osKernelInitialize(); /* Khởi tạo kernel RTOS */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /*
   * Tạo 2 task (luồng chạy song song):
   *
   * Task 1 - defaultTask (512 bytes stack):
   *   Chức năng: đọc cảm biến MAX30102 (PPG), tính SpO2, điều khiển buzzer/LED cảnh báo
   *   Gọi hàm App_DefaultTask() trong thư mục App/
   *
   * Task 2 - GUI_Task (32KB stack):
   *   Chức năng: chạy giao diện TouchGFX (hiển thị đồ thị PPG, giá trị SpO2, BPM...)
   *   Gọi hàm TouchGFX_Task() từ thư viện TouchGFX
   */
  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of GUI_Task */
  GUI_TaskHandle = osThreadNew(TouchGFX_Task, NULL, &GUI_Task_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /*
   * KHỞI ĐỘNG RTOS SCHEDULER!
   * Từ đây, RTOS tự phân bổ CPU time cho 2 task theo round-robin (cùng ưu tiên).
   * Hàm này KHÔNG BAO GIỜ trả về (trừ khi có lỗi kernel).
   */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  *
  * === CẤU HÌNH XUNG NHỊP HỆ THỐNG ===
  *
  * Sơ đồ xung nhịp:
  *
  *   HSE (8MHz) ---> [PLL] ---> SYSCLK = 180MHz (tối đa cho STM32F429)
  *                 M=8         AHB  = 180MHz (DIV1)
  *                 N=360       APB1 = 45MHz  (DIV4) - timer, UART, I2C...
  *                 P=2         APB2 = 90MHz  (DIV2) - SPI, ADC, timer nhanh...
  *                 Q=4         USB = 48MHz
  *
  * Công thức PLL:
  *   VCO_input  = HSE / PLLM = 8MHz / 8 = 1MHz
  *   VCO_output = VCO_input * PLLN = 1MHz * 360 = 360MHz
  *   SYSCLK     = VCO_output / PLLP = 360MHz / 2 = 180MHz
  *   USB_CLK    = VCO_output / PLLQ = 360MHz / 4 = 90MHz (cần thêm divider cho 48MHz)
  *
  * Over-Drive mode: cần thiết khi chạy 180MHz (giảm điện áp nội bộ)
  * Flash Latency = 5 cycles: cần đợi thêm khi CPU chạy nhanh để đọc Flash đúng
  *
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /*
   * Bật clock cho module PWR (Power Control) và chọn Voltage Scaling Level 1
   * (điện áp cao nhất để chạy ở 180MHz - Over-Drive cần Voltage Level 1)
   */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /*
   * Cấu hình oscillator:
   * - Sử dụng HSE (High Speed External) - crystal 8MHz gắn ngoài chip
   * - Bật PLL (Phase Locked Loop) - nhân tần số lên 180MHz
   * - Nguồn PLL là HSE (không phải HSI nội bộ)
   */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE; /* Chọn HSE external crystal */
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;                   /* Bật HSE */
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;               /* Bật PLL */
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;       /* PLL lấy xung từ HSE */
  RCC_OscInitStruct.PLL.PLLM = 8;   /* VCO_in = 8MHz / 8 = 1MHz (tối ưu cho PLL) */
  RCC_OscInitStruct.PLL.PLLN = 360; /* VCO_out = 1MHz * 360 = 360MHz */
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2; /* SYSCLK = 360MHz / 2 = 180MHz */
  RCC_OscInitStruct.PLL.PLLQ = 4;   /* USB_CLK = 360MHz / 4 = 90MHz */
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler(); /* Nếu cấu hình oscillator thất bại -> dừng hệ thống */
  }

  /*
   * Bật Over-Drive mode:
   * STM32F429 chạy tối đa 180MHz cần bật Over-Drive để tăng điện áp
   * nội bộ, đảm bảo hoạt động ổn định ở tần số cao.
   */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /*
   * Cấu hình bus clock:
   * - SYSCLK = 180MHz (PLL output) -> chạy CPU core
   * - AHB   = SYSCLK/1 = 180MHz (cho DMA, GPIO, SRAM, FMC...)
   * - APB1  = AHB/4    = 45MHz  (cho UART, SPI slave, I2C, timer chậm...)
   *        * Lưu ý: APB1 timer clock = 90MHz (nhân 2 vì divider > 1)
   * - APB2  = AHB/2    = 90MHz  (cho SPI master, ADC, timer nhanh...)
   *        * Lưu ý: APB2 timer clock = 180MHz (nhân 2 vì divider > 1)
   */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK; /* SYSCLK lấy từ PLL */
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;   /* AHB = SYSCLK / 1 = 180MHz */
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;    /* APB1 = AHB / 4 = 45MHz */
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;    /* APB2 = AHB / 2 = 90MHz */

  /* FLASH_LATENCY_5: khi chạy 180MHz cần 5 chu kỳ chờ để đọc Flash an toàn */
  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief CRC Initialization Function
  *
  * Khởi tạo bộ tính CRC (Cyclic Redundancy Check) phần cứng.
  * TouchGFX sử dụng CRC để kiểm tra tính toàn vẹn dữ liệu framebuffer
  * trên SDRAM, phát hiện lỗi dữ liệu do nhiễu hoặc lỗi bộ nhớ.
  *
  * @param None
  * @retval None
  */
static void MX_CRC_Init(void)
{

  /* USER CODE BEGIN CRC_Init 0 */

  /* USER CODE END CRC_Init 0 */

  /* USER CODE BEGIN CRC_Init 1 */

  /* USER CODE END CRC_Init 1 */
  hcrc.Instance = CRC;
  if (HAL_CRC_Init(&hcrc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CRC_Init 2 */

  /* USER CODE END CRC_Init 2 */

}

/**
  * @brief DMA2D Initialization Function
  *
  * Khởi tạo DMA2D (ChromArt) - bộ tăng tốc vẽ hình 2D phần cứng.
  * DMA2D có thể copy, blend (pha trộn), và đổ màu vùng pixel
  * mà KHÔNG cần CPU, giúp TouchGFX hoạt động mượt mà hơn.
  *
  * Chế độ M2M (Memory-to-Memory): copy dữ liệu từ vùng nhớ này sang vùng nhớ khác
  * Định dạng màu: RGB565 (16 bit/pixel: 5 bit đỏ, 6 bit xanh lá, 5 bit xanh dương)
  *
  * @param None
  * @retval None
  */
static void MX_DMA2D_Init(void)
{

  /* USER CODE BEGIN DMA2D_Init 0 */

  /* USER CODE END DMA2D_Init 0 */

  /* USER CODE BEGIN DMA2D_Init 1 */

  /* USER CODE END DMA2D_Init 1 */
  hdma2d.Instance = DMA2D;
  hdma2d.Init.Mode = DMA2D_M2M;
  hdma2d.Init.ColorMode = DMA2D_OUTPUT_RGB565;
  hdma2d.Init.OutputOffset = 0;
  hdma2d.LayerCfg[1].InputOffset = 0;
  hdma2d.LayerCfg[1].InputColorMode = DMA2D_INPUT_RGB565;
  hdma2d.LayerCfg[1].AlphaMode = DMA2D_NO_MODIF_ALPHA;
  hdma2d.LayerCfg[1].InputAlpha = 0;
  if (HAL_DMA2D_Init(&hdma2d) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_DMA2D_ConfigLayer(&hdma2d, 1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DMA2D_Init 2 */

  /* USER CODE END DMA2D_Init 2 */

}

/**
  * @brief I2C3 Initialization Function
  *
  * Khởi tạo bus I2C3 - giao tiếp serial đồng bộ 2 dây (SDA + SCL).
  * I2C hoạt động ở chế độ Master, tốc độ Standard Mode 100kHz.
  *
  * Các thiết bị trên bus I2C3:
  *   - MAX30102 (addr 0xAE/0xAF): cảm biến SpO2 + nhịp tim (PPG sensor)
  *   - DS1307 (addr 0xD0/0xD1): RTC - đồng hồ thời gian thực
  *   - STMPE811 (addr 0x82): touchscreen controller (nếu có)
  *
  * @param None
  * @retval None
  */
static void MX_I2C3_Init(void)
{

  /* USER CODE BEGIN I2C3_Init 0 */

  /* USER CODE END I2C3_Init 0 */

  /* USER CODE BEGIN I2C3_Init 1 */

  /* USER CODE END I2C3_Init 1 */
  hi2c3.Instance = I2C3;                    /* Chọn peripheral I2C3 */
  hi2c3.Init.ClockSpeed = 100000;           /* Tốc độ bus: 100kHz (Standard Mode) */
  hi2c3.Init.DutyCycle = I2C_DUTYCYCLE_2;   /* Duty cycle: SCL high = 2 * SCL low */
  hi2c3.Init.OwnAddress1 = 0;               /* Địa chỉ của STM32 (0 vì là Master) */
  hi2c3.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT; /* Địa chỉ 7-bit (chuẩn) */
  hi2c3.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE; /* Không dùng dual address */
  hi2c3.Init.OwnAddress2 = 0;               /* Địa chỉ thứ 2 không dùng */
  hi2c3.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE; /* Không phản hồi general call */
  hi2c3.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE; /* Cho phép slave stretch clock (chờ) */
  if (HAL_I2C_Init(&hi2c3) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c3, I2C_ANALOGFILTER_DISABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c3, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C3_Init 2 */

  /* USER CODE END I2C3_Init 2 */

}

/**
  * @brief LTDC Initialization Function
  *
  * Khởi tạo LTDC (LCD-TFT Display Controller) - bộ điều khiển màn hình LCD.
  * LTDC đọc dữ liệu pixel từ SDRAM (framebuffer) và hiển thị lên LCD ILI9341.
  *
  * Màn hình ILI9341: 240x320 pixel, 16 bit màu (RGB565)
  *
  * Thông số timing (theo datasheet ILI9341):
  *   - HSYNC: 9 clocks (horizontal sync pulse width)
  *   - VSYNC: 1 line (vertical sync pulse width)
  *   - HBP (Horizontal Back Porch): 29 - 9 = 20 clocks
  *   - VBP (Vertical Back Porch): 3 - 1 = 2 lines
  *   - Active area: 240 x 320 pixels
  *   - Total width: 279, Total height: 327
  *
  * @param None
  * @retval None
  */
static void MX_LTDC_Init(void)
{

  /* USER CODE BEGIN LTDC_Init 0 */

  /* USER CODE END LTDC_Init 0 */

  LTDC_LayerCfgTypeDef pLayerCfg = {0};

  /* USER CODE BEGIN LTDC_Init 1 */

  /* USER CODE END LTDC_Init 1 */
  hltdc.Instance = LTDC;
  /* Polarity: LCD ILI9341 dùng polaritы active-low cho HSYNC, VSYNC, DE */
  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;  /* HSYNC: active low */
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;  /* VSYNC: active low */
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;  /* Data Enable: active low */
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC; /* Pixel Clock: input.pixel_clock (không đảo) */

  /* Timing parameters cho LCD ILI9341 (240x320) - xem datasheet ILI9341 Section 6 */
  hltdc.Init.HorizontalSync = 9;               /* HSYNC pulse width = 9 pixel clocks */
  hltdc.Init.VerticalSync = 1;                 /* VSYNC pulse width = 1 line */
  hltdc.Init.AccumulatedHBP = 29;              /* Horizontal Back Porch tích lũy = 29 */
  hltdc.Init.AccumulatedVBP = 3;               /* Vertical Back Porch tích lũy = 3 */
  hltdc.Init.AccumulatedActiveW = 269;          /* Width tích lũy (HSYNC + HBP + ActiveW) */
  hltdc.Init.AccumulatedActiveH = 323;          /* Height tích lũy (VSYNC + VBP + ActiveH) */
  hltdc.Init.TotalWidth = 279;                  /* Tổng width = HSYNC + HBP + Active + HFP */
  hltdc.Init.TotalHeigh = 327;                  /* Tổng height = VSYNC + VBP + Active + VFP */
  hltdc.Init.Backcolor.Blue = 0;                /* Màu nền: đen (R=G=B=0) */
  hltdc.Init.Backcolor.Green = 0;
  hltdc.Init.Backcolor.Red = 0;
  if (HAL_LTDC_Init(&hltdc) != HAL_OK)
  {
    Error_Handler();
  }
  /*
   * Cấu hình Layer 0 (layer nền):
   * - Kích thước hiển thị: 240x320 pixel (toàn bộ màn hình ILI9341)
   * - Định dạng màu: RGB565 (16 bit/pixel) - tối ưu cho SDRAM bus 16-bit
   * - Alpha = 255: layer hiển thị đầy đủ opacity (không trong suốt)
   * - Blending Factor CA: Color Alpha blending (pha trộn màu theo alpha)
   * - FBStartAdress = 0: sẽ được cập nhật sau khi cấp phát buffer trong SDRAM
   *   (TouchGFX sẽ gán địa chỉ framebuffer thực tế khi chạy)
   */
  pLayerCfg.WindowX0 = 0;                   /* Bắt đầu từ pixel X = 0 (trái) */
  pLayerCfg.WindowX1 = 240;                 /* Kết thúc tại pixel X = 240 (phải) */
  pLayerCfg.WindowY0 = 0;                   /* Bắt đầu từ pixel Y = 0 (trên) */
  pLayerCfg.WindowY1 = 320;                 /* Kết thúc tại pixel Y = 320 (dưới) */
  pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565; /* RGB565: 5R 6G 5B = 16 bit/pixel */
  pLayerCfg.Alpha = 255;                    /* Alpha = 255: hiển thị đầy đủ (không mờ) */
  pLayerCfg.Alpha0 = 0;                     /* Alpha mặc định của nền = 0 (trong suốt) */
  pLayerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA; /* Pha trộn theo alpha layer */
  pLayerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
  pLayerCfg.FBStartAdress = 0;              /* Địa chỉ framebuffer (cập nhật bởi TouchGFX) */
  pLayerCfg.ImageWidth = 240;               /* Chiều rộng ảnh trong SDRAM */
  pLayerCfg.ImageHeight = 320;              /* Chiều cao ảnh trong SDRAM */
  pLayerCfg.Backcolor.Blue = 0;
  pLayerCfg.Backcolor.Green = 0;
  pLayerCfg.Backcolor.Red = 0;
  if (HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LTDC_Init 2 */
    /*Select the device */
  /*
   * Gán driver ILI9341 vào con trỏ LcdDrv.
   * ili9341_drv là biến toàn cục chứa các hàm pointer của driver ILI9341.
   * Sau đó gọi Init() để gửi các lệnh khởi động LCD qua SPI5.
   * DisplayOff() tắt màn hình tạm thời (sẽ bật lại khi TouchGFX ready).
   */
  LcdDrv = &ili9341_drv;    /* Chọn chip LCD: ILI9341 */
  /* LCD Init */
  LcdDrv->Init();           /* Gửi chuỗi lệnh khởi động LCD (software reset, sleep out, display on...) */

  LcdDrv->DisplayOff();     /* Tắt màn hình tạm thời - TouchGFX sẽ bật lại sau */
  /* USER CODE END LTDC_Init 2 */

}

/**
  * @brief SPI5 Initialization Function
  *
  * Khởi tạo bus SPI5 - giao tiếp serial đồng bộ tốc độ cao.
  * SPI5 được dùng để giao tiếp với:
  *   - LCD ILI9341: gửi pixel data và lệnh điều khiển
  *   - STMPE811: đọc dữ liệu cảm ứng (nếu có trên board)
  *
  * SPI5 pins (STM32F429I-Discovery):
  *   - SCK:  PF7  (Serial Clock)
  *   - MISO: PF8  (Master In Slave Out) - đọc dữ liệu từ LCD
  *   - MOSI: PF9  (Master Out Slave In) - gửi dữ liệu tới LCD
  *   - NCS:  PC2  (Chip Select, software controlled) - chọn LCD
  *
  * Tốc độ: SPI5_CLK = APB2(90MHz) / 16 = 5.625MHz (Prescaler = 16)
  *
  * @param None
  * @retval None
  */
static void MX_SPI5_Init(void)
{

  /* USER CODE BEGIN SPI5_Init 0 */

  /* USER CODE END SPI5_Init 0 */

  /* USER CODE BEGIN SPI5_Init 1 */

  /* USER CODE END SPI5_Init 1 */
  /* SPI5 parameter configuration*/
  hspi5.Instance = SPI5;                           /* Chọn peripheral SPI5 */
  hspi5.Init.Mode = SPI_MODE_MASTER;               /* STM32 là Master (điều khiển clock) */
  hspi5.Init.Direction = SPI_DIRECTION_2LINES;     /* Full-duplex: MISO + MOSI (2 dây dữ liệu) */
  hspi5.Init.DataSize = SPI_DATASIZE_8BIT;         /* Mỗi lần gửi 8 bit (1 byte) */
  hspi5.Init.CLKPolarity = SPI_POLARITY_LOW;       /* Clock_idle = LOW (CPOL = 0) */
  hspi5.Init.CLKPhase = SPI_PHASE_1EDGE;           /* Data sample trên cạnh đầu tiên (CPHA = 0) */
  hspi5.Init.NSS = SPI_NSS_SOFT;                   /* Chip Select điều khiển bằng software (GPIO) */
  hspi5.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16; /* SPI_CLK = 90MHz/16 = 5.625MHz */
  hspi5.Init.FirstBit = SPI_FIRSTBIT_MSB;          /* Bit cao (MSB) gửi trước */
  hspi5.Init.TIMode = SPI_TIMODE_DISABLE;          /* Không dùng TI frame format */
  hspi5.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE; /* Không tính CRC phần cứng */
  hspi5.Init.CRCPolynomial = 10;                   /* Polynomial CRC (không dùng vì CRC disabled) */
  if (HAL_SPI_Init(&hspi5) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI5_Init 2 */
  /*
   * === PHÁT HIỆN PHIÊN BẢN BOARD (Board Revision Detection) ===
   *
   * STM32F429I-Discovery có nhiều phiên bản board.
   * Phiên bản Rev D trở lên sử dụng gyroscope LSM303DLHC thay vì L3GD20H.
   * Hai chip này có WHO_AM_I register khác nhau:
   *   - L3GD20H (board cũ):  WHO_AM_I = 0xD4
   *   - LSM303DLHC (Rev D+): WHO_AM_I = 0xD3
   *
   * Thông tin này quan trọng để xử lý touch input đúng cách.
   *
   * Giao thức SPI đọc WHO_AM_I:
   *   1. Kéo CS xuống LOW (chọn chip)
   *   2. Gửi lệnh đọc: 0x8F = 0b10001111
   *      (bit 7 = 1: đọc, bits 6-0 = 0x0F: địa chỉ WHO_AM_I register)
   *   3. Đọc 1 byte phản hồi
   *   4. Kéo CS lên HIGH (bỏ chọn chip)
   */
  const uint8_t READ_ID_CMD = 0x8F; /* Lệnh SPI: đọc thanh ghi WHO_AM_I (0x0F) */
  uint8_t pdata = 0;
  HAL_GPIO_WritePin(SPI5_NCS_GPIO_Port, SPI5_NCS_Pin, GPIO_PIN_RESET); /* CS = LOW: bắt đầu giao dịch */
  HAL_SPI_Transmit(&hspi5, &READ_ID_CMD, 1, 1000);  /* Gửi lệnh đọc */
  HAL_SPI_Receive(&hspi5, &pdata, 1, 1000);          /* Nhận 1 byte dữ liệu từ cảm biến */
  HAL_GPIO_WritePin(SPI5_NCS_GPIO_Port, SPI5_NCS_Pin, GPIO_PIN_SET);   /* CS = HIGH: kết thúc giao dịch */
  if (pdata == 0xD3) /* LSM303DLHC trả về 0xD3 -> đây là board Rev D hoặc mới hơn */
  {
    isRevD = 1; /* Đánh dấu: dùng logic touch input cho Rev D+ */
  }
  /* USER CODE END SPI5_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  *
  * Khởi tạo USART1 - cổng giao tiếp serial bất đồng bộ.
  * Tốc độ 921600 baud: rất nhanh, phù hợp để gửi dữ liệu telemtry
  * (giá trị SpO2, BPM, raw PPG) lên máy tính qua USB-to-Serial adapter.
  *
  * USART1 pins:
  *   - TX: PA9  (gửi dữ liệu ra ngoài)
  *   - RX: PA10 (nhận dữ liệu từ ngoài)
  *
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 921600;               /* Tốc độ baud: 921600 bit/s (rất nhanh!) */
  huart1.Init.WordLength = UART_WORDLENGTH_8B;  /* 8 bit dữ liệu mỗi byte */
  huart1.Init.StopBits = UART_STOPBITS_1;       /* 1 bit dừng */
  huart1.Init.Parity = UART_PARITY_NONE;        /* Không bit parity (đơn giản, nhanh) */
  huart1.Init.Mode = UART_MODE_TX_RX;           /* Cả gửi (TX) và nhận (RX) */
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;  /* Không dùng phần cứng flow control (RTS/CTS) */
  huart1.Init.OverSampling = UART_OVERSAMPLING_16; /* Oversampling 16x (chuẩn, ổn định nhất) */
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/*
 * FMC initialization function
 *
 * === FMC (Flexible Memory Controller) + SDRAM (IS42S16400J, 2MB) ===
 *
 * FMC là bộ điều khiển bộ nhớ ngoại của STM32F429.
 * SDRAM (IS42S16400J) được kết nối qua FMC:
 *   - Dung lượng: 2MB (1M x 16 bit = 64 Mbit)
 *   - Bus width: 16 bit
 *   - Tốc độ: 90MHz (FMC_SDCLK = AHB/2 = 90MHz)
 *
 * SDRAM đóng vai trò framebuffer: TouchGFX vẽ frame vào SDRAM,
 * LTDC đọc từ SDRAM và hiển thị lên LCD.
 * Không có SDRAM -> TouchGFX không hoạt động được!
 *
 * FMC pins (STM32F429I-Discovery):
 *   - Data:  PD0-PD1, PE0-PE1, PD8-PD10, PD14-PD15, PE7-PE15, PD8-PD10
 *   - Address: PF0-PF15, PG0-PG15, PH0-PH3
 *   - Control: PC3 (SDNE2), PH5 (SDNWE), PC2 (SDCKE - alternate function)
 */
static void MX_FMC_Init(void)
{

  /* USER CODE BEGIN FMC_Init 0 */

  /* USER CODE END FMC_Init 0 */

  FMC_SDRAM_TimingTypeDef SdramTiming = {0};

  /* USER CODE BEGIN FMC_Init 1 */

  /* USER CODE END FMC_Init 1 */

  /** Perform the SDRAM1 memory initialization sequence
  */
  hsdram1.Instance = FMC_SDRAM_DEVICE;
  /* hsdram1.Init */
  hsdram1.Init.SDBank = FMC_SDRAM_BANK2;
  hsdram1.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_8;
  hsdram1.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_12;
  hsdram1.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
  hsdram1.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
  hsdram1.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_3;
  hsdram1.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
  hsdram1.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
  hsdram1.Init.ReadBurst = FMC_SDRAM_RBURST_DISABLE;
  hsdram1.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_1;
  /* SdramTiming */
  SdramTiming.LoadToActiveDelay = 2;
  SdramTiming.ExitSelfRefreshDelay = 7;
  SdramTiming.SelfRefreshTime = 4;
  SdramTiming.RowCycleDelay = 7;
  SdramTiming.WriteRecoveryTime = 3;
  SdramTiming.RPDelay = 2;
  SdramTiming.RCDDelay = 2;

  if (HAL_SDRAM_Init(&hsdram1, &SdramTiming) != HAL_OK)
  {
    Error_Handler( );
  }

  /* USER CODE BEGIN FMC_Init 2 */

  FMC_SDRAM_CommandTypeDef command;

  /* Program the SDRAM external device */
  BSP_SDRAM_Initialization_Sequence(&hsdram1, &command);
  /* USER CODE END FMC_Init 2 */
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */
  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOE, VSYNC_FREQ_Pin|RENDER_TIME_Pin|FRAME_RATE_Pin|MCU_ACTIVE_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SPI5_NCS_GPIO_Port, SPI5_NCS_Pin, GPIO_PIN_SET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12|GPIO_PIN_13, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOG, ALERT_LED_1_Pin|ALERT_LED_2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : VSYNC_FREQ_Pin RENDER_TIME_Pin FRAME_RATE_Pin MCU_ACTIVE_Pin */
  GPIO_InitStruct.Pin = VSYNC_FREQ_Pin|RENDER_TIME_Pin|FRAME_RATE_Pin|MCU_ACTIVE_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);

  /*Configure GPIO pin : SPI5_NCS_Pin */
  GPIO_InitStruct.Pin = SPI5_NCS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SPI5_NCS_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PC2 */
  GPIO_InitStruct.Pin = GPIO_PIN_2;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : B1_BUTTON_Pin */
  GPIO_InitStruct.Pin = B1_BUTTON_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(B1_BUTTON_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : PD12 PD13 */
  GPIO_InitStruct.Pin = GPIO_PIN_12|GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : ALERT_LED_1_Pin ALERT_LED_2_Pin */
  GPIO_InitStruct.Pin = ALERT_LED_1_Pin|ALERT_LED_2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */
  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/**
  * @brief  Perform the SDRAM external memory initialization sequence
  * @param  hsdram: SDRAM handle
  * @param  Command: Pointer to SDRAM command structure
  * @retval None
  */
static void BSP_SDRAM_Initialization_Sequence(SDRAM_HandleTypeDef *hsdram, FMC_SDRAM_CommandTypeDef *Command)
{
 __IO uint32_t tmpmrd =0;

  /* Step 1:  Configure a clock configuration enable command */
  Command->CommandMode             = FMC_SDRAM_CMD_CLK_ENABLE;
  Command->CommandTarget           = FMC_SDRAM_CMD_TARGET_BANK2;
  Command->AutoRefreshNumber       = 1;
  Command->ModeRegisterDefinition  = 0;

  /* Send the command */
  HAL_SDRAM_SendCommand(hsdram, Command, SDRAM_TIMEOUT);

  /* Step 2: Insert 100 us minimum delay */
  /* Inserted delay is equal to 1 ms due to systick time base unit (ms) */
  HAL_Delay(1);

  /* Step 3: Configure a PALL (precharge all) command */
  Command->CommandMode             = FMC_SDRAM_CMD_PALL;
  Command->CommandTarget           = FMC_SDRAM_CMD_TARGET_BANK2;
  Command->AutoRefreshNumber       = 1;
  Command->ModeRegisterDefinition  = 0;

  /* Send the command */
  HAL_SDRAM_SendCommand(hsdram, Command, SDRAM_TIMEOUT);

  /* Step 4: Configure an Auto Refresh command */
  Command->CommandMode             = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
  Command->CommandTarget           = FMC_SDRAM_CMD_TARGET_BANK2;
  Command->AutoRefreshNumber       = 4;
  Command->ModeRegisterDefinition  = 0;

  /* Send the command */
  HAL_SDRAM_SendCommand(hsdram, Command, SDRAM_TIMEOUT);

  /* Step 5: Program the external memory mode register */
  tmpmrd = (uint32_t)SDRAM_MODEREG_BURST_LENGTH_1          |
                     SDRAM_MODEREG_BURST_TYPE_SEQUENTIAL   |
                     SDRAM_MODEREG_CAS_LATENCY_3           |
                     SDRAM_MODEREG_OPERATING_MODE_STANDARD |
                     SDRAM_MODEREG_WRITEBURST_MODE_SINGLE;

  Command->CommandMode             = FMC_SDRAM_CMD_LOAD_MODE;
  Command->CommandTarget           = FMC_SDRAM_CMD_TARGET_BANK2;
  Command->AutoRefreshNumber       = 1;
  Command->ModeRegisterDefinition  = tmpmrd;

  /* Send the command */
  HAL_SDRAM_SendCommand(hsdram, Command, SDRAM_TIMEOUT);

  /* Step 6: Set the refresh rate counter */
  /* Set the device refresh rate */
  HAL_SDRAM_ProgramRefreshRate(hsdram, REFRESH_COUNT);
}

/**
  * @brief  IOE Low Level Initialization.
  */
void IOE_Init(void)
{
  //Dummy function called when initializing to stmpe811 to setup the i2c.
  //This is done with cubmx and is therfore not done here.
}

/**
  * @brief  IOE Low Level Interrupt configuration.
  */
void IOE_ITConfig(void)
{
  //Dummy function called when initializing to stmpe811 to setup interupt for the i2c.
  //The interupt is not used in our case, therefore nothing is done here.
}

/**
  * @brief  IOE Writes single data operation.
  * @param  Addr: I2C Address
  * @param  Reg: Reg Address
  * @param  Value: Data to be written
  */
void IOE_Write(uint8_t Addr, uint8_t Reg, uint8_t Value)
{
  I2C3_WriteData(Addr, Reg, Value);
}

/**
  * @brief  IOE Reads single data.
  * @param  Addr: I2C Address
  * @param  Reg: Reg Address
  * @retval The read data
  */
uint8_t IOE_Read(uint8_t Addr, uint8_t Reg)
{
  return I2C3_ReadData(Addr, Reg);
}

/**
  * @brief  IOE Reads multiple data.
  * @param  Addr: I2C Address
  * @param  Reg: Reg Address
  * @param  pBuffer: pointer to data buffer
  * @param  Length: length of the data
  * @retval 0 if no problems to read multiple data
  */
uint16_t IOE_ReadMultiple(uint8_t Addr, uint8_t Reg, uint8_t *pBuffer, uint16_t Length)
{
 return I2C3_ReadBuffer(Addr, Reg, pBuffer, Length);
}

/**
  * @brief  IOE Delay.
  * @param  Delay in ms
  */
void IOE_Delay(uint32_t Delay)
{
  HAL_Delay(Delay);
}

/**
  * @brief  Writes a value in a register of the device through BUS.
  * @param  Addr: Device address on BUS Bus.
  * @param  Reg: The target register address to write
  * @param  Value: The target register value to be written
  */
static void I2C3_WriteData(uint8_t Addr, uint8_t Reg, uint8_t Value)
{
  HAL_StatusTypeDef status = HAL_OK;

  status = HAL_I2C_Mem_Write(&hi2c3, Addr, (uint16_t)Reg, I2C_MEMADD_SIZE_8BIT, &Value, 1, I2c3Timeout);

  /* Check the communication status */
  if(status != HAL_OK)
  {
    /* Re-Initialize the BUS */
    //I2Cx_Error();
  }
}

/**
  * @brief  Reads a register of the device through BUS.
  * @param  Addr: Device address on BUS Bus.
  * @param  Reg: The target register address to write
  * @retval Data read at register address
  */
static uint8_t I2C3_ReadData(uint8_t Addr, uint8_t Reg)
{
  HAL_StatusTypeDef status = HAL_OK;
  uint8_t value = 0;

  status = HAL_I2C_Mem_Read(&hi2c3, Addr, Reg, I2C_MEMADD_SIZE_8BIT, &value, 1, I2c3Timeout);

  /* Check the communication status */
  if(status != HAL_OK)
  {
    /* Re-Initialize the BUS */
    //I2Cx_Error();

  }
  return value;
}

/**
  * @brief  Reads multiple data on the BUS.
  * @param  Addr: I2C Address
  * @param  Reg: Reg Address
  * @param  pBuffer: pointer to read data buffer
  * @param  Length: length of the data
  * @retval 0 if no problems to read multiple data
  */
static uint8_t I2C3_ReadBuffer(uint8_t Addr, uint8_t Reg, uint8_t *pBuffer, uint16_t Length)
{
  HAL_StatusTypeDef status = HAL_OK;

  status = HAL_I2C_Mem_Read(&hi2c3, Addr, (uint16_t)Reg, I2C_MEMADD_SIZE_8BIT, pBuffer, Length, I2c3Timeout);

  /* Check the communication status */
  if(status == HAL_OK)
  {
    return 0;
  }
  else
  {
    /* Re-Initialize the BUS */
    //I2Cx_Error();

    return 1;
  }
}

/**
  * @brief  Reads 4 bytes from device.
  * @param  ReadSize: Number of bytes to read (max 4 bytes)
  * @retval Value read on the SPI
  */
static uint32_t SPI5_Read(uint8_t ReadSize)
{
  HAL_StatusTypeDef status = HAL_OK;
  uint32_t readvalue;

  status = HAL_SPI_Receive(&hspi5, (uint8_t*) &readvalue, ReadSize, Spi5Timeout);

  /* Check the communication status */
  if(status != HAL_OK)
  {
    /* Re-Initialize the BUS */
    SPI5_Error();
  }

  return readvalue;
}

/**
  * @brief  Writes a byte to device.
  * @param  Value: value to be written
  */
static void SPI5_Write(uint16_t Value)
{
  HAL_StatusTypeDef status = HAL_OK;

  status = HAL_SPI_Transmit(&hspi5, (uint8_t*) &Value, 1, Spi5Timeout);

  /* Check the communication status */
  if(status != HAL_OK)
  {
    /* Re-Initialize the BUS */
    SPI5_Error();
  }
}

/**
  * @brief  SPI5 error treatment function.
  */
static void SPI5_Error(void)
{
  /* De-initialize the SPI communication BUS */
  //HAL_SPI_DeInit(&SpiHandle);

  /* Re- Initialize the SPI communication BUS */
  //SPIx_Init();
}

void LCD_IO_Init(void)
{
  /* Set or Reset the control line */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_SET);
}

/**
  * @brief  Writes register value.
  */
void LCD_IO_WriteData(uint16_t RegValue)
{
  /* Set WRX to send data */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_SET);

  /* Reset LCD control line(/CS) and Send data */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_RESET);
  SPI5_Write(RegValue);

  /* Deselect: Chip Select high */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_SET);
}

/**
  * @brief  Writes register address.
  */
void LCD_IO_WriteReg(uint8_t Reg)
{
  /* Reset WRX to send command */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_RESET);

  /* Reset LCD control line(/CS) and Send command */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_RESET);
  SPI5_Write(Reg);

  /* Deselect: Chip Select high */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_SET);
}

/**
  * @brief  Reads register value.
  * @param  RegValue Address of the register to read
  * @param  ReadSize Number of bytes to read
  * @retval Content of the register value
  */
uint32_t LCD_IO_ReadData(uint16_t RegValue, uint8_t ReadSize)
{
  uint32_t readvalue = 0;

  /* Select: Chip Select low */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_RESET);

  /* Reset WRX to send command */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_RESET);

  SPI5_Write(RegValue);

  readvalue = SPI5_Read(ReadSize);

  /* Set WRX to send data */
  HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_SET);

  /* Deselect: Chip Select high */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_2, GPIO_PIN_SET);

  return readvalue;
}

/**
  * @brief  Wait for loop in ms.
  * @param  Delay in ms.
  */
void LCD_Delay(uint32_t Delay)
{
  HAL_Delay(Delay);
}

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  /* Sensor/RTC task: poll MAX30102 FIFO -> PPG queue, buzzer + LED cảnh báo. */
  App_DefaultTask();
  /* USER CODE END 5 */
}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */

  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
