/**
 * @file    spo2_estimator.c
 * @brief   Cai dat estimator SpO2 ratio-of-ratios (cua so truot sub-block).
 *
 * ============================================================================
 * TONG QUAN THUAT TOAN (Ratio-of-Ratios)
 * ============================================================================
 *
 * SpO2 (do bao hoa oxy trong mau) duoc uoc luong bang cach so sanh ty le
 * "ac-to-dc" cua hai nguon anh sang khac nhau:
 *   - RED  (~660 nm): oxy-hemoglobin (HbO2) hap thu nhieu, deoxy-Hb hap thu it.
 *   - IR   (~940 nm): nguoc lai, deoxy-Hb hap thu nhieu hon.
 *
 *   R = (AC_RED / DC_RED) / (AC_IR / DC_IR)
 *
 * Trong do:
 *   - DC = gia tri trung binh (thanh phan khong doi) cua tin hieu PPG.
 *          Day la "nen" do luong mau chieu qua mo/cu.
 *   - AC = bien do rung (thanh phan dong) phan anh nhip tim.
 *          AC duoc tinh bang RMS (root-mean-square) quanh trung binh.
 *
 * Gia tri R roi duoc dua vao da thuc calibration:
 *   SpO2 = A + B*R + C*R^2
 *
 * voi A, B, C la cac he so thuc nghiem (tuong ung voi duong cong Maxim
 * MAX30102). Gia tri SpO2 nam trong khoang 70% - 100% duoc coi la hop ly
 * ve mat sinh ly.
 *
 * ============================================================================
 * CAI DAT - CUA SO TRUOT SUB-BLOCK
 * ============================================================================
 *
 * De tinh R can mot cua so du mau (PPG_SPO2_BLOCKS block, moi block
 * PPG_SPO2_UPDATE_MS ms = khoang 4 giay tong cong). Thay vi luu tat ca
 * mau vao bo nho (tiet kiem RAM), moi block chi luu cac tong chay (running
 * sum):
 *   sumRed, sumIr       -> de tinh DC = sum / n
 *   sumSqRed, sumSqIr   -> de tinh RMS = sqrt(E[x^2] - E[x]^2)
 *   count               -> so mau trong block
 *
 * Khi mot block moi hoan tat, no "lan chen" block cu nhat (circular buffer).
 * Khi du BLOCKS block, toan bo cua so duoc tinh toan va tra ve ket qua.
 * Moi sample chi can O(1) thoi gian va O(1) bo nho - phu hop cho MCU.
 *
 * ============================================================================
 * LUU Y QUAN TRONG
 * ============================================================================
 * Day KHONG phai thiet bi y te. Calibration la thuc nghiem. Caller can them
 * kiem tra chat luong tin hieu (SQI), su hien dien ngon tay, va bao hoa
 * truoc khi hien thi gia tri cho nguoi dung.
 * @note    User-owned. O(1) moi sample, khong cap phat.
 */

#include "spo2_estimator.h"
#include <math.h>

/* ============================================================================
 * clearBlock - Xoa du lieu cua mot sub-block
 * ============================================================================
 * Tai sao: Khi mot block cu bi "lan chen" boi block moi (circular buffer),
 * ta can reset tat ca accumulator ve 0 de bat dau tinh tu dau. Cung dung
 * khi Reset toan bo estimator.
 *
 * Cach hoat dong: Gan gia tri 0 cho moi truong trong struct Spo2Block.
 * Khong cap phat bo nho, chi don gian xoa du lieu.
 * ========================================================================= */
static void clearBlock(Spo2Block* b)
{
    b->sumRed = 0;       // Tong gia tri RED tat ca mau trong block
    b->sumIr = 0;        // Tong gia tri IR tat ca mau trong block
    b->sumSqRed = 0;     // Tong binh phuong RED -> dung de tinh variance/RMS
    b->sumSqIr = 0;      // Tong binh phuong IR  -> dung de tinh variance/RMS
    b->count = 0U;       // So mau da duoc nap vao block nay
}

