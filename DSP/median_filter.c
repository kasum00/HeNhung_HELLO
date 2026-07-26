/**
 * @file    median_filter.c
 * @brief   Bo loc trung vi (median filter) tren cua so co dinh.
 *
 * ==========================================================================
 * MUC DICH (WHAT)
 * ==========================================================================
 *   - Loai bo nhieu xung (spike noise) khoi tin hieu PPG.
 *   - Khong lam mo cac dinh (peak) nhu bo loc trung binh (moving average).
 *   - Giu nguyen kich thuoc va vi tri cua cac pulse PPG (quan trong cho BPM).
 *
 * ==========================================================================
 * THUAT TOAN (HOW)
 * ==========================================================================
 *   Median filter hoat dong theo nguyen ly:
 *     1. Giu mot "cua so" (window) chua N mau gan nhat.
 *     2. Moi lan co mau moi, ghi vao ring buffer.
 *     3. Copy sang buffer phu (sortBuf) va sap xep tang dan.
 *     4. Lay phan tu giua (median) - day la gia tri trung binh "chong nhieu".
 *
 *   Vi du (N=5, sortBuf sau sap xep = [10, 12, 50, 100, 102]):
 *     - Median = 50 (phan tu thu 2, index = 5/2 = 2)
 *     - 50 la gia tri dai dien cho "binh quan" cua 5 mau.
 *     - Neu 100 la spike (sai so lon), median van ~50 (chong nhieu).
 *     - Neu dung moving average thi 100 se keo len trung binh len ~75.
 *
 *   Tai sao dung ring buffer + buffer phu?
 *     - Ring buffer cho phep ghi mau moi O(1) - khong can dich mang.
 *     - Buffer phu (sortBuf) cho phep sap xep ma khong pha vo thu tu
 *       goc cua ring buffer (giu nguyen du lieu goc cho lan sau).
 *     - Doi voi embedded, viec "khong pha vo du lieu goc" quan trong
 *       vi co the can doc lai buffer cho muc dich debug/logging.
 *
 *   Tai sao dung insertion sort?
 *     - N nho (5-9), insertion sort rat nhanh O(N) trong truong hop
 *       gan sorted (moi chi thay 1 mau moi moi lan).
 *     - Heap sort / merge sort chi hieu qua khi N lon (>50).
 *     - Insertion sort khong can bo nho phu (in-place).
 *
 * ==========================================================================
 * DO PHUC TAP (COMPLEXITY)
 * ==========================================================================
 *   - Thoi gian: O(N log N) moi mau do sap xep (N nho nen ~O(N)).
 *   - Bo nho: 2 * N * 4 byte (buffer + sortBuf) = 40 byte voi N=5.
 *   - Khong cap phat bo nho dong (buffer do caller so huu).
 *
 * ==========================================================================
 * GHI CHU SU DUNG (USAGE NOTES)
 * ==========================================================================
 *   - N nen la so le (5, 7, 9) de median luon la 1 gia tri co that.
 *   - Neu N chan, median la trung binh cua 2 phan tu giua (van hop ly).
 *   - Khi chua du N mau, median lay tren so mau it hon (chua on dinh).
 *   - Dung Median_IsReady de kiem tra da du N mau truoc khi su dung.
 *
 * ==========================================================================
 */

#include "median_filter.h"
#include <string.h>  /* memcpy */

/*
 * ==========================================================================
 * insertionSort - Sap xep tang dan trong-place bang insertion sort
 * ==========================================================================
 *
 * LAM GI (WHAT):
 *   Sap xep mang arr co n phan tu theo thu tu tang dan (ascending).
 *
 * TAI SAO (WHY):
 *   - N nho (thuong 5-9), insertion sort la lua chon tot nhat:
 *     + O(N) khi du lieu gan sorted (moi chi 1 phan tu moi).
 *     + O(N^2) worst case nhung N nho nen khong quan trong.
 *     + Khong can bo nho phu (in-place).
 *   - Cac thuat toan khac (qsort, heap sort) co overhead lon hon voi N nho.
 *
 * CHI TIET (HOW):
 *   - Duyet tu phan tu thu 2 (i=1) den cuoi mang.
 *   - Moi phan tu "key" duoc "chen vao cho" bang cach dich cac phan tu
 *     lon hon key sang phai (vi du [10, 50, 12] -> chen 12 -> [10, 12, 50]).
 *   - So phep so sanh toi da = N*(N-1)/2 = 10 voi N=5 (rat nhanh).
 *   - Mang da sap xep o lan truoc thi chi can so sanh 1-2 lan la xong
 *     (vi moi chi thay 1 phan tu moi), nen thuc te gan nhu O(N).
 */
