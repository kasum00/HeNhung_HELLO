/**
 * @file    ds1307_driver.c
 * @brief   Cài đặt driver RTC DS1307 (I2C, BCD, 24 giờ).
 * @note    User-owned (ngoài các thư mục generated).
 *
 * =====================================================================
 *  DS1307 REGISTER MAP (Bản đồ thanh ghi)
 * =====================================================================
 *
 *  DS1307 có 8 thanh ghi (register) tại địa chỉ 0x00 - 0x07:
 *
 *  Addr | Ten          | Dinh dang                            | Ghi chu
 *  -----|--------------|--------------------------------------|------------------
 *  0x00 | Seconds      | bit7=CH, bit6-4=chuc giay, bit3-0= don vi giay
 *       |              | CH=1: dong ho dung (oscillator stop) | CH=0: dong ho chay
 *  0x01 | Minutes      | bit6-4=chuc phut, bit3-0= don vi phut   (00-59)
 *  0x02 | Hours        | bit6=12/24 mode, 0=24h                   (00-23)
 *       |              | bit5-4=chuc gio, bit3-0= don vi gio
 *  0x03 | Day of week  | bit2-0= thu trong tuan (1-7)              (1=CN, 2=T2...)
 *  0x04 | Date         | bit5-4=chuc ngay, bit3-0= don vi ngay     (01-31)
 *  0x05 | Month        | bit4=chuc thang, bit3-0= don vi thang     (01-12)
 *  0x06 | Year         | bit7-4=chuc nam, bit3-0= don vi nam       (00-99)
 *       |              |                                       (2000-2099)
 *  0x07 | Control      | SQW output control                       (khong dung)
 *
 *  Dia chi I2C cua DS1307: 0x68 (7-bit), hoac 0xD0 (8-bit write) / 0xD1 (8-bit read)
 *
 * =====================================================================
 *  BCD ENCODING (Ma hoa BCD)
 * =====================================================================
 *
 *  DS1307 luu thoi gian dang BCD (Binary Coded Decimal):
 *    - Byte cao (nibble trai) = hang chuc
 *    - Byte thap (nibble phai) = hang don vi
 *
 *  Ví du:
 *    So thuc 25 -> BCD: 0x25 (nibble cao = 2, nibble thap = 5)
 *    So thuc 59 -> BCD: 0x59
 *    So thuc 09 -> BCD: 0x09
 *
 *  bcd2bin(0x25) = (2 * 10) + 5 = 25
 *  bin2bcd(25)   = (25/10 << 4) | (25%10) = 0x25
 *
 * =====================================================================
 *  CH BIT (Clock Halt - Bit dung dong ho)
 * =====================================================================
 *
 *  Bit 7 cua register 0x00 (Seconds) la bit CH (Clock Halt):
 *    - CH = 1: oscillator DUNG -> thoi gian KHONG tang, RTC khong hoat dong.
 *    - CH = 0: oscillator CHAY -> thoi gian tang binh thuong.
 *
 *  Khi mo phong (power-up), CH thuong la 1 -> dong ho dung.
 *  Phai xoa CH = 0 de khoi chay dong ho. Viec nay KHONG xoa thoi gian
 *  dang luu -> dong ho tiep tuc tu thoi diem da dung truoc do.
 *
 * =====================================================================
 *  READBACK VERIFICATION (Xac nhan lai sau khi ghi)
 * =====================================================================
 *
 *  Sau khi ghi DateTime vao DS1307, driver doc lai va kiem tra:
 *    - Nam, thang, ngay, gio, phut, thu: phai khop EXACTLY (chinh xac 100%).
 *    - Giay: duoc cho phep lech toi da 3 giay (READBACK_SEC_TOLERANCE = 3)
 *      vi thoi gian co the tang trong qua trinh ghi/doc lai (qua 1-2 tick).
 *
 *  Neu khop -> RTC_STATUS_OK
 *  Neu khong khop -> RTC_STATUS_READBACK_MISMATCH (loi ghi du lieu)
 */