/* ============================================================================
 * Spo2_Reset - Xoa toan bo trang thai estimator
 * ============================================================================
 * Tai sao: Can reset khi bat dau phien do moi, khi nhac ngon tay khoi cam
 * bien, hoac khi du lieu cu bi nhiem/nghi ngo. Ham nay xoa sach tat ca
 * accumulator, dua con tro block ve vi tri ban dau, va danh dau ket qua
 * cu khong hop le.
 *
 * Cach hoat dong:
 *   1. Duyet va xoa tung block trong mang blocks[].
 *   2. Reset chi so block hien tai (curBlock) ve 0.
 *   3. Reset so block da day (filledBlocks) ve 0 - cua so chua du mau.
 *   4. Reset thoi diem bat dau block hien tai.
 *   5. Xoa ket qua cuoi cung (last) va danh dau invalid.
 * ========================================================================= */
void Spo2_Reset(Spo2Estimator* est)
{
    /* Kiem tra null de tranh crash tren MCU - thiet ke defensive */
    if (est == NULL)
    {
        return;
    }

    /* Xoa tung sub-block: moi block la mot "phay" nho cua cua so truot */
    for (uint8_t i = 0U; i < (uint8_t)PPG_SPO2_BLOCKS; ++i)
    {
        clearBlock(&est->blocks[i]);
    }

    est->curBlock = 0U;       // Con tro block hien tai (circular buffer)
    est->filledBlocks = 0U;   // Chua co block nao day -> cua so chua du du lieu
    est->blockStartMs = 0U;   // Thoi diem bat dau block hien tai (ms)
    est->started = false;     // Chua bat dau nhan mau

    /* Xoa ket qua cuoi cung - danh dau la khong hop le de caller biet */
    est->last.spo2 = 0.0F;
    est->last.ratio = 0.0F;
    est->last.dcRed = 0.0F;
    est->last.dcIr = 0.0F;
    est->last.acRed = 0.0F;
    est->last.acIr = 0.0F;
    est->last.valid = false;
}

/* ============================================================================
 * Spo2_Init - Khoi tao estimator voi calibration
 * ============================================================================
 * Tai sao: Moi module cam bien MAX30102 co dac trung khac nhau (do nhiet do,
 * do sang LED, chat lieu da...). Calibration A/B/C la he so thuc nghiem phu
 * hop voi module cu the. Neu khong co calibration tuy chinh, su dung gia tri
 * mac dinh tu ppg_config.h (duoc tinh tu thuc nghiem tren module MAX30102).
 *
 * Cach hoat dong:
 *   1. Kiem tra null.
 *   2. Neu co calibration tuy chinh -> sao chep vao est.
 *      Neu khong (NULL) -> dung gia tri mac dinh PPG_SPO2_CAL_A/B/C.
 *   3. Goi Reset de xoa sach toan bo trang thai.
 * ========================================================================= */
void Spo2_Init(Spo2Estimator* est, const Spo2Calibration* cal)
{
    if (est == NULL)
    {
        return;
    }

    if (cal != NULL)
    {
        /* Su dung calibration tuy chinh cua nguoi dung */
        est->cal = *cal;
    }
    else
    {
        /* Dung he so mac dinh: SpO2 = 94.845 + 30.354*R - 45.060*R^2
         * Day la da thuc calibration thuc nghiem cho MAX30102. */
        est->cal.coefficientA = PPG_SPO2_CAL_A;
        est->cal.coefficientB = PPG_SPO2_CAL_B;
        est->cal.coefficientC = PPG_SPO2_CAL_C;
    }

    /* Sau khi thiet lap calibration, xoa sach toan bo trang thai accumulator */
    Spo2_Reset(est);
}