static void insertionSort(int32_t* arr, size_t n)
{
    for (size_t i = 1U; i < n; ++i)
    {
        int32_t key = arr[i];           /* Phan tu can chen */
        size_t j = i;
        /* Dich cac phan tu lon hon key sang phai de tao cho trong */
        while (j > 0U && arr[j - 1U] > key)
        {
            arr[j] = arr[j - 1U];       /* Dich sang phai */
            --j;
        }
        arr[j] = key;                   /* Chen key vao vi tri dung */
    }
}

/*
 * ==========================================================================
 * Median_Init - Khoi tao bo loc trung vi
 * ==========================================================================
 *
 * LAM GI (WHAT):
 *   Gan buffer (do caller cap) vao filter, dat trang thai ban dau.
 *   Reset toan bo buffer ve 0.
 *
 * TAI SAO (WHY):
 *   - "User-owned buffer" pattern: filter khong cap phat bo nho dong.
 *     Vi du: caller khai bao int32_t myBuf[5], mySort[5]; va truyen vao.
 *   - Loi ich: khong co malloc/free (an toan cho embedded, khong co heap).
 *   - Buffer o stack hoac static: do tre truy cap nhat, khong fragmentation.
 *
 * CHI TIET (HOW):
 *   - Gan con tro: filter->buffer = buf (ring buffer).
 *   - Gan con tro: filter->sortBuf = sortBuf (buffer phu de sap xep).
 *   - capacity = n: so phan tu toi da cua cua so.
 *   - count = 0: chua co mau nao (cua so trong).
 *   - writeIndex = 0: ghi tu phan tu dau tien.
 *   - memset(buf, 0, ...): xoa sach buffer de tranh doc gia tri rac.
 */
void Median_Init(MedianFilter* f, int32_t* buf, int32_t* sortBuf, size_t n)
{
    if (f == NULL || buf == NULL || sortBuf == NULL || n == 0U) { return; }
    f->buffer    = buf;       /* Gan ring buffer (do caller so huu) */
    f->sortBuf   = sortBuf;  /* Gan buffer phu cho sap xep */
    f->capacity  = n;         /* Kich thuoc cua so (nen la so le) */
    f->count     = 0U;        /* Chua co mau nao */
    f->writeIndex= 0U;        /* Bat dau ghi tu dau */
    memset(buf, 0, n * sizeof(int32_t));  /* Xoa sach buffer */
}

/*
 * ==========================================================================
 * Median_Reset - Xoa du lieu nhung giu nguyen cau truc
 * ==========================================================================
 *
 * LAM GI (WHAT):
 *   Dat count = 0, writeIndex = 0, xoa buffer ve 0.
 *   Filter tro ve trang thai "chua co mau nao" nhu sau Init.
 *
 * TAI SAO (WHY):
 *   - Can reset khi chuyen nguoi dung (thiet bi y te da dang nguoi dung).
 *   - Reset nhanh hon Init vi khong can gan lai buffer/con tro.
 *   - Dung khi phat hien tin hieu bat thuong (artifact lon), can bat dau lai
 *     tu dau de median khong bi nhiem gia tri "xau" cu.
 *
 * CHI TIET (HOW):
 *   - count = 0: danh dau la cua so trong.
 *   - writeIndex = 0: ghi lai tu vi tri dau tien.
 *   - memset(buffer, 0): xoa sach du lieu cu de tranh doc gia tri rac
 *     trong truong hop filter duoc dung lai (re-use) ngay sau reset.
 *   - Khong can reset sortBuf vi no se bi ghi de moi lan Process.
 */
void Median_Reset(MedianFilter* f)
{
    if (f == NULL) { return; }
    f->count      = 0U;   /* Danh dau cua so trong */
    f->writeIndex = 0U;   /* Ve dau ring buffer */
    memset(f->buffer, 0, f->capacity * sizeof(int32_t));  /* Xoa sach du lieu cu */
}

