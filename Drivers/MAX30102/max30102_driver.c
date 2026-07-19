/**
 * @file    max30102_driver.c
 * @brief   Cài đặt driver MAX30102 (I2C polling).
 * @note    User-owned (ngoài các thư mục generated).
 */

#include "max30102_driver.h"
#include "hw_config.h"

/* -------------------------------------------------------------------------- */
/* Bản đồ thanh ghi                                                            */
/* -------------------------------------------------------------------------- */
#define REG_INT_STATUS_1   0x00U
#define REG_FIFO_WR_PTR    0x04U
#define REG_OVF_COUNTER    0x05U
#define REG_FIFO_RD_PTR    0x06U
#define REG_FIFO_DATA      0x07U
#define REG_FIFO_CONFIG    0x08U
#define REG_MODE_CONFIG    0x09U
#define REG_SPO2_CONFIG    0x0AU
#define REG_LED1_PA        0x0CU  /* RED */
#define REG_LED2_PA        0x0DU  /* IR  */
#define REG_PART_ID        0xFFU

#define MODE_RESET_BIT     0x40U
#define MODE_SPO2          0x03U  /* RED + IR */

#define BYTES_PER_SAMPLE   6U     /* 3 bytes RED + 3 bytes IR */
#define FIFO_DEPTH         32U
#define SAMPLE_MASK_18BIT  0x0003FFFFUL

/* -------------------------------------------------------------------------- */
/* Trạng thái                                                                  */
/* -------------------------------------------------------------------------- */
static I2C_HandleTypeDef* s_hi2c = NULL;

static Max30102Status readReg(uint8_t reg, uint8_t* data, uint16_t len)
{
    if (s_hi2c == NULL)
    {
        return MAX30102_ERR_NOT_INIT;
    }
    const HAL_StatusTypeDef st = HAL_I2C_Mem_Read(
        s_hi2c, MAX30102_I2C_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT,
        data, len, HW_I2C_TIMEOUT_MS);
    return (st == HAL_OK) ? MAX30102_OK : MAX30102_ERR_I2C;
}

static Max30102Status writeReg(uint8_t reg, uint8_t value)
{
    if (s_hi2c == NULL)
    {
        return MAX30102_ERR_NOT_INIT;
    }
    const HAL_StatusTypeDef st = HAL_I2C_Mem_Write(
        s_hi2c, MAX30102_I2C_ADDRESS, reg, I2C_MEMADD_SIZE_8BIT,
        &value, 1U, HW_I2C_TIMEOUT_MS);
    return (st == HAL_OK) ? MAX30102_OK : MAX30102_ERR_I2C;
}

Max30102Status MAX30102_ReadPartId(uint8_t* partId)
{
    if (partId == NULL)
    {
        return MAX30102_ERR_ARG;
    }
    return readReg(REG_PART_ID, partId, 1U);
}

Max30102Status MAX30102_Reset(void)
{
    Max30102Status s = writeReg(REG_MODE_CONFIG, MODE_RESET_BIT);
    if (s != MAX30102_OK)
    {
        return s;
    }
    /* Chờ bit reset tự xóa (có giới hạn, thân thiện non-blocking). */
    for (uint8_t i = 0U; i < 20U; ++i)
    {
        uint8_t mode = 0U;
        s = readReg(REG_MODE_CONFIG, &mode, 1U);
        if (s != MAX30102_OK)
        {
            return s;
        }
        if ((mode & MODE_RESET_BIT) == 0U)
        {
            return MAX30102_OK;
        }
        HAL_Delay(1U);
    }
    return MAX30102_ERR_I2C;
}