/* ============================================================================
 * rmsFromSums - Tinh RMS quanh trung binh tu running sum
 * ============================================================================
 * THUAT TOAN:
 *   RMS = sqrt( E[x^2] - E[x]^2 ) = sqrt( Var(x) )
 *
 * Trong do:
 *   E[x]   = sum / n      (gia tri trung binh / DC)
 *   E[x^2] = sumSq / n   (gia tri trung binh binh phuong)
 *
 *   Var(x) = E[x^2] - E[x]^2  (phuong sai)
 *   RMS    = sqrt(Var(x))      (can bac hai phuong sai)
 *
 * Tai sao dung cong thuc nay thay vi tinh lai tu dau?
 *   - Chi can 3 so luong da luu tru: sum, sumSq, n (O(1) bo nho).
 *   - Khong can luu tung sample vao mang (tiet kiem RAM tren MCU).
 *   - Phuong sai co the am nho do lam tron so thuc -> can clamp ve 0.
 *
 * Tham so:
 *   sum    = tong gia tri tat ca mau trong cua so (int64 de tranh so lan)
 *   sumSq  = tong binh phuong gia tri tat ca mau
 *   n      = so mau
 * Tra ve: Gia tri RMS (double de giam sai so lam tron)
 * ========================================================================= */
static double rmsFromSums(int64_t sum, int64_t sumSq, uint32_t n)
{
    if (n == 0U)
    {
        return 0.0;  /* Tranh chia cho 0 khi chua co mau nao */
    }

    /* Tinh trung binh (DC component) */
    const double mean = (double)sum / (double)n;

    /* Tinh phuong sai: Var = E[x^2] - (E[x])^2
     * Day la cong thuc "one-pass" - chi can 1 lan duyet de tinh RMS */
    double var = ((double)sumSq / (double)n) - (mean * mean);

    /* Chan gia tri am nho do sai so lam tron so thuc (floating-point error).
     * Vi phuong sai that khong the am, gia tri am nho la artifact cua phep tinh. */
    if (var < 0.0) { var = 0.0; }

    return sqrt(var);  /* RMS = can bac hai phuong sai */
}

/* ============================================================================
 * computeWindow - Tinh SpO2 tren toan bo cua so
 * ============================================================================
 * THUAT TOAN:
 *   1. Tong hop du lieu tu TAT CA cac sub-block trong cua so (4 block).
 *   2. Tinh DC = tong_gia_tri / so_mau (trung binh cong / mean).
 *   3. Tinh AC = RMS quanh trung binh (bien do rung cua nhip tim).
 *   4. Tinh ty le: redRatio = AC_RED/DC_RED, irRatio = AC_IR/DC_IR.
 *   5. Tinh R = redRatio / irRatio (ratio of ratios).
 *   6. Ap dung da thuc calibration: SpO2 = A + B*R + C*R^2.
 *   7. Kiem tra ket qua nam trong dai sinh ly (70%-100%).
 *
 * Tai sao can nhieu buoc kiem tra?
 *   - Tin hieu kem chat luong (DC qua thap, AC qua nho, bao hoa) se cho
 *     ket qua SpO2 sai lam. Can loai bo truoc khi hien thi.
 *   - Mau so nho (irRatio ~ 0) co the gay tran so hoac sai lam lon.
 *   - Gia tri ngoai dai sinh ly ( < 70% hoac > 100%) khong co y nghia y te.
 *
 * Tham so:
 *   est  -> estimator chua du lieu accumulator
 *   out  -> con tro nhan ket qua moi
 * Tra ve: SPO2_STATUS_OK neu hop le, SPO2_STATUS_INVALID_SIGNAL neu khong.
 * ========================================================================= */
