/**
 * @file    moving_average_filter.c
 * @brief   Cai dat moving average chay tren sum (running-sum) tren buffer co dinh.
 *
 * ==========================================================================
 * MUC DICH (WHAT)
 * ==========================================================================
 *   - Lam muot tin hieu PPG bang trung binh cong tren cua so truot (N mau).
 *   - Loai bo nhiem ngau nhien (random noise) co tan so cao.
 *   - Dau ra la trung binh cua N mau gan nhat, giu lai xu huong chinh.
 *
 * ==========================================================================
 * THUAT TOAN (HOW) - Running Sum
 * ==========================================================================
 *   Moving average don gian la:
 *     y[n] = (x[n] + x[n-1] + x[n-2] + ... + x[n-N+1]) / N
 *
 *   Tinh truc tiep moi lan: O(N) phep cong - KHONG hieu qua.
 *
 *   Cach toi uu - Running Sum:
 *     1. Giu mot bien "sum" luu tong cua N mau hien co trong cua so.
 *     2. Khi co mau moi:
 *        - Neu cua so da day: tru di mau cu nhat (da "het han"), cong mau moi.
 *        - Neu cua so chua day: chi cong mau moi, tang count.
 *     3. Dau ra = sum / count.
 *
 *   Do do moi mau chi can 1 phep tru + 1 phep cong + 1 phep chia = O(1).
 *
 *   Vi du (N=5, sum=300, count=5):
 *     - Buffer: [60, 55, 70, 58, 57], sum = 300
 *     - Mau moi = 80:
 *       + Tru mau cu nhat (60): sum = 300 - 60 = 240
 *       + Cong mau moi (80): sum = 240 + 80 = 320
 *       + Buffer: [80, 55, 70, 58, 57], sum = 320
 *       + Output = 320 / 5 = 64
 *
 * ==========================================================================
 * RING BUFFER (CIRCULAR BUFFER)
 * ==========================================================================
 *   Buffer co dinh voi writeIndex quay vong (0 -> 1 -> ... -> N-1 -> 0).
 *     - writeIndex luon tro den vi tri ghi tiep theo (cung la mau cu nhat).
 *     - Khi cua so day: ghi de len mau cu nhat (da bi tru khoi sum).
 *     - Khong can dich mang - O(1) cho moi thao tac ghi.
 *
 * ==========================================================================
 * DO PHUC TAP (COMPLEXITY)
 * ==========================================================================
 *   - Thoi gian: O(1) moi mau (1 tru, 1 cong, 1 chia).
 *   - Bo nho: N * 4 byte (buffer) + 8 byte (sum, int64_t) + 12 byte (struct).
 *   - Khong cap phat bo nho dong (buffer do caller so huu).
 *
 * ==========================================================================
 * DO TRE (GROUP DELAY)
 * ==========================================================================
 *   - Moving average co group delay heng so = (N-1)/2 mau.
 *   - Vi du N=5: delay = 2 mau = 20 ms tai fs=100 Hz.
 *   - Delay nay khong anh huong BPM vi lay khoang cach giua cac peak
 *     (ca hai peak deu bi tre cung muc do, huy nhau).
 *
 * ==========================================================================
 * GHI CHU SU DUNG (USAGE NOTES)
 * ==========================================================================
 *   - Dung khi da du N mau (IsReady = true) de dam bao trung binh day du.
 *   - Truoc khi du N, output la trung binh mot phan (giam dan khi N tang).
 *   - Co the ket hop voi Lowpass_Filter de tinh BPM chinh xac hon.
 *   - User-owned buffer: caller cap buffer, filter khong malloc/free.
 *
 * ==========================================================================
 */

#include "moving_average_filter.h"

/*
 * ==========================================================================
 * MovingAverage_Init - Khoi tao bo loc trung binh cong
 * ==========================================================================
 *
 * LAM GI (WHAT):
 *   Gan buffer (do caller cap) vao filter, thiet lap kich thuoc cua so,
 *   va dat trang thai ban dau (sum=0, count=0, writeIndex=0).
 *
 * TAI SAO (WHY):
 *   - "User-owned buffer" pattern: filter khong cap phat bo nho dong.
 *     Vi du: int32_t myBuf[8]; ... MovingAverage_Init(&f, myBuf, 8);
 *   - Loi ich cho embedded:
 *     + Khong co malloc/free (an toan, khong co heap fragmentation).
 *     + Buffer o stack hoac static: do tre truy cap nhat (L1 cache hit).
 *   - Kiem tra truoc (precondition) de tranh bug sau nay:
 *     + NULL pointer -> HardFault tren ARM Cortex-M.
 *     + capacity = 0 -> chia cho 0 trong Process.
 *
 * CHI TIET (HOW):
 *   - filter->buffer = backingBuffer: gan con tro buffer (khong copy).
 *   - filter->capacity = capacity: luu kich thuoc cua so N.
 *   - filter->count = 0: chua co mau nao (cua so trong).
 *   - filter->writeIndex = 0: bat dau ghi tu vi tri dau tien.
 *   - filter->sum = 0: tong hien tai = 0 (chua co mau nao).
 *   - Tra ve MOVING_AVERAGE_STATUS_OK: thao tac thanh cong.
 */
