/**
 * @file    rtc_service.c
 * @brief   Cài đặt RTC service (đọc/công bố DS1307 + yêu cầu cài giờ).
 * @note    User-owned. Target-only. Snapshot double-buffer cho an toàn SPSC.
 */

#include "rtc_service.h"
#include "ds1307_driver.h"

typedef struct
{
    DateTime dt;
    bool valid;
} RtcSnapshot;

static RtcSnapshot s_buf[2];
static volatile uint8_t s_active;

static DateTime s_setRequest;
static volatile bool s_setPending;
static volatile RtcStatus s_setResult = RTC_STATUS_OK;
static volatile uint32_t s_setGeneration;

RtcStatus RtcService_Init(I2C_HandleTypeDef* hi2c)
{
    s_active = 0U;
    s_buf[0].valid = false;
    s_buf[1].valid = false;
    s_setPending = false;
    s_setGeneration = 0U;

    RtcStatus s = DS1307_Init(hi2c);
    if (s == RTC_STATUS_OK)
    {
        RtcService_Poll();
    }
    return s;
}

void RtcService_Poll(void)
{
    DateTime dt;
    const RtcStatus s = DS1307_ReadDateTime(&dt);
    const uint8_t next = (uint8_t)(s_active ^ 1U);
    s_buf[next].dt = dt;
    s_buf[next].valid = (s == RTC_STATUS_OK);   /* dừng/lỗi -> không hợp lệ */
    s_active = next;                             /* publish */
}

void RtcService_ProcessPendingSet(void)
{
    if (!s_setPending)
    {
        return;
    }
    DateTime req = s_setRequest;
    const RtcStatus r = DS1307_SetDateTime(&req);
    s_setResult = r;
    ++s_setGeneration;
    s_setPending = false;
    if (r == RTC_STATUS_OK)
    {
        RtcService_Poll();                       /* làm mới thời gian công bố */
    }
}

void RtcService_GetSnapshot(DateTime* dateTime, bool* valid)
{
    const uint8_t idx = s_active;
    if (dateTime != 0)
    {
        *dateTime = s_buf[idx].dt;
    }
    if (valid != 0)
    {
        *valid = s_buf[idx].valid;
    }
}

void RtcService_RequestSet(const DateTime* dateTime)
{
    if (dateTime == 0)
    {
        return;
    }
    s_setRequest = *dateTime;
    s_setPending = true;                          /* publish sau dữ liệu */
}

void RtcService_GetLastSetResult(RtcStatus* status, uint32_t* generation)
{
    if (status != 0)
    {
        *status = s_setResult;
    }
    if (generation != 0)
    {
        *generation = s_setGeneration;
    }
}
