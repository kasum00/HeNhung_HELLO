#include "lowpass_filter.h"
#include <stddef.h>  /* NULL */

/*
 * Hệ số Butterworth bậc 2, tần số cắt fc = 4 Hz, sample rate fs = 100 Hz.
 *
 * Tính bằng Python:
 *   from scipy.signal import butter
 *   b, a = butter(N=2, Wn=4.0/(100.0/2), btype='low')
 *   # b = [0.020083, 0.040167, 0.020083]
 *   # a = [1.0, -1.561018, 0.641352]
 */
#define LP_B0  0.020083F
#define LP_B1  0.040167F
#define LP_B2  0.020083F
#define LP_A1 -1.561018F
#define LP_A2  0.641352F

void Lowpass_Init(LowpassFilter* f)
{
    if (f == NULL) { return; }
    Lowpass_InitCoeffs(f, LP_B0, LP_B1, LP_B2, LP_A1, LP_A2);
}

void Lowpass_InitCoeffs(LowpassFilter* f, float b0, float b1, float b2,
                        float a1, float a2)
{
    if (f == NULL) { return; }
    f->b0 = b0;  f->b1 = b1;  f->b2 = b2;
    f->a1 = a1;  f->a2 = a2;
    f->w1 = 0.0F;
    f->w2 = 0.0F;
}

void Lowpass_Reset(LowpassFilter* f)
{
    if (f == NULL) { return; }
    f->w1 = 0.0F;
    f->w2 = 0.0F;
}

int32_t Lowpass_Process(LowpassFilter* f, int32_t input)
{
    if (f == NULL) { return input; }

    /* Direct Form II Transposed (2 phép nhớ, tối ưu cho embedded) */
    float x = (float)input;
    float y = f->b0 * x + f->w1;
    f->w1   = f->b1 * x - f->a1 * y + f->w2;
    f->w2   = f->b2 * x - f->a2 * y;

    return (int32_t)y;
}
