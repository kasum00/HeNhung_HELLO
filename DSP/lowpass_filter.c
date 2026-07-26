/**
 * @file    lowpass_filter.c
 * @brief   Bo loc thap Butterworth bac 2 (IIR) - Direct Form II Transposed.
 *
 * ==========================================================================
 * MUC DICH (WHAT)
 * ==========================================================================
 *   - Loai bo nhieu tan so cao (tren 4 Hz) khoi tin hieu PPG.
 *   - Tin hieu PPG huu ich nam trong khoang 0.5 - 4 Hz (tung do tai).
 *   - Tan so cat fc = 4 Hz, tan so lay mau fs = 100 Hz (PPG_SAMPLE_RATE_HZ).
 *
 * ==========================================================================
 * THUAT TOAN (HOW)
 * ==========================================================================
 *   Bo loc IIR (Infinite Impulse Response) Butterworth bac 2 su dung dang
 *   chuyen doi (transfer function) trong khong gian so (z-domain):
 *
 *            H(z) = (b0 + b1*z^-1 + b2*z^-2) / (1 + a1*z^-1 + a2*z^-2)
 *
 *   Cach hieu: moi mau dau ra y[n] phu thuoc vao:
 *     - 3 mau dau vao hien tai va truoc do:  x[n], x[n-1], x[n-2]
 *     - 2 mau dau ra truoc do:               y[n-1], y[n-2]
 *
 *   Truc quan: Bo loc "nho" gia tri truoc do (feedback tu y), nen khong
 *   bao gio "het hoi ung" (unlimited impulse response) - day la dac diem
 *   cua IIR so voi FIR.
 *
 *   Direct Form II Transposed dung 2 thanh ghi trung gian (w1, w2) thay
 *   vi 4 thanh ghi cua Direct Form thuong. Loi ich:
 *     - Chi can 2 o nho trang thai (RAM toi thieu cho embedded).
 *     - Do ben so (numerical stability) tot hon Direct Form I.
 *     - Moi mau chi can 5 phep nhan + 4 phep cong/tru (O(1)).
 *
 *   Cong thuc Direct Form II Transposed:
 *     y  = b0 * x + w1
 *     w1 = b1 * x - a1 * y + w2
 *     w2 = b2 * x - a2 * y
 *
 *   Trong do w1, w2 la "thu ky noi bo" (internal state), luu tru gia tri
 *   cua cac mau truoc do de tinh mau tiep theo.
 *
 * ==========================================================================
 * HE SO BUTTERWORTH (COEFFICIENTS)
 * ==========================================================================
 *   Tinh bang Python (scipy.signal.butter):
 *     from scipy.signal import butter
 *     b, a = butter(N=2, Wn=4.0/(100.0/2), btype='low')
 *
 *   b = [0.020083, 0.040167, 0.020083]   --> he so tu so (numerator)
 *   a = [1.0,      -1.561018, 0.641352]   --> he so mau so (denominator)
 *
 *   Dac diem Butterworth:
 *     - Phan cuc (maximally flat) trong day dong - khong co rung dong (ripple).
 *     - Phan ung tan so muot hon so voi Chebyshev hoac Elliptic.
 *     - Do tre nhom (group delay) gan nhu hinh do, khong lam bien dang nang.
 *
 * ==========================================================================
 * DO PHUC TAP (COMPLEXITY)
 * ==========================================================================
 *   - Thoi gian: O(1) moi mau (5 nhan + 4 cong/tru).
 *   - Bo nho: 5 he so float + 2 trang thai float = 7 float = 28 byte.
 *   - Khong cap phat bo nho dong (no malloc/new).
 *
 * ==========================================================================
 * NGU DUNG (USAGE CONTEXT)
 * ==========================================================================
 *   - Loc tin hieu PPG truoc khi trich xuat SpO2 hoac BPM.
 *   - Loai bo nhiem dong (powerline 50/60 Hz), nhiem co (muscle artifact).
 *   - Dung trong he thong nhung (embedded), chay tren STM32/ARM Cortex-M.
 *
 * ==========================================================================
 */

#include "lowpass_filter.h"
#include <stddef.h>  /* NULL */