static Spo2Status computeWindow(Spo2Estimator* est, Spo2Result* out)
{
    /* ---- Buoc 1: Tong hop du lieu tu tat ca sub-block ----
     * Moi block la mot "phay" thoi gian 1 giay. Khi du 4 block, ta co
     * du lieu 4 giay de tinh toan. Cong dong cua tat ca accumulator. */
    int64_t sumRed = 0;
    int64_t sumIr = 0;
    int64_t sumSqRed = 0;
    int64_t sumSqIr = 0;
    uint32_t n = 0U;

    for (uint8_t i = 0U; i < (uint8_t)PPG_SPO2_BLOCKS; ++i)
    {
        sumRed   += est->blocks[i].sumRed;    // Tong RED cua toan cua so
        sumIr    += est->blocks[i].sumIr;     // Tong IR cua toan cua so
        sumSqRed += est->blocks[i].sumSqRed;  // Tong binh phuong RED
        sumSqIr  += est->blocks[i].sumSqIr;   // Tong binh phuong IR
        n        += est->blocks[i].count;      // Tong so mau
    }

    /* ---- Buoc 2 & 3: Tinh DC va AC cho RED va IR ----
     * DC = trung binh cong (mean) -> dai dien cho thanh phan "nen" cua tin hieu.
     * AC = RMS quanh trung binh -> dai dien cho bien do rung phan anh nhip tim.
     * AC/DC la ty le khong don vi, khong phu thuoc vao do sang LED hay
     * toc do lay mau, chi phu thuoc vao ty le hap thu qua mau. */
    Spo2Result r;
    r.dcRed = (n > 0U) ? (float)((double)sumRed / (double)n) : 0.0F;
    r.dcIr  = (n > 0U) ? (float)((double)sumIr / (double)n) : 0.0F;
    r.acRed = (float)rmsFromSums(sumRed, sumSqRed, n);
    r.acIr  = (float)rmsFromSums(sumIr, sumSqIr, n);

    /* Khoi tao gia tri mac dinh: ratio va SpO2 = 0, khong hop le */
    r.ratio = 0.0F;
    r.spo2 = 0.0F;
    r.valid = false;

    /* ---- Buoc 4: Kiem tra tinh hop le cua tin hieu ----
     * Tat ca cac dieu kien duoi phai THOA MAN cung luc de tinh SpO2:
     *
     * (a) enough: Du mau trong cua so (>= PPG_SPO2_MIN_SAMPLES = 200).
     *     Thieu mau -> RMS chua on dinh, ket qua khong dang tin cay.
     *
     * (b) dcOk: DC RED va DC IR deu du lon (> PPG_SPO2_MIN_DC = 40000).
     *     DC thap co nghia khong co ngon tay hoac tin hieu qua yeu.
     *
     * (c) notSat: DC khong vuot muc bao hoa (< PPG_SPO2_SATURATION = 255000).
     *     DC qua cao nghia la cam bien bi bao hoa -> tin hieu bi dut.
     *
     * (d) acOk: AC RED va AC IR deu du lon (>= PPG_SPO2_MIN_AC = 15).
     *     AC nho nghia la khong co nhip tim hoac tin hieu bi nhiem.
     */
    const bool enough = (n >= (uint32_t)PPG_SPO2_MIN_SAMPLES);
    const bool dcOk   = (r.dcRed > (float)PPG_SPO2_MIN_DC) &&
                        (r.dcIr > (float)PPG_SPO2_MIN_DC);
    const bool notSat = (r.dcRed < (float)PPG_SPO2_SATURATION) &&
                        (r.dcIr < (float)PPG_SPO2_SATURATION);
    const bool acOk   = (r.acRed >= PPG_SPO2_MIN_AC) &&
                        (r.acIr >= PPG_SPO2_MIN_AC);

    /* ---- Buoc 5: Tinh Ratio of Ratios (R) ----
     * Chi tinh khi TAT CA cac dieu kien tren deu thoa man. */
    if (enough && dcOk && notSat && acOk)
    {
        /* Ty le AC/DC cho tung nguon anh sang:
         *   redRatio = AC_RED / DC_RED
         *   irRatio  = AC_IR  / DC_IR
         * Ty le nay khong don vi va phu thuoc vao ti le hap thu cua HbO2 vs Hb. */
        const float redRatio = r.acRed / r.dcRed;
        const float irRatio  = r.acIr / r.dcIr;

        /* Kiem tra mau so khong qua nho de tranh tran so / ket qua vo nghia.
         * 1e-6F la nguong "an toan" cho so float. */
        if (irRatio > 1e-6F)
        {
            /* R = (AC_RED/DC_RED) / (AC_IR/DC_IR) - "ratio of ratios".
             * Day la gia tri trung gian quan trong nhat:
             *   - R cao -> RED bi hap thu nhieu hon IR -> SpO2 thap
             *   - R thap -> RED bi hap thu it hon IR -> SpO2 cao */
            const float R = redRatio / irRatio;
            r.ratio = R;

            /* ---- Buoc 6: Ap dung da thuc calibration ----
             * SpO2 = A + B*R + C*R^2
             *   A = 94.845 (he so hang - intercept)
             *   B = 30.354 (he so bac 1 - do nhay voi R)
             *   C = -45.060 (he so bac 2 - khong bac)
             * Day la da thuc bac 2 thuc nghiem phu hop voi MAX30102. */
            const float spo2 = est->cal.coefficientA +
                               (est->cal.coefficientB * R) +
                               (est->cal.coefficientC * R * R);

            /* ---- Buoc 7: Kiem tra dai sinh ly ----
             * SpO2 thuc te nam trong khoang 70%-100%.
             *   - < 70%: Co the la loi do tin hieu kem chat luong, KHONG phai
             *     SpO2 that. Khong clamp ve 70% vi dieu do an di tin hieu sai.
             *   - > 100%: Vo ly sinh ly, cung co the la loi tin hieu.
             * Chi tra ve gia tri khi nam trong dai hop ly. */
            if ((spo2 >= PPG_SPO2_MIN_PERCENT) && (spo2 <= PPG_SPO2_MAX_PERCENT))
            {
                r.spo2 = spo2;
                r.valid = true;
            }
        }
    }

    /* Luu ket qua vao est de caller co the truy xuat (cho muc dich hien thi
     * hoac ghi log) du chi so khong hop le. */
    *out = r;
    est->last = r;

    /* Tra ve status tuong ung: OK neu hop le, INVALID_SIGNAL neu khong */
    return r.valid ? SPO2_STATUS_OK : SPO2_STATUS_INVALID_SIGNAL;
}