#include "ds1307_driver.h"
#include "hw_config.h"

/* ====================================================================
 *  BẢN ĐỒ THANH GHI (Register Map)
 * ====================================================================
 *  DS1307 có 8 thanh ghi tại địa chỉ 0x00-0x07.
 *  Driver chỉ dùng 7 thanh ghi đầu (0x00-0x06) cho thời gian.
 *  Thanh ghi 0x07 (Control) không được sử dụng trong driver này.
 */
#define REG_SECONDS   0x00U   /* Thanh giay: bit7 = CH (clock halt), bit6-0 = giay BCD */
#define REG_HOURS     0x02U   /* Thanh gio: bit6 = mode 12/24 (0 = 24h), bit5-0 = gio BCD */
#define CH_BIT        0x80U   /* Bit mask cho CH (Clock Halt): bit7 = 0x80 = 1000_0000b */
#define HOUR_12_24    0x40U   /* Bit mask cho 12/24 mode: bit6 = 0x40 = 0100_0000b */
#define TIME_REG_LEN  7U      /* So thanh ghi thoi gian: 0x00-0x06 (7 byte) */

/* So giay dung thua cho readback verification (vi thoi gian co the tang khi ghi) */
#define READBACK_SEC_TOLERANCE  3U

/* Handle I2C duoc luu trong module-static, chi khoi tao 1 lan qua DS1307_Init() */
static I2C_HandleTypeDef* s_hi2c = NULL;

/**
 * @brief  Chuyen doi BCD sang so thuc (binary).
 *
 * DS1307 luu thoi gian dang BCD: nibble cao (bit7-4) = hang chuc,
 * nibble thap (bit3-0) = hang don vi.
 *
 * Ví du:
 *   bcd2bin(0x25) -> (2 * 10) + 5 = 25   (25 giay)
 *   bcd2bin(0x59) -> (5 * 10) + 9 = 59   (59 phut)
 *   bcd2bin(0x09) -> (0 * 10) + 9 = 9    (9 gio)
 *
 * Thao tac:
 *   v >> 4      : lay nibble cao (hang chuc), nhan 10
 *   v & 0x0F    : lay nibble thap (hang don vi)
 *   Cong 2 gia tri -> so thuc tuong ung
 */
static uint8_t bcd2bin(uint8_t v)
{
    return (uint8_t)(((v >> 4) * 10U) + (v & 0x0FU));
}

/**
 * @brief  Chuyen doi so thuc (binary) sang BCD.
 *
 * La phep nguoc lai cua bcd2bin.
 *
 * Ví du:
 *   bin2bcd(25) -> (25/10 = 2, 25%10 = 5) -> (2 << 4) | 5 = 0x25
 *   bin2bcd(59) -> (59/10 = 5, 59%10 = 9) -> (5 << 4) | 9 = 0x59
 *   bin2bcd(9)  -> (9/10 = 0, 9%10 = 9)   -> (0 << 4) | 9 = 0x09
 *
 * Thao tac:
 *   v / 10      : lay hang chuc, dich trai 4 bit de vao nibble cao
 *   v % 10      : lay hang don vi, nam o nibble thap
 *   OR 2 gia tri -> byte BCD
 */
static uint8_t bin2bcd(uint8_t v)
{
    return (uint8_t)(((v / 10U) << 4) | (v % 10U));
}

/**
 * @brief  Doc nhieu byte tu DS1307 qua I2C (burst read).
 *
 * Su dung HAL_I2C_Mem_Read:
 *   - DS1307_I2C_ADDRESS: dia chi I2C cua DS1307 (0x68 7-bit, hoac 0xD0 8-bit)
 *   - reg: dia chi thanh ghi bat dau doc (VD: 0x00 de doc seconds)
 *   - I2C_MEMADD_SIZE_8BIT: DS1307 dung 8-bit register addressing (0x00-0x07)
 *   - data: buffer nhan du lieu
 *   - len: so byte can doc (VD: 7 de doc toan bo thoi gian)
 *   - HW_I2C_TIMEOUT_MS: thoi gian cho toi da (tranh treo I2C)
 *
 * I2C transaction khi doc 7 byte tu register 0x00:
 *   [START] [0xD0/W] [0x00] [RESTART] [0xD1/R] [data0] [data1]...[data6] [NACK] [STOP]
 *    (master write dia chi ghi)  (dia chi reg)  (master read dia chi doc)  (7 byte du lieu)
 */
