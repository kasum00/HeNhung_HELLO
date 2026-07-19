/**
 * @file    spo2_estimator.c
 * @brief   Cài đặt estimator SpO2 ratio-of-ratios (cửa sổ trượt sub-block).
 * @note    User-owned. O(1) mỗi sample, không cấp phát.
 */

#include "spo2_estimator.h"
#include <math.h>

static void clearBlock(Spo2Block* b)
{
    b->sumRed = 0;
    b->sumIr = 0;
    b->sumSqRed = 0;
    b->sumSqIr = 0;
    b->count = 0U;
}

void Spo2_Reset(Spo2Estimator* est)
{
    if (est == NULL)
    {
        return;
    }
    for (uint8_t i = 0U; i < (uint8_t)PPG_SPO2_BLOCKS; ++i)
    {
        clearBlock(&est->blocks[i]);
    }
    est->curBlock = 0U;
    est->filledBlocks = 0U;
    est->blockStartMs = 0U;
    est->started = false;
    est->last.spo2 = 0.0F;
    est->last.ratio = 0.0F;
    est->last.dcRed = 0.0F;
    est->last.dcIr = 0.0F;
    est->last.acRed = 0.0F;
    est->last.acIr = 0.0F;
    est->last.valid = false;
}

void Spo2_Init(Spo2Estimator* est, const Spo2Calibration* cal)
{
    if (est == NULL)
    {
        return;
    }
    if (cal != NULL)
    {
        est->cal = *cal;
    }
    else
    {
        est->cal.coefficientA = PPG_SPO2_CAL_A;
        est->cal.coefficientB = PPG_SPO2_CAL_B;
        est->cal.coefficientC = PPG_SPO2_CAL_C;
    }
    Spo2_Reset(est);
}

/** RMS quanh trung bình từ running sum: sqrt(max(0, E[x^2] - E[x]^2)). */
static double rmsFromSums(int64_t sum, int64_t sumSq, uint32_t n)
{
    if (n == 0U)
    {
        return 0.0;
    }
    const double mean = (double)sum / (double)n;
    double var = ((double)sumSq / (double)n) - (mean * mean);
    if (var < 0.0) { var = 0.0; }   /* chặn giá trị âm nhỏ do làm tròn */
    return sqrt(var);
}

/** Tính SpO2 trên cả cửa sổ; điền @p out và trả về status. */
static Spo2Status computeWindow(Spo2Estimator* est, Spo2Result* out)
{
    int64_t sumRed = 0;
    int64_t sumIr = 0;
    int64_t sumSqRed = 0;
    int64_t sumSqIr = 0;
    uint32_t n = 0U;
    for (uint8_t i = 0U; i < (uint8_t)PPG_SPO2_BLOCKS; ++i)
    {
        sumRed += est->blocks[i].sumRed;
        sumIr += est->blocks[i].sumIr;
        sumSqRed += est->blocks[i].sumSqRed;
        sumSqIr += est->blocks[i].sumSqIr;
        n += est->blocks[i].count;
    }

    Spo2Result r;
    r.dcRed = (n > 0U) ? (float)((double)sumRed / (double)n) : 0.0F;
    r.dcIr = (n > 0U) ? (float)((double)sumIr / (double)n) : 0.0F;
    r.acRed = (float)rmsFromSums(sumRed, sumSqRed, n);
    r.acIr = (float)rmsFromSums(sumIr, sumSqIr, n);
    r.ratio = 0.0F;
    r.spo2 = 0.0F;
    r.valid = false;

    /* Tính hợp lệ của tín hiệu: đủ sample, DC trong dải, AC đủ lớn, không bão
       hòa, và mẫu số không suy biến (không bao giờ chia cho ~0). */
    const bool enough = (n >= (uint32_t)PPG_SPO2_MIN_SAMPLES);
    const bool dcOk = (r.dcRed > (float)PPG_SPO2_MIN_DC) && (r.dcIr > (float)PPG_SPO2_MIN_DC);
    const bool notSat = (r.dcRed < (float)PPG_SPO2_SATURATION) && (r.dcIr < (float)PPG_SPO2_SATURATION);
    const bool acOk = (r.acRed >= PPG_SPO2_MIN_AC) && (r.acIr >= PPG_SPO2_MIN_AC);

    if (enough && dcOk && notSat && acOk)
    {
        const float redRatio = r.acRed / r.dcRed;
        const float irRatio = r.acIr / r.dcIr;
        if (irRatio > 1e-6F)
        {
            const float R = redRatio / irRatio;
            r.ratio = R;
            const float spo2 = est->cal.coefficientA + (est->cal.coefficientB * R) +
                               (est->cal.coefficientC * R * R);
            /* Kiểm tra dải sinh lý; KHÔNG clamp một giá trị sai thành hợp lệ.
               Chỉ clamp con số hiển thị khi nó đã nằm trong dải hợp lý. */
            if ((spo2 >= PPG_SPO2_MIN_PERCENT) && (spo2 <= PPG_SPO2_MAX_PERCENT))
            {
                r.spo2 = spo2;
                r.valid = true;
            }
        }
    }

    *out = r;
    est->last = r;
    return r.valid ? SPO2_STATUS_OK : SPO2_STATUS_INVALID_SIGNAL;
}

Spo2Status Spo2_Process(Spo2Estimator* est, uint32_t redRaw, uint32_t irRaw,
                        uint32_t timestampMs, Spo2Result* out)
{
    if ((est == NULL) || (out == NULL))
    {
        return SPO2_STATUS_NOT_READY;
    }

    if (!est->started)
    {
        est->started = true;
        est->blockStartMs = timestampMs;
    }

    Spo2Block* b = &est->blocks[est->curBlock];
    b->sumRed += (int64_t)redRaw;
    b->sumIr += (int64_t)irRaw;
    b->sumSqRed += (int64_t)redRaw * (int64_t)redRaw;
    b->sumSqIr += (int64_t)irRaw * (int64_t)irRaw;
    ++b->count;

    /* Đã tới ranh giới block chưa? */
    if ((timestampMs - est->blockStartMs) >= (uint32_t)PPG_SPO2_UPDATE_MS)
    {
        Spo2Status status = SPO2_STATUS_NOT_READY;
        if (est->filledBlocks < (uint8_t)PPG_SPO2_BLOCKS)
        {
            ++est->filledBlocks;
        }
        if (est->filledBlocks >= (uint8_t)PPG_SPO2_BLOCKS)
        {
            status = computeWindow(est, out);
        }

        /* Chuyển sang block kế tiếp (cũ nhất) và bắt đầu lại từ đầu. */
        est->curBlock = (uint8_t)((est->curBlock + 1U) % (uint8_t)PPG_SPO2_BLOCKS);
        clearBlock(&est->blocks[est->curBlock]);
        est->blockStartMs = timestampMs;
        return status;
    }

    return SPO2_STATUS_NOT_READY;
}