/* ============================================================================
 * Spo2_Process - Xu ly mot mau RED/IR va co the tinh SpO2 moi
 * ============================================================================
 * DAY LA HAM CHINH - goi moi lan lay duoc mau tu MAX30102.
 *
 * THUAT TOAN:
 *   1. Nap gia tri RED va IR vao block hien tai (cong vao running sum).
 *   2. Kiem tra neu da het thoi gian block (>= PPG_SPO2_UPDATE_MS = 1000ms):
 *      a. Tang so block da day (toi da PPG_SPO2_BLOCKS = 4).
 *      b. Neu du 4 block -> goi computeWindow de tinh SpO2.
 *      c. Chuyen sang block moi (circular buffer), bat dau tinh lai tu dau.
 *   3. Tra ve status tuong ung.
 *
 * Tai sao dung circular buffer?
 *   - Khi day 4 block, block cu nhat bi "lan chen" boi block moi.
 *   - Khong can dich mang (O(n)) -> O(1) cho moi sample.
 *   - Chi can PPG_SPO2_BLOCKS = 4 struct Spo2Block -> rat it RAM.
 *
 * Tai sao dung running sum thay vi luu mang mau?
 *   - Tiet kiem RAM: chi can 5 so (sumRed, sumIr, sumSqRed, sumSqIr, count)
 *     moi block, thay vi luu hang tram mau.
 *   - Tinh DC va RMS chi can 3 phep tinh: sum/n, sumSq/n, sqrt().
 *   - Phu hop cho MCU co RAM han che.
 *
 * Tham so:
 *   est         -> estimator (trang thai noi bo)
 *   redRaw      -> gia tri RAW RED tu ADC (18-bit, 0..262143)
 *   irRaw       -> gia tri RAW IR tu ADC
 *   timestampMs -> thoi diem lay mau (millisecond)
 *   out         -> nhan ket qua khi mot block moi hoan tat
 * Tra ve:
 *   SPO2_STATUS_NOT_READY      -> chua du du lieu (cua so dang day)
 *   SPO2_STATUS_OK             -> da tinh xong va hop le
 *   SPO2_STATUS_INVALID_SIGNAL -> da tinh xong nhung tin hieu kem chat luong
 * ========================================================================= */