static RtcStatus readRegs(uint8_t reg, uint8_t* data, uint16_t len)
{
    if (s_hi2c == NULL)
    {
        return RTC_STATUS_NOT_INITIALIZED;  /* Chua goi DS1307_Init() */
    }
    /* HAL_I2C_Mem_Read: doc `len` byte tu register `reg` cua DS1307 */
    const HAL_StatusTypeDef st = HAL_I2C_Mem_Read(
        s_hi2c, DS1307_I2C_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT,
        data, len, HW_I2C_TIMEOUT_MS);
    return (st == HAL_OK) ? RTC_STATUS_OK : RTC_STATUS_I2C_ERROR;
}

/**
 * @brief  Ghi nhieu byte vao DS1307 qua I2C (burst write).
 *
 * Su dung HAL_I2C_Mem_Write:
 *   - Ghi `len` byte bat dau tu register `reg`.
 *   - DS1307 auto-increment address: sau khi ghi byte tai 0x00, tu dong
 *     chuyen sang 0x01, 0x02... (nen chi can gui 1 lan dia chi bat dau).
 *
 * I2C transaction khi ghi 7 byte vao register 0x00:
 *   [START] [0xD0/W] [0x00] [data0] [data1]...[data6] [STOP]
 *    (dia chi ghi)  (dia chi reg)  (7 byte du lieu)
 *
 * Luu y:ghi nhieu byte lien tuc (burst) la nhanh hon ghi tung byte rieng,
 * boi khong can phai truyen lai dia chi register moi lan.
 */
static RtcStatus writeRegs(uint8_t reg, const uint8_t* data, uint16_t len)
{
    if (s_hi2c == NULL)
    {
        return RTC_STATUS_NOT_INITIALIZED;  /* Chua goi DS1307_Init() */
    }
    /* HAL_I2C_Mem_Write: ghi `len` byte vao register `reg` cua DS1307 */
    const HAL_StatusTypeDef st = HAL_I2C_Mem_Write(
        s_hi2c, DS1307_I2C_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT,
        (uint8_t*)data, len, HW_I2C_TIMEOUT_MS);
    return (st == HAL_OK) ? RTC_STATUS_OK : RTC_STATUS_I2C_ERROR;
}

/**
 * @brief  Chuyen doi 7 byte thô (BCD) tu DS1307 sang struct DateTime (nhi phan).
 *
 * Thanh ghi raw[] tu DS1307 (dia chi 0x00-0x06):
 *   raw[0] = Seconds:  bit7=CH,  bit6-4=chuc giay,  bit3-0=don vi giay
 *   raw[1] = Minutes:  bit7-6=khong dung, bit5-4=chuc phut, bit3-0=don vi phut
 *   raw[2] = Hours:    bit7-6=khong dung, bit5-4=chuc gio,  bit3-0=don vi gio
 *   raw[3] = Day:      bit7-3=khong dung, bit2-0=thu (1-7)
 *   raw[4] = Date:     bit7-6=khong dung, bit5-4=chuc ngay, bit3-0=don vi ngay
 *   raw[5] = Month:    bit7-5=khong dung, bit4=chuc thang,  bit3-0=don vi thang
 *   raw[6] = Year:     bit7-4=chuc nam,   bit3-0=don vi nam (0-99 -> 2000-2099)
 *
 * Moi truong deu:
 *   1. Ap dung mask de loai bo cac bit khong dung (bit cao).
 *   2. Chuyen BCD -> nhi phan bang bcd2bin().
 *   3. Nam duoc cong DATETIME_YEAR_MIN (2000) de dua vao dinh dang 20xx.
 */