MovingAverageStatus MovingAverage_Init(MovingAverageFilter* filter,
                                       int32_t* backingBuffer,
                                       size_t capacity)
{
    if ((filter == NULL) || (backingBuffer == NULL) || (capacity == 0U))
    {
        return MOVING_AVERAGE_STATUS_INVALID_ARGUMENT;
    }
    filter->buffer = backingBuffer;  /* Gan buffer do caller so huu */
    filter->capacity = capacity;     /* Kich thuoc cua so N */
    filter->count = 0U;             /* Chua co mau nao */
    filter->writeIndex = 0U;        /* Bat dau ghi tu dau */
    filter->sum = 0;                /* Tong hien tai = 0 */
    return MOVING_AVERAGE_STATUS_OK;
}

/*
 * ==========================================================================
 * MovingAverage_Reset - Xoa du lieu, giu nguyen cau truc buffer
 * ==========================================================================
 *
 * LAM GI (WHAT):
 *   Dat count = 0, writeIndex = 0, sum = 0.
 *   Filter tro ve trang thai "chua co mau nao" nhu sau Init.
 *
 * TAI SAO (WHY):
 *   - Can reset khi chuyen doi trang thai: chuyen nguoi dung, phat hien
 *     tin hieu bat thuong (artifact lon), hoac sau khoi dong lai.
 *   - Reset nhanh hon Init vi khong can gan lai buffer/con tro.
 *   - Khong can memset buffer vi cac gia tri cu se bi ghi de khi Process.
 *
 * CHI TIET (HOW):
 *   - count = 0: danh dau cua so trong, IsReady se tra ve false.
 *   - writeIndex = 0: ghi lai tu vi tri dau tien cua buffer.
 *   - sum = 0: xoa tong chay - dau ra se bat dau tu 0/1 = 0.
 *   - Sau reset, Process se tra ve NOT_READY cho den khi du N mau.
 */
void MovingAverage_Reset(MovingAverageFilter* filter)
{
    if (filter == NULL)
    {
        return;
    }
    filter->count = 0U;      /* Danh dau cua so trong */
    filter->writeIndex = 0U;  /* Ve dau ring buffer */
    filter->sum = 0;         /* Xoa tong chay */
}

/*
 * ==========================================================================
 * MovingAverage_Process - Xu ly mot mau tin hieu PPG
 * ==========================================================================
 *
 * LAM GI (WHAT):
 *   Nhan 1 mau moi, cap nhat tong chay (running sum), va tra ve trung binh
 *   cong cua cac mau dang nam trong cua so.
 *
 * TAI SAO (WHY):
 *   - Day la ham chinh (core) cua bo loc, goi moi lan lay mau (100 lan/giay).
 *   - Moving average O(1) la phep loc nhanh nhat co the - phu hop cho
 *     embedded vi CPU han che (STM32F0 chi 48 MHz).
 *   - Khong can dung FPU (floating point) - toan so nguyen (int32_t).
 *
 * CHI TIET (HOW) - Tung buoc:
 *
 *   Buoc 1: Kiem tra dieu kien tien quyet (precondition)
 *     - filter, output, buffer khong NULL.
 *     - capacity > 0 (tranh chia cho 0).
 *
 *   Buoc 2: Xu ly cua so day (count == capacity)
 *     - writeIndex dang tro den vi tri cu nhat (se bi ghi de).
 *     - Tru gia tri cu nhat khoi sum: sum -= buffer[writeIndex].
 *     - Vi du: sum=300, buffer[2]=70 -> sum = 230.
 *
 *   Buoc 3: Xu ly cua so chua day (count < capacity)
 *     - Chi tang count (chua can tru gi vi con cho trong).
 *     - Vi du: count=2/5 -> count=3/5, van con cho trong 2 vi tri.
 *
 *   Buoc 4: Ghi mau moi vao buffer
 *     - buffer[writeIndex] = input: ghi tai vi tri cu nhat (da tru o buoc 2).
 *     - sum += (int64_t)input: cong mau moi vao tong.
 *     - Quan trong: ep sang int64_t truoc khi cong de tranh overflow.
 *       Vi du: sum (int64_t) + input (int32_t) -> ket qua int64_t.
 *
 *   Buoc 5: Tien writeIndex (ring buffer)
 *     - writeIndex = (writeIndex + 1) % capacity: quay ve 0 khi het buffer.
 *     - Vi du capacity=5: 0->1->2->3->4->0->1->...
 *
 *   Buoc 6: Tinh trung binh
 *     - output = sum / count (chia nguyen, cat ve 0).
 *     - Khi chua day: count < N -> trung binh tren so mau it hon.
 *     - Khi day: count = N -> trung binh day du N mau.
 *
 *   Buoc 7: Tra ve trang thai
 *     - MOVING_AVERAGE_STATUS_OK: cua so da day, output la trung binh day du.
 *     - MOVING_AVERAGE_STATUS_NOT_READY: dang lap, output la trung binh mot phan.
 *     - Caller nen kiem tra trang thai de quyet dinh su dung output hay khong.
 *
 *   Luu y ve kieu du lieu:
 *     - sum la int64_t de tranh overflow khi N lon. Vi du: N=100, moi mau=65535
 *       -> sum toi da = 6,553,500 (vua int32_t nhung an toan hon voi int64_t).
 *     - Chia int64_t / int64_t -> ket qua int32_t (truncate ve 0).
 */