Max30102Status MAX30102_Init(I2C_HandleTypeDef* hi2c)
{
    if (hi2c == NULL)
    {
        return MAX30102_ERR_ARG;
    }
    s_hi2c = hi2c;

    if (HAL_I2C_IsDeviceReady(s_hi2c, MAX30102_I2C_ADDRESS, 3U, HW_I2C_TIMEOUT_MS) != HAL_OK)
    {
        return MAX30102_ERR_I2C;
    }

    uint8_t partId = 0U;
    Max30102Status s = MAX30102_ReadPartId(&partId);
    if (s != MAX30102_OK)
    {
        return s;
    }
    if (partId != MAX30102_PART_ID)
    {
        return MAX30102_ERR_ID;
    }

    s = MAX30102_Reset();
    if (s != MAX30102_OK)
    {
        return s;
    }

    /* Xóa các con trỏ FIFO. */
    (void)writeReg(REG_FIFO_WR_PTR, 0x00U);
    (void)writeReg(REG_OVF_COUNTER, 0x00U);
    (void)writeReg(REG_FIFO_RD_PTR, 0x00U);

    /* FIFO config 0x0F: SMP_AVE = 1 (không trung bình -> giữ RAW sample,
       bits[7:5]=000), bật FIFO rollover (bit4=1), FIFO-almost-full = còn 15
       sample (bits[3:0]=1111). Cố ý TẮT trung bình để engine đo nhận dữ liệu RAW
       không bị sửa. */
    (void)writeReg(REG_FIFO_CONFIG, 0x0FU);
    /* SpO2: dải ADC 4096nA, tốc độ lấy mẫu 100 Hz, độ rộng xung 411us (18-bit). */
    (void)writeReg(REG_SPO2_CONFIG, 0x27U);
    /* Dòng LED ~6.4 mA mỗi kênh (0x24). */
    (void)writeReg(REG_LED1_PA, 0x24U);
    (void)writeReg(REG_LED2_PA, 0x24U);
    /* Vào SpO2 mode (RED + IR). */
    s = writeReg(REG_MODE_CONFIG, MODE_SPO2);

    return s;
}

Max30102Status MAX30102_GetFifoSampleCount(uint8_t* count)
{
    if (count == NULL)
    {
        return MAX30102_ERR_ARG;
    }
    uint8_t wr = 0U;
    uint8_t rd = 0U;
    Max30102Status s = readReg(REG_FIFO_WR_PTR, &wr, 1U);
    if (s != MAX30102_OK)
    {
        return s;
    }
    s = readReg(REG_FIFO_RD_PTR, &rd, 1U);
    if (s != MAX30102_OK)
    {
        return s;
    }
    *count = (uint8_t)((wr - rd) & (FIFO_DEPTH - 1U));
    return MAX30102_OK;
}

Max30102Status MAX30102_ReadFifo(Max30102Sample* samples, uint8_t maxSamples, uint8_t* readCount)
{
    if ((samples == NULL) || (readCount == NULL) || (maxSamples == 0U))
    {
        return MAX30102_ERR_ARG;
    }
    *readCount = 0U;

    uint8_t pending = 0U;
    Max30102Status s = MAX30102_GetFifoSampleCount(&pending);
    if (s != MAX30102_OK)
    {
        return s;
    }
    if (pending > maxSamples)
    {
        pending = maxSamples;
    }

    for (uint8_t i = 0U; i < pending; ++i)
    {
        uint8_t raw[BYTES_PER_SAMPLE] = {0};
        s = readReg(REG_FIFO_DATA, raw, BYTES_PER_SAMPLE);
        if (s != MAX30102_OK)
        {
            return s;
        }
        const uint32_t red = (((uint32_t)raw[0] << 16) | ((uint32_t)raw[1] << 8) |
                              (uint32_t)raw[2]) & SAMPLE_MASK_18BIT;
        const uint32_t ir  = (((uint32_t)raw[3] << 16) | ((uint32_t)raw[4] << 8) |
                              (uint32_t)raw[5]) & SAMPLE_MASK_18BIT;
        samples[i].red = red;
        samples[i].ir = ir;
    }
    *readCount = pending;
    return MAX30102_OK;
}

Max30102Status MAX30102_ReadOverflowCounter(uint8_t* overflow)
{
    if (overflow == NULL)
    {
        return MAX30102_ERR_ARG;
    }
    return readReg(REG_OVF_COUNTER, overflow, 1U);
}