static void decode(const uint8_t raw[TIME_REG_LEN], DateTime* dt)
{
    /* raw[0] & 0x7F: loai bo bit7 (CH bit) de chi lay giay (0-59) */
    dt->second  = bcd2bin(raw[0] & 0x7FU);

    /* raw[1] & 0x7F: loai bit7 (reserved) lay phut (0-59) */
    dt->minute  = bcd2bin(raw[1] & 0x7FU);

    /* raw[2] & 0x3F: loai 2 bit cao lay gio trong che do 24h (0-23) */
    dt->hour    = bcd2bin(raw[2] & 0x3FU);

    /* raw[3] & 0x07: chi lay 3 bit thap = thu trong tuan (1-7) */
    dt->weekday = bcd2bin(raw[3] & 0x07U);

    /* raw[4] & 0x3F: loai 2 bit cao lay ngay (1-31) */
    dt->day     = bcd2bin(raw[4] & 0x3FU);

    /* raw[5] & 0x1F: loai 3 bit cao lay thang (1-12) */
    dt->month   = bcd2bin(raw[5] & 0x1FU);

    /* raw[6]: nam 0-99 tu DS1307 + 2000 = nam 2000-2099 */
    dt->year    = (uint16_t)(DATETIME_YEAR_MIN + bcd2bin(raw[6]));
}

/**
 * @brief  Khoi tao driver: gan handle I2C va kiem tra DS1307 phan hoi.
 *
 * Flow:
 *   1. Kiem tra hi2c != NULL (handle hop le).
 *   2. Luu handle vao s_hi2c (dung cho tat ca ham I2C sau nay).
 *   3. HAL_I2C_IsDeviceReady(): gui 3 lan I2C address den DS1307
 *      de xac nhan thiet bi dang online va phan hoi ACK.
 *      - Neu DS1307 ket noi dung -> tra ve HAL_OK.
 *      - Neu DS1307 mat ket noi hoac sai dia chi -> tra ve HAL_ERROR.
 */
RtcStatus DS1307_Init(I2C_HandleTypeDef* hi2c)
{
    if (hi2c == NULL)
    {
        return RTC_STATUS_INVALID_ARGUMENT;  /* Handle I2C khong hop le */
    }
    s_hi2c = hi2c;  /* Luu handle I2C vao module-static */

    /* Ping DS1307 3 lan de xac nhan thiet bi online tren I2C bus */
    if (HAL_I2C_IsDeviceReady(s_hi2c, DS1307_I2C_ADDRESS, 3U, HW_I2C_TIMEOUT_MS) != HAL_OK)
    {
        return RTC_STATUS_I2C_ERROR;  /* DS1307 khong phan hoi -> loi ket noi */
    }
    return RTC_STATUS_OK;
}

/**
 * @brief  Doc ngay/gio hien tai tu DS1307.
 *
 * Flow:
 *   1. Kiem tra dateTime != NULL.
 *   2. Doc 7 byte tu register 0x00-0x06 qua I2C (burst read).
 *      - raw[0] = giay (co CH bit), raw[1] = phut, raw[2] = gio,
 *        raw[3] = thu, raw[4] = ngay, raw[5] = thang, raw[6] = nam.
 *   3. Decode: chuyen BCD -> nhi phan, ap mask, luu vao DateTime.
 *   4. Kiem tra CH bit (bit7 raw[0]):
 *      - CH=0: dong ho dang chay -> tra ve RTC_STATUS_OK.
 *      - CH=1: dong ho da dung (oscillator stop) -> tra ve
 *        RTC_STATUS_OSCILLATOR_STOPPED (thoi gian co the cu).
 *
 * Luu y: neu oscillator dung, thoi gian doc duoc van la "thoi gian cu"
 * (luc dong ho dung truoc do). Caller can danh flag valid=false.
 */