/*
 * ==========================================================================
 * HE SO LOC BUTTERWORTH BAC 2
 * ==========================================================================
 *
 * Cac he so nay duoc tinh tien tuyen (offline) bang cong cu Python
 * va hard-code vao day de tranh tinh toan phuc tap runtime tren MCU.
 *
 * Butterworth bac 2 voi fc = 4 Hz, fs = 100 Hz:
 *   - fp/fs = 4/100 = 0.04 (ty le tan so cat / tan so lay mau)
 *   - Wn = fp / (fs/2) = 4 / 50 = 0.08 (tan so quy hoach, normalized)
 *   - Bac 2 dam bao do doc -20*2 = -40 dB/decade sau tan so cat.
 *     Nghia la tin hieu o 8 Hz bi giam ~12 dB, o 40 Hz bi giam ~36 dB.
 *
 * He so tu so (b0, b1, b2) - dieu khien dau vao:
 *   b0 = b2 = 0.020083: he so doi xung (dac diem Butterworth bac chan)
 *   b1 = 0.040167 = 2 * b0: he so giua, nhan 2 vi symmetry
 *
 * He so mau so (a1, a2) - feedback tu dau ra truoc:
 *   a1 = -1.561018: he so feedback mau dau tien (am vi tu feedback)
 *   a2 = 0.641352: he so feedback mau thu hai (duong, stabilizes filter)
 *   a0 = 1.0 (mac dinh, khong can khai bao)
 */
#define LP_B0  0.020083F
#define LP_B1  0.040167F
#define LP_B2  0.020083F
#define LP_A1 -1.561018F
#define LP_A2  0.641352F

/*
 * ==========================================================================
 * Lowpass_Init - Khoi tao filter voi he so mac dinh
 * ==========================================================================
 *
 * LAM GI (WHAT):
 *   Gan he so Butterworth mac dinh (fc=4Hz, fs=100Hz) vao filter va dat
 *   trang thai ban dau (w1 = w2 = 0).
 *
 * TAI SAO (WHY):
 *   Ham tien ich cho truong hop pho thong - nguoi dung khong can phai
 *   tinh toan he so. Chi can goi Init la co filter san sang dung.
 *   Neu can he so khac (thay doi tan so cat), dung Lowpass_InitCoeffs.
 *
 * CHI TIET (HOW):
 *   - Kiem tra NULL pointer de tranh crash tren embedded (no exception).
 *   - Goi Lowpass_InitCoeffs voi cac he so hard-code tu macro o tren.
 *   - w1 = w2 = 0: filter "sach" - dau ra dau tien se bang 0, sau do
 *     tu dan on dinh khi co du sample dau vao.
 */
void Lowpass_Init(LowpassFilter* f)
{
    if (f == NULL) { return; }
    Lowpass_InitCoeffs(f, LP_B0, LP_B1, LP_B2, LP_A1, LP_A2);
}

/*
 * ==========================================================================
 * Lowpass_InitCoeffs - Khoi tao filter voi he so tuy chinh
 * ==========================================================================
 *
 * LAM GI (WHAT):
 *   Gan 5 he so (b0, b1, b2, a1, a2) va reset trang thai thanh ghi
 *   trung gian w1, w2 ve 0.
 *
 * TAI SAO (WHY):
 *   Cho phep nguoi dung tinh he so filter rieng (butterworth, Chebyshev,
 *   tham chi FIR) va truyen vao day. Vi du:
 *     - Tang tan so cat len 10 Hz de lay tin hieu PPG nhanh hon.
 *     - Giam tan so cat xuong 2 Hz de loc nhieu hon nhung tre hon.
 *     - Dung he so FIR thay vi IIR neu muon do tre nhat dinh.
 *
 * CHI TIET (HOW):
 *   - Gan truc tiep gia tri float vao struct.
 *   - w1 = 0, w2 = 0: xoa "ky uc" cua filter - khong con nho gi ve
 *     cac mau truoc do, dau ra se bat dau tu 0.
 *   - Kiem tra NULL pointer: quy tac an toan cho embedded, bao dam
 *     khong ghi vao dia chi 0x00000000 (HardFault tren ARM Cortex-M).
 */
void Lowpass_InitCoeffs(LowpassFilter* f, float b0, float b1, float b2,
                        float a1, float a2)
{
    if (f == NULL) { return; }
    /* Gan he so tu so (numerator) */
    f->b0 = b0;  f->b1 = b1;  f->b2 = b2;
    /* Gan he so mau so (denominator), a0 = 1.0 nen khong luu */
    f->a1 = a1;  f->a2 = a2;
    /* Reset trang thai trung gian - filter bat dau "quen" */
    f->w1 = 0.0F;
    f->w2 = 0.0F;
}

/*
 * ==========================================================================
 * Lowpass_Reset - Xoa trang thai filter (giu nguyen he so)
 * ==========================================================================
 *
 * LAM GI (WHAT):
 *   Dat w1 = w2 = 0, xoa toan bo "ky uc" cua filter ve cac mau truoc do.
 *
 * TAI SAO (WHY):
 *   Can reset filter khi chuyen doi trang thai, vi du:
 *     - Chuyen tu nguoi dung A sang nguoi dung B (thiet bi y te da dang).
 *     - Phat hien tin hieu khong hop le (artifact qua lon), can bat dau lai.
 *     - Sau khoi dong lai khoang sleep (deep sleep tren STM32).
 *   Khong reset he so vi tinh toan coefficient tốn thoi gian (may ms).
 *
 * CHI TIET (HOW):
 *   - w1 = 0, w2 = 0: sau khi reset, mau dau ra dau tien se = b0 * input
 *     (vi w1, w2 = 0), sau do dan on dinh lai.
 *   - Khong can re-init he so (b0, b1, b2, a1, a2) vi da giu nguyen.
 *   - Thuc chat, sau reset filter se "nhay" mot cai do w2 cu bi xoa,
 *     nhung no se on dinh sau 2-3 mau (do la bac cua filter).
 */
