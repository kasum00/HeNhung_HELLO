#ifndef DATETIME_H
#define DATETIME_H

/**
 * @file    datetime.h
 * @brief   Kiểu ngày/giờ lịch dùng chung, mã trạng thái RTC và kiểm tra hợp lệ.
 *
 * Kiểu phía firmware dùng bởi driver DS1307 và RTC service. GUI có GuiTime riêng;
 * service ánh xạ qua lại. Thuần C, không HAL/bộ nhớ động.
 */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Dải năm lịch hỗ trợ (DS1307 lưu năm 2 chữ số -> 20xx). */
#define DATETIME_YEAR_MIN  2000U
#define DATETIME_YEAR_MAX  2099U

/** @brief Ngày/giờ lịch tách trường (24 giờ). */
typedef struct
{
    uint16_t year;    /**< Năm đầy đủ, ví dụ 2026 (DATETIME_YEAR_MIN..MAX). */
    uint8_t  month;   /**< 1..12 */
    uint8_t  day;     /**< 1..31 (đã kiểm theo tháng/năm nhuận)            */
    uint8_t  weekday; /**< 1..7  (1 = Thứ Hai theo quy ước; DS1307 định)   */
    uint8_t  hour;    /**< 0..23 */
    uint8_t  minute;  /**< 0..59 */
    uint8_t  second;  /**< 0..59 */
} DateTime;

/** @brief Kết quả thao tác RTC. */
typedef enum
{
    RTC_STATUS_OK = 0,
    RTC_STATUS_INVALID_ARGUMENT,
    RTC_STATUS_INVALID_DATE,
    RTC_STATUS_INVALID_TIME,
    RTC_STATUS_I2C_ERROR,
    RTC_STATUS_OSCILLATOR_STOPPED,
    RTC_STATUS_READBACK_MISMATCH,
    RTC_STATUS_NOT_INITIALIZED
} RtcStatus;

/**
 * @brief Kiểm tra năm nhuận theo lịch Gregory.
 * @param year Năm đầy đủ.
 * @return True nếu @p year là năm nhuận.
 */
static inline bool DateTime_IsLeapYear(uint16_t year)
{
    return ((year % 400U) == 0U) || (((year % 4U) == 0U) && ((year % 100U) != 0U));
}

/**
 * @brief Số ngày trong một tháng.
 * @param year  Năm đầy đủ (cho tháng Hai).
 * @param month 1..12.
 * @return Số ngày trong tháng, hoặc 0 nếu @p month ngoài dải.
 */
static inline uint8_t DateTime_DaysInMonth(uint16_t year, uint8_t month)
{
    static const uint8_t days[12] = {31U, 28U, 31U, 30U, 31U, 30U,
                                     31U, 31U, 30U, 31U, 30U, 31U};
    if ((month < 1U) || (month > 12U))
    {
        return 0U;
    }
    if ((month == 2U) && DateTime_IsLeapYear(year))
    {
        return 29U;
    }
    return days[month - 1U];
}

/**
 * @brief Kiểm tra tính hợp lệ của một DateTime.
 * @param dt Giá trị cần kiểm (có thể null).
 * @return RTC_STATUS_OK, hoặc lý do INVALID_* cụ thể.
 *
 * Kiểm các trường giờ trước, rồi tới ngày (nên giờ sai báo INVALID_TIME thay vì
 * INVALID_DATE).
 */
static inline RtcStatus DateTime_Validate(const DateTime* dt)
{
    if (dt == 0)
    {
        return RTC_STATUS_INVALID_ARGUMENT;
    }
    if ((dt->hour > 23U) || (dt->minute > 59U) || (dt->second > 59U))
    {
        return RTC_STATUS_INVALID_TIME;
    }
    if ((dt->year < DATETIME_YEAR_MIN) || (dt->year > DATETIME_YEAR_MAX) ||
        (dt->month < 1U) || (dt->month > 12U) ||
        (dt->weekday < 1U) || (dt->weekday > 7U))
    {
        return RTC_STATUS_INVALID_DATE;
    }
    if ((dt->day < 1U) || (dt->day > DateTime_DaysInMonth(dt->year, dt->month)))
    {
        return RTC_STATUS_INVALID_DATE;
    }
    return RTC_STATUS_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* DATETIME_H */