RtcStatus DS1307_ReadDateTime(DateTime* dateTime)
{
    if (dateTime == NULL)
    {
        return RTC_STATUS_INVALID_ARGUMENT;
    }

    /* Doc 7 byte tu DS1307 bat dau tu register 0x00 (seconds) */
    uint8_t raw[TIME_REG_LEN] = {0};
    RtcStatus s = readRegs(REG_SECONDS, raw, TIME_REG_LEN);
    if (s != RTC_STATUS_OK)
    {
        return s;  /* Loi I2C -> khong tiep tuc */
    }

    /* Chuyen 7 byte BCD -> struct DateTime nhi phan */
    decode(raw, dateTime);

    /* Kiem tra CH bit: neu CH=1 thi oscillator da dung, thoi gian co the cu */
    return ((raw[0] & CH_BIT) != 0U) ? RTC_STATUS_OSCILLATOR_STOPPED : RTC_STATUS_OK;
}

/**
 * @brief  Kiem hop le, ghi va doc lai ngay/gio; khoi dong oscillator.
 *
 * Day la ham quan trong nhat cua driver. Thuc hien 4 buoc lon:
 *
 * BUOC 1 - VALIDATE (Kiem tra hop le):
 *   DateTime_Validate() kiem tra:
 *     - Nam >= 2000, thang 1-12, ngay 1-31 (phu hop voi thang/nam).
 *     - Gio 0-23, phut 0-59, giay 0-59.
 *     - Thu 1-7.
 *     - Nam nhuan (ngay 29/02 chi duoc phep o nam nhuan).
 *
 * BUOC 2 - MA HOA & GHI (Encode & Write):
 *   Chuyen tung truong nhi phan -> BCD bang bin2bcd(), roi ghi 7 byte
 *   vao DS1307 register 0x00-0x06 qua I2C burst write.
 *   Dac biet:
 *     - raw[0] & 0x7F: xoa CH bit (bit7) de KHOI DONG oscillator.
 *     - raw[2] & ~0x40: xoa bit6 de dam bao che do 24 gio.
 *
 * BUOC 3 - DOC LAI & KIEM TRA OSCILLATOR:
 *   Goi DS1307_ReadDateTime() de doc lai thoi gian vua ghi.
 *   Neu oscillator van dung (CH=1): tra ve OSCILLATOR_STOPPED
 *   (co the DS1307 bi loi, khong xoa duoc CH).
 *
 * BUOC 4 - READBACK VERIFICATION (Xac nhan lai):
 *   So sanh du lieu vua ghi voi du lieu doc lai:
 *     - Nam, thang, ngay, thu, gio, phut: phai khop EXACTLY.
 *     - Giay: duoc cho phep lech toi da 3 giay (READBACK_SEC_TOLERANCE).
 *       Ly do: trong qua trinh ghi -> doc lai, thoi gian co the tang
 *       1-2 giay (vi dong ho RTC van chay song song).
 *       Cong thuc: secDelta = (doc_lai + 60 - vua_ghi) % 60
 *       Neu secDelta <= 3 -> OK.
 *   Neu khong khop -> tra ve READBACK_MISMATCH (loi ghi du lieu).
 */