/*
 * ==========================================================================
 * Median_Process - Xu ly mot mau tin hieu PPG
 * ==========================================================================
 *
 * LAM GI (WHAT):
 *   Nhan 1 mau moi, ghi vao ring buffer, sap xep, va tra ve gia tri trung vi.
 *
 * TAI SAO (WHY):
 *   - Median la phuong phap loc "manh" nhat de loai spike:
 *     + Moving average: spike keo len trung binh (dang ke voi spike lon).
 *     + Median: spike bi "bo qua" (chi thay 1 phan tu trong N).
 *   - Vi du PPG: neu co 1 mau ADC bi nhay len 4095 (sai), voi N=5,
 *     median van la gia tri binh thuong (chi 1/5 mau bi sai).
 *   - Dau ra PPG dep hon, cac dinh (peak) khong bi "nun" boi spike.
 *
 * CHI TIET (HOW):
 *
 *   Buoc 1: Ghi mau moi vao ring buffer
 *     - buffer[writeIndex] = input: ghi tai vi tri hien tai.
 *     - writeIndex = (writeIndex + 1) % capacity: tien den vi tiep theo,
 *       quay ve dau khi het buffer (circular/ring buffer).
 *     - count: tang toi da la capacity (khong vuot qua).
 *
 *   Buoc 2: Copy sang sortBuf va sap xep
 *     - memcpy: sao chep du lieu tu ring buffer sang buffer phu.
 *     - Tai sao khong sap xep truc tiep ring buffer?
 *       + Ring buffer giu thu tu goc (FIFO) de biet mau nao cu nhat.
 *       + Sap xep tren buffer phu, ring buffer van nguyen trang thai.
 *     - insertionSort: sap xep tang dan tren buffer phu.
 *
 *   Buoc 3: Lay median
 *     - Median = sortBuf[count / 2]: phan tu giua cua mang da sap xep.
 *     - Voi N=5: median = sortBuf[2] (phan tu thu 3).
 *     - Voi N=7: median = sortBuf[3] (phan tu thu 4).
 *     - Khi count < capacity: van lay duoc median (tren so mau it hon).
 *
 *   Luu y ve hieu qua:
 *     - Phuong phap nay la O(N log N) moi mau do sap xep.
 *     - Voi N=5: sap xep chi can toi da 10 phep so sanh (rat nhanh).
 *     - Neu N lon (>20), can xem xet dung "quick select" (O(N) average).
 */
int32_t Median_Process(MedianFilter* f, int32_t input)
{
    if (f == NULL || f->buffer == NULL || f->capacity == 0U) { return 0; }

    /* Buoc 1: Ghi mau moi vao ring buffer tai vi tri writeIndex */
    f->buffer[f->writeIndex] = input;
    /* Quay vi tri ghi: 0 -> 1 -> 2 -> ... -> capacity-1 -> 0 */
    f->writeIndex = (f->writeIndex + 1U) % f->capacity;
    /* Tang count (toi da la capacity, khong vuot qua) */
    if (f->count < f->capacity) { ++f->count; }

    /* Buoc 2: Copy sang buffer phu va sap xep de lay median
     * Khong sap xep truc tiep ring buffer de giu nguyen thu tu goc */
    memcpy(f->sortBuf, f->buffer, f->count * sizeof(int32_t));
    insertionSort(f->sortBuf, f->count);

    /* Buoc 3: Lay median = phan tu giua cua mang da sap xep
     * Vi du: N=5, sortBuf = [10, 12, 50, 100, 102], median = 50 */
    return f->sortBuf[f->count / 2U];
}

/*
 * ==========================================================================
 * Median_IsReady - Kiem tra da du mau chua
 * ==========================================================================
 *
 * LAM GI (WHAT):
 *   Tra ve true khi count >= capacity (cua so da du N mau).
 *
 * TAI SAO (WHY):
 *   - Khi chua du N mau, median lay tren so mau it hon (van hop ly nhung
 *     khong on dinh). Vi du: N=5 nhung chi co 2 mau thi median chi dua
 *     tren 2 mau, chua day du thong tin.
 *   - Caller nen kiem tra IsReady truoc khi su dung ket qua cho BPM/SpO2
 *     de dam bao do chinh xac cao nhat.
 *   - Pattern nay giong MovingAverage_IsReady: mot giao dien thong nhat.
 *
 * CHI TIET (HOW):
 *   - count >= capacity: cua so da day.
 *   - NULL check: an toan cho embedded (khong crash).
 *   - Dung const MedianFilter* vi ham nay chi doc (khong thay doi trang thai).
 */
bool Median_IsReady(const MedianFilter* f)
{
    return (f != NULL) && (f->count >= f->capacity);
}