void Lowpass_Reset(LowpassFilter* f)
{
    if (f == NULL) { return; }
    f->w1 = 0.0F;  /* Xoa ky uc mau gan nhat */
    f->w2 = 0.0F;  /* Xoa ky uc mau 2 buoc truoc */
}

/*
 * ==========================================================================
 * Lowpass_Process - Xu ly mot mau tin hieu PPG
 * ==========================================================================
 *
 * LAM GI (WHAT):
 *   Nhan 1 gia tri int32_t tu ADC/PPG, ap dung bo loc Butterworth, tra ve
 *   gia tri da loc (chi con thanh phan tan thap < 4 Hz).
 *
 * TAI SAO (WHY):
 *   Day la ham chinh (core) cua bo loc, goi trong ngat (ISR) hoac vong
 *   lap chinh moi lan lay mau (100 lan/giay voi fs = 100 Hz).
 *   Can thuc hien nhanh (< 1 us tren STM32F4 @ 168 MHz).
 *
 * CHI TIET (HOW) - Direct Form II Transposed:
 *
 *   Buoc 1: Ep kieu int32_t -> float
 *     - ADC tra ve so nguyen (12-bit: 0-4095, hoac 16-bit: 0-65535).
 *     - Filter can lam viec voi so thuc de tinh toan chinh xac.
 *     - Ep kieu chi mat 1 cyckle tren ARM (FPU ho tro VCVT).
 *
 *   Buoc 2: Tinh dau ra y = b0*x + w1
 *     - x la mau dau vao hien tai.
 *     - w1 la "ky uc" tu buoc truoc (chua ket hop cua b1*x - a1*y cu).
 *     - y la gia tri dau ra (da loc).
 *
 *   Buoc 3: Cap nhat trang thai w1 = b1*x - a1*y + w2
 *     - w1 luu thong tin ve mau truoc do: b1*x hien tai + w2 (tu 2 mau truoc).
 *     - Tru a1*y: day la feedback, lam filter "nho" dau ra truoc do.
 *
 *   Buoc 4: Cap nhat trang thai w2 = b2*x - a2*y
 *     - w2 luu thong tin ve 2 mau truoc do.
 *     - a2 y phu thuoc vao mau truoc do (delay 2 mau).
 *
 *   Buoc 5: Ep kieu float -> int32_t va tra ve
 *     - Dau ra PPG la so nguyen (thuong 12-16 bit).
 *     - Ep kieu bang cuong ep (truncate) - khong lam tron (round).
 *     - Vi y da duoc "smoothing" nen ep kieu khong mat nhieu thong tin.
 *
 *   Luu y ve do tre (group delay):
 *     - Butterworth bac 2 co group delay ~2 mau (20 ms tai fs=100 Hz).
 *     - Day la do tre nhat dinh, khong anh huong den tinh toan BPM
 *       (vi BPM dua tren khoang cach giua cac peak, khong phai gia tri tuyet doi).
 *
 *   Tai sao dung int32_t thay vi float?
 *     - ADC va DMA tren STM32 ho tro 12/16-bit integer truc tiep.
 *     - Truyen float qua ngat se tốn them PDU/PPI (cong them chi phi).
 *     - Ep kieu int->float->int chi mat ~2-3 cyckle FPU, rang buoc chap nhan.
 */
int32_t Lowpass_Process(LowpassFilter* f, int32_t input)
{
    if (f == NULL) { return input; }

    /*
     * Direct Form II Transposed:
     * Chi can 2 thanh ghi trang thai (w1, w2) - toi uu RAM cho embedded.
     * Truc quate: y = tong trong luong cua dau vao va "ky uc" truoc do.
     */
    float x = (float)input;                                /* Ep kieu sang so thuc */
    float y = f->b0 * x + f->w1;                          /* Tinh dau ra: y = b0*x + w1 */
    f->w1   = f->b1 * x - f->a1 * y + f->w2;             /* Cap nhat trang thai 1 */
    f->w2   = f->b2 * x - f->a2 * y;                     /* Cap nhat trang thai 2 */

    return (int32_t)y;  /* Ep kieu lai thanh so nguyen cho ADC/DAC */
}
