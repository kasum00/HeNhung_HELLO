/**
 * @file    ds1307_driver.c
 * @brief   Cài đặt driver RTC DS1307 (I2C, BCD, 24 giờ).
 * @note    User-owned (ngoài các thư mục generated).
 */

#include "ds1307_driver.h"
#include "hw_config.h"

/* Bản đồ thanh ghi. */
#define REG_SECONDS   0x00U   /* bit7 = CH (clock halt) */
#define REG_HOURS     0x02U   /* bit6 = mode 12/24 (0 = 24h) */
#define CH_BIT        0x80U
#define HOUR_12_24    0x40U
#define TIME_REG_LEN  7U

/* Số giây dung thứ giữa lúc ghi và lúc đọc lại (do vượt qua một tick). */
#define READBACK_SEC_TOLERANCE  3U

static I2C_HandleTypeDef* s_hi2c = NULL;

static uint8_t bcd2bin(uint8_t v)
{
    return (uint8_t)(((v >> 4) * 10U) + (v & 0x0FU));
}

static uint8_t bin2bcd(uint8_t v)
{
    return (uint8_t)(((v / 10U) << 4) | (v % 10U));
}

static RtcStatus readRegs(uint8_t reg, uint8_t* data, uint16_t len)
{
    if (s_hi2c == NULL)
    {
        return RTC_STATUS_NOT_INITIALIZED;
    }
    const HAL_StatusTypeDef st = HAL_I2C_Mem_Read(
        s_hi2c, DS1307_I2C_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT,
        data, len, HW_I2C_TIMEOUT_MS);
    return (st == HAL_OK) ? RTC_STATUS_OK : RTC_STATUS_I2C_ERROR;
}

static RtcStatus writeRegs(uint8_t reg, const uint8_t* data, uint16_t len)
{
    if (s_hi2c == NULL)
    {
        return RTC_STATUS_NOT_INITIALIZED;
    }
    const HAL_StatusTypeDef st = HAL_I2C_Mem_Write(
        s_hi2c, DS1307_I2C_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT,
        (uint8_t*)data, len, HW_I2C_TIMEOUT_MS);
    return (st == HAL_OK) ? RTC_STATUS_OK : RTC_STATUS_I2C_ERROR;
}

static void decode(const uint8_t raw[TIME_REG_LEN], DateTime* dt)
{
    dt->second  = bcd2bin(raw[0] & 0x7FU);   /* bỏ bit CH */
    dt->minute  = bcd2bin(raw[1] & 0x7FU);
    dt->hour    = bcd2bin(raw[2] & 0x3FU);   /* trường 24 giờ */
    dt->weekday = bcd2bin(raw[3] & 0x07U);
    dt->day     = bcd2bin(raw[4] & 0x3FU);
    dt->month   = bcd2bin(raw[5] & 0x1FU);
    dt->year    = (uint16_t)(DATETIME_YEAR_MIN + bcd2bin(raw[6]));
}

RtcStatus DS1307_Init(I2C_HandleTypeDef* hi2c)
{
    if (hi2c == NULL)
    {
        return RTC_STATUS_INVALID_ARGUMENT;
    }
    s_hi2c = hi2c;

    if (HAL_I2C_IsDeviceReady(s_hi2c, DS1307_I2C_ADDRESS, 3U, HW_I2C_TIMEOUT_MS) != HAL_OK)
    {
        return RTC_STATUS_I2C_ERROR;
    }
    return RTC_STATUS_OK;
}

RtcStatus DS1307_ReadDateTime(DateTime* dateTime)
{
    if (dateTime == NULL)
    {
        return RTC_STATUS_INVALID_ARGUMENT;
    }
    uint8_t raw[TIME_REG_LEN] = {0};
    RtcStatus s = readRegs(REG_SECONDS, raw, TIME_REG_LEN);
    if (s != RTC_STATUS_OK)
    {
        return s;
    }
    decode(raw, dateTime);
    /* Báo oscillator đã dừng để caller đánh dấu thời gian là cũ. */
    return ((raw[0] & CH_BIT) != 0U) ? RTC_STATUS_OSCILLATOR_STOPPED : RTC_STATUS_OK;
}

RtcStatus DS1307_SetDateTime(const DateTime* dateTime)
{
    RtcStatus v = DateTime_Validate(dateTime);
    if (v != RTC_STATUS_OK)
    {
        return v;
    }

    uint8_t raw[TIME_REG_LEN];
    raw[0] = bin2bcd(dateTime->second) & 0x7FU;                 /* CH = 0 -> chạy */
    raw[1] = bin2bcd(dateTime->minute);
    raw[2] = bin2bcd(dateTime->hour) & (uint8_t)~HOUR_12_24;    /* ép về 24 giờ */
    raw[3] = bin2bcd(dateTime->weekday);
    raw[4] = bin2bcd(dateTime->day);
    raw[5] = bin2bcd(dateTime->month);
    raw[6] = bin2bcd((uint8_t)(dateTime->year - DATETIME_YEAR_MIN));

    RtcStatus s = writeRegs(REG_SECONDS, raw, TIME_REG_LEN);
    if (s != RTC_STATUS_OK)
    {
        return s;
    }

    /* Đọc lại ngay và xác minh (dung thứ lệch vài giây). */
    DateTime rb;
    s = DS1307_ReadDateTime(&rb);
    if ((s != RTC_STATUS_OK) && (s != RTC_STATUS_OSCILLATOR_STOPPED))
    {
        return s;
    }
    if (s == RTC_STATUS_OSCILLATOR_STOPPED)
    {
        return RTC_STATUS_OSCILLATOR_STOPPED; /* CH không xóa được */
    }

    const bool dateOk = (rb.year == dateTime->year) && (rb.month == dateTime->month) &&
                        (rb.day == dateTime->day) && (rb.weekday == dateTime->weekday) &&
                        (rb.hour == dateTime->hour) && (rb.minute == dateTime->minute);
    const uint8_t secDelta = (uint8_t)((rb.second + 60U - dateTime->second) % 60U);
    if (!dateOk || (secDelta > READBACK_SEC_TOLERANCE))
    {
        return RTC_STATUS_READBACK_MISMATCH;
    }
    return RTC_STATUS_OK;
}

RtcStatus DS1307_IsOscillatorRunning(bool* running)
{
    if (running == NULL)
    {
        return RTC_STATUS_INVALID_ARGUMENT;
    }
    uint8_t sec = 0U;
    RtcStatus s = readRegs(REG_SECONDS, &sec, 1U);
    if (s != RTC_STATUS_OK)
    {
        return s;
    }
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