MovingAverageStatus MovingAverage_Process(MovingAverageFilter* filter,
                                          int32_t input,
                                          int32_t* output)
{
    if ((filter == NULL) || (output == NULL) || (filter->buffer == NULL) ||
        (filter->capacity == 0U))
    {
        return MOVING_AVERAGE_STATUS_INVALID_ARGUMENT;
    }

    if (filter->count == filter->capacity)
    {
        /*
         * Cua so da day: writeIndex dang tro den mau CU NHAT (se bi ghi de).
         * Tru mau cu nhat khoi tong chay truoc khi ghi mau moi.
         * Vi du: sum=300, buffer[2]=70 -> sum=230, sau do cong mau moi.
         */
        filter->sum -= (int64_t)filter->buffer[filter->writeIndex];
    }
    else
    {
        /*
         * Cua so chua day: tang count (van con cho trong trong buffer).
         * Khong can tru gi vi moi mau them vao deu la "mau moi".
         */
        ++filter->count;
    }

    /* Ghi mau moi vao vi tri writeIndex (vi tri cu nhat da bi tru o tren) */
    filter->buffer[filter->writeIndex] = input;
    /* Cong mau moi vao tong chay (ep int64_t de tranh overflow) */
    filter->sum += (int64_t)input;
    /* Tien vi tri ghi: quay ve 0 khi het buffer (ring buffer) */
    filter->writeIndex = (filter->writeIndex + 1U) % filter->capacity;

    /*
     * Tinh trung binh: chia nguyen (truncate ve 0).
     * Doi xung, khong gay lech DC (dieu kien quan trong cho PPG).
     * Vi du: sum=320, count=5 -> output = 64.
     */
    *output = (int32_t)(filter->sum / (int64_t)filter->count);

    /* Tra ve trang thai: OK neu day du N mau, NOT_READY neu dang lap */
    return (filter->count == filter->capacity) ? MOVING_AVERAGE_STATUS_OK
                                               : MOVING_AVERAGE_STATUS_NOT_READY;
}

/*
 * ==========================================================================
 * MovingAverage_IsReady - Kiem tra da du mau chua
 * ==========================================================================
 *
 * LAM GI (WHAT):
 *   Tra ve true khi count == capacity (cua so da du N mau).
 *
 * TAI SAO (WHY):
 *   - Khi cua so chua day, output la trung binh mot phan (giam dan khi N tang).
 *   - Vi du: N=5, chi co 2 mau -> output = (60+55)/2 = 57.5 -> 57.
 *     Day la gia tri chua on dinh, khong nen su dung cho tinh BPM.
 *   - Caller nen kiem tra IsReady truoc khi su dung ket qua:
 *       if (MovingAverage_IsReady(&f)) { su dung output cho BPM; }
 *   - Pattern nay giong Median_IsReady: giao dien thong nhat cho tat ca filter.
 *
 * CHI TIET (HOW):
 *   - count == capacity: cua so da day, moi vi tri deu co mau.
 *   - NULL check: an toan cho embedded (khong crash).
 *   - Dung const MovingAverageFilter* vi chi doc (khong thay doi trang thai).
 *   - Toan bo phep kiem tra trong 1 dong: hieu qua va de doc.
 */
bool MovingAverage_IsReady(const MovingAverageFilter* filter)
{
    return (filter != NULL) && (filter->count == filter->capacity);
}