RtcStatus DS1307_SetDateTime(const DateTime* dateTime)
{
    /* --- BUOC 1: VALIDATE --- */
    RtcStatus v = DateTime_Validate(dateTime);
    if (v != RTC_STATUS_OK)
    {
        return v;  /* DateTime khong hop le -> khong ghi vao DS1307 */
    }

    /* --- BUOC 2: MA HOA BCD & GHI VAO DS1307 --- */
    uint8_t raw[TIME_REG_LEN];

    /* Giay: xoa bit7 (CH=0) de khoi dong oscillator */
    raw[0] = bin2bcd(dateTime->second) & 0x7FU;

    /* Phut: BCD binh thuong */
    raw[1] = bin2bcd(dateTime->minute);

    /* Gio: xoa bit6 de dam bao che do 24 gio (KHONG phai 12h) */
    raw[2] = bin2bcd(dateTime->hour) & (uint8_t)~HOUR_12_24;

    /* Thu, Ngay, Thang: BCD binh thuong */
    raw[3] = bin2bcd(dateTime->weekday);
    raw[4] = bin2bcd(dateTime->day);
    raw[5] = bin2bcd(dateTime->month);

    /* Nam: tru di 2000 de dua ve 0-99, roi ma hoa BCD */
    raw[6] = bin2bcd((uint8_t)(dateTime->year - DATETIME_YEAR_MIN));

    /* Ghi 7 byte lien tuc vao DS1307 (burst write tu register 0x00) */
    RtcStatus s = writeRegs(REG_SECONDS, raw, TIME_REG_LEN);
    if (s != RTC_STATUS_OK)
    {
        return s;  /* Loi I2C khi ghi -> khong tiep tuc */
    }

    /* --- BUOC 3 & 4: DOC LAI VA XAC MINH --- */
    DateTime rb;
    s = DS1307_ReadDateTime(&rb);
    if ((s != RTC_STATUS_OK) && (s != RTC_STATUS_OSCILLATOR_STOPPED))
    {
        return s;  /* Loi I2C khi doc lai -> khong the xac minh */
    }
    if (s == RTC_STATUS_OSCILLATOR_STOPPED)
    {
        return RTC_STATUS_OSCILLATOR_STOPPED;  /* CH khong xoa duoc -> oscillator van dung */
    }

    /* Xac minh: nam/thang/ngay/thu/gio/phut phai khop chinh xac */
    const bool dateOk = (rb.year == dateTime->year) && (rb.month == dateTime->month) &&
                        (rb.day == dateTime->day) && (rb.weekday == dateTime->weekday) &&
                        (rb.hour == dateTime->hour) && (rb.minute == dateTime->minute);

    /* Tinh chenh lech giay (cho phep lech toi da 3 giay vi dong ho van chay) */
    const uint8_t secDelta = (uint8_t)((rb.second + 60U - dateTime->second) % 60U);

    if (!dateOk || (secDelta > READBACK_SEC_TOLERANCE))
    {
        return RTC_STATUS_READBACK_MISMATCH;  /* Du lieu ghi va doc lai khong khop */
    }
    return RTC_STATUS_OK;  /* Ghi thanh cong, da xac minh */
}

/**
 * @brief  Kiem tra oscillator dang chay hay da dung.
 *
 * Doc 1 byte tu register 0x00 (seconds), kiem tra CH bit (bit7):
 *   - CH = 0 (bit7 = 0): oscillator DANG CHAY -> running = true.
 *   - CH = 1 (bit7 = 1): oscillator DA DUNG -> running = false.
 *
 * Vi du doc duoc sec = 0x05 (CH=0, giay=5) -> running = true.
 * Vi du doc duoc sec = 0x80 (CH=1, giay=0) -> running = false.
 */
RtcStatus DS1307_IsOscillatorRunning(bool* running)
{
    if (running == NULL)
    {
        return RTC_STATUS_INVALID_ARGUMENT;
    }
    uint8_t sec = 0U;

    /* Doc 1 byte tu register 0x00 (seconds register) */
    RtcStatus s = readRegs(REG_SECONDS, &sec, 1U);
    if (s != RTC_STATUS_OK)
    {
        return s;  /* Loi I2C */
    }

    /* Kiem tra CH bit: bit7 = 0 -> dong ho dang chay */
    *running = ((sec & CH_BIT) == 0U);
    return RTC_STATUS_OK;
}

RtcStatus DS1307_StartOscillator(void)
{
    uint8_t sec = 0U;
    RtcStatus s = readRegs(REG_SECONDS, &sec, 1U);
    if (s != RTC_STATUS_OK)
    {
        return s;
    }
    if ((sec & CH_BIT) == 0U)
    {
        return RTC_STATUS_OK; /* đã đang chạy */
    }
    sec &= (uint8_t)~CH_BIT;
    return writeRegs(REG_SECONDS, &sec, 1U);
}