Spo2Status Spo2_Process(Spo2Estimator* est, uint32_t redRaw, uint32_t irRaw,
                        uint32_t timestampMs, Spo2Result* out)
{
    /* Kiem tra tham so truoc khi su dung - phong tranh crash tren MCU */
    if ((est == NULL) || (out == NULL))
    {
        return SPO2_STATUS_NOT_READY;
    }

    /* Lan dau goi -> danh dau da bat dau va ghi nhan thoi diem bat dau block.
     * blockStartMs se duoc dung de xac dinh khi nao block hien tai "day". */
    if (!est->started)
    {
        est->started = true;
        est->blockStartMs = timestampMs;
    }

    /* ---- Nap mau vao block hien tai ----
     * Moi mau RED va IR duoc cong vao running sum cua block hien tai.
     * Su dung int64 de tranh so lan (overflow) khi tong nhieu mau RAW lon.
     *
     * Cong thuc tong chay (running sum):
     *   sumSq += raw * raw  -> tich luy E[x^2] cho viec tinh RMS sau nay
     *   sum   += raw        -> tich luy E[x] (DC)
     *   count  ++           -> dem so mau de tinh trung binh
     *
     * Day la "online algorithm" - cap nhat lien tuc moi sample ma khong
     * can quay lai du lieu cu. */
    Spo2Block* b = &est->blocks[est->curBlock];
    b->sumRed   += (int64_t)redRaw;               // Tong gia tri RED
    b->sumIr    += (int64_t)irRaw;                // Tong gia tri IR
    b->sumSqRed += (int64_t)redRaw * (int64_t)redRaw;  // Tong binh phuong RED
    b->sumSqIr  += (int64_t)irRaw * (int64_t)irRaw;    // Tong binh phuong IR
    ++b->count;                                     // Tang so mau trong block

    /* ---- Kiem tra ranh gioi block ----
     * Neu thoi gian tu khi bat dau block hien tai >= PPG_SPO2_UPDATE_MS
     * (1000ms = 1 giay), thi block da du thoi gian va can "dong cua" block nay,
     * sau do chuyen sang block moi. */
    if ((timestampMs - est->blockStartMs) >= (uint32_t)PPG_SPO2_UPDATE_MS)
    {
        Spo2Status status = SPO2_STATUS_NOT_READY;

        /* Tang so block da day (gioi han toi da PPG_SPO2_BLOCKS).
         * Vi circular buffer, filledBlocks toi da bang PPG_SPO2_BLOCKS.
         * Neu da day -> khong tang nua, block cu nhat bi lan chen. */
        if (est->filledBlocks < (uint8_t)PPG_SPO2_BLOCKS)
        {
            ++est->filledBlocks;
        }

        /* Chi tinh SpO2 khi du so block (4 block = 4 giay).
         * Truoc do, cua so con thieu du lieu -> tra ve NOT_READY. */
        if (est->filledBlocks >= (uint8_t)PPG_SPO2_BLOCKS)
        {
            status = computeWindow(est, out);
        }

        /* ---- Chuyen sang block ke tiep (circular buffer) ----
         * Modulo PPG_SPO2_BLOCKS de quay ve dau mang khi den cuoi.
         * Block moi duoc xoa sach de bat dau tich luy tu dau.
         * blockStartMs duoc cap nhat lai de dem thoi gian block moi. */
        est->curBlock = (uint8_t)((est->curBlock + 1U) %
                                  (uint8_t)PPG_SPO2_BLOCKS);
        clearBlock(&est->blocks[est->curBlock]);  // Xoa block moi truoc khi su dung
        est->blockStartMs = timestampMs;           // Bat dau dem thoi gian block moi

        return status;
    }

    /* Block chua het thoi gian -> tiep tuc tich luy mau, khong co ket qua moi */
    return SPO2_STATUS_NOT_READY;
}
