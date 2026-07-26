/**
 * @file    temporary_history_store.c
 * @brief   Cài đặt lịch sử RAM circular cỡ cố định (ghi đè bản cũ nhất).
 * @note    User-owned. Không cấp phát động.
 *
 * =========================================================================
 *  CIRCULAR BUFFER PATTERN (Mẫu Bộ Đệm Xoay Vòng)
 * =========================================================================
 *
 *  Đây là cách cài đặt "circular buffer" (bộ đệm xoay vòng / bộ đệm hình vành đai).
 *  Ý tưởng: dùng một mảng cố định, khi ghi đến cuối mảng thì quay về đầu.
 *
 *  s_store.records[TEMP_HISTORY_CAPACITY]  = mảng cố định chứa tối đa 20 bản ghi.
 *  s_store.count        = số bản ghi hiện đang lưu (từ 0 đến 20).
 *                        - Khi count < 20: mảng chưa đầy, mỗi lần thêm thì count++.
 *                        - Khi count == 20: mảng đã đầy, mỗi lần thêm sẽ ghi đè
 *                          lên bản ghi CŨ NHẤT (overwrite).
 *  s_store.writeIndex   = chỉ số (index) trong mảng mà bản ghi TIẾP THEO sẽ được ghi vào.
 *                        - Luôn trượt về phía trước, quay vòng lại 0 khi hết mảng.
 *                        - Ví dụ: writeIndex=0 -> ghi records[0], rồi writeIndex=1, ...
 *                          Khi writeIndex=19 (cuối mảng), lần sau quay lại records[0].
 *  s_store.overwriteCount = số lần một bản ghi cũ nhất đã bị ghi đè (dùng để chẩn đoán).
 *  s_store.nextRecordId   = ID tự tăng, mỗi bản ghi nhận một ID duy nhất, không bao giờ
 *                           trùng lặp (kể cả khi đã ghi đè).
 *
 *  Ví dụ minh họa với TEMP_HISTORY_CAPACITY = 5:
 *  ------------------------------------------------
 *  Giai đoạn 1 (chưa đầy):  count=3, writeIndex=3
 *    Mảng:  [Bản_A] [Bản_B] [Bản_C] [   ?  ] [   ?  ]
 *             idx=0   idx=1   idx=2   idx=3   idx=4
 *    Bản mới nhất = records[2] (index cao nhất đã ghi)
 *    Bản cũ nhất   = records[0] (index thấp nhất đã ghi)
 *
 *  Giai đoạn 2 (đã đầy):   count=5, writeIndex=3 (đã ghi đè)
 *    Mảng:  [Bản_F] [Bản_G] [Bản_H] [Bản_I] [Bản_E]
 *             idx=0   idx=1   idx=2   idx=3   idx=4
 *    Ghi đè: Bản_A (cũ nhất) ở idx=0 bị thay bằng Bản_F.
 *    count KHÔNG tăng (luôn stays = 5), overwriteCount++.
 *
 *  Khi đọc: "Bản mới nhất" luôn nằm ở writeIndex - 1 (quay vòng).
 * =========================================================================
 */

#include "temporary_history_store.h"

/**
 * Cấu trúc nội bộ của store (singleton - chỉ có 1 instance duy nhất trong chương trình).
 *
 * @field records       - mảng cố định 20 bản ghi, lưu trong RAM (mất khi reset).
 * @field count         - số bản ghi hiện có (0 đến TEMP_HISTORY_CAPACITY).
 * @field writeIndex    - chỉ số ghi tiếp theo trong mảng (0 đến CAPACITY-1).
 * @field nextRecordId  - ID tiếp theo sẽ gán cho bản ghi mới (tự tăng).
 * @field overwriteCount - số lần ghi đè lên bản ghi cũ nhất (để theo dõi/diagnostic).
 */
typedef struct
{
    MeasurementHistoryRecord records[TEMP_HISTORY_CAPACITY]; /* Mảng cố định 20 bản ghi */
    size_t   count;         /* Số bản ghi đang lưu (0..20) */
    size_t   writeIndex;    /* Chỉ số mảng sẽ ghi vào tiếp theo (xoay vòng 0..19) */
    uint32_t nextRecordId;  /* ID tự tăng, bắt đầu từ 1 */
    uint32_t overwriteCount;/* Số lần bản ghi cũ nhất bị ghi đè (chẩn đoán) */
} TemporaryHistoryStore;

/* Biến static toàn cục - chỉ file này mới truy cập được (encapsulation) */
static TemporaryHistoryStore s_store;

/**
 * Khởi tạo store - gọi MỘT LẦN lúc bật nguồn (hoặc reset logic).
 *
 * - count = 0: mảng đang trống, chưa có bản ghi nào.
 * - writeIndex = 0: lần Add đầu tiên sẽ ghi vào records[0].
 * - nextRecordId = 1: bản ghi đầu tiên sẽ nhận ID = 1, bản tiếp = 2, ...
 * - overwriteCount = 0: chưa có lần ghi đè nào.
 *
 * LƯU Ý: Không xóa nội dung records[] array! Chỉ reset con trỏ/count mà thôi.
 * Đây là tối ưu trên embedded: không cần memset cả mảng, vì dữ liệu cũ
 * sẽ bị ghi đè khi count đầy lên lại.
 */
HistoryStatus TemporaryHistory_Init(void)
{
    s_store.count = 0U;
    s_store.writeIndex = 0U;
    s_store.nextRecordId = 1U;
    s_store.overwriteCount = 0U;
    return HISTORY_STATUS_OK;
}

/**
 * Thêm một bản ghi mới vào store.
 *
 * QUY TRÌNH (step-by-step):
 *  1. Kiểm tra con trỏ record không được NULL.
 *  2. Gán recordId tự tăng cho record, rồi tăng nextRecordId cho lần sau.
 *     -> recordId = s_store.nextRecordId; s_store.nextRecordId++;
 *     -> Gán xong thì nextRecordId đã tăng 1, sẵn sàng cho bản ghi kế tiếp.
 *  3. Copy TOÀN BỘ nội dung record vào records[writeIndex] (sao chép giá trị,
 *     không chỉ copy con trỏ). Sau phép gán này, record trong store là bản sao
 *     độc lập - caller có thể thay đổi record gốc mà store không bị ảnh hưởng.
 *  4. Xử lý count và overwriteCount:
 *     - Nếu count < CAPACITY (mảng chưa đầy): count++ (thêm bản ghi mới).
 *     - Nếu count == CAPACITY (mảng đã đầy): KHÔNG tăng count (vẫn giữ nguyên),
 *       nhưng tăng overwriteCount. Bản ghi cũ nhất ở writeIndex hiện tại đã bị
 *       ghi đè bằng bản ghi mới.
 *  5. Tăng writeIndex, quay vòng bằng phép chia lấy dư (%).
 *     - Ví dụ: writeIndex = 19 -> (19+1) % 20 = 0 -> quay về đầu mảng.
 *
 * @param record  Con trỏ đến bản ghi cần thêm. recordId của nó sẽ bị GHI ĐÈ
 *                bằng ID tự tăng (caller đọc lại từ record sau khi gọi).
 * @return HISTORY_STATUS_OK, hoặc INVALID_ARGUMENT nếu record == NULL.
 */
HistoryStatus TemporaryHistory_Add(MeasurementHistoryRecord* record)
{
    /* Bước 1: Kiểm tra đầu vào */
    if (record == NULL)
    {
        return HISTORY_STATUS_INVALID_ARGUMENT;
    }

    /* Bước 2: Gán recordId tự tăng. Post-increment: dùng giá trị hiện tại,
       rồi mới tăng cho lần sau. Ví dụ: lần đầu nextRecordId=1 -> recordId=1,
       sau đó nextRecordId trở thành 2 cho bản ghi tiếp theo. */
    record->recordId = s_store.nextRecordId++;

    /* Bước 3: Copy toàn bộ bản ghi vào vị trí writeIndex hiện tại.
       Dereference *record để copy giá trị (không phải copy con trỏ).
       Nếu mảng đã đầy, thao tác này sẽ ghi đè lên bản ghi cũ nhất. */
    s_store.records[s_store.writeIndex] = *record;

    /* Bước 4: Cập nhật count và overwriteCount.
       - Nếu count == CAPACITY: mảng đã đầy -> ghi đè xảy ra -> overwriteCount++.
         count KHÔNG tăng vì số lượng bản ghi vẫn giữ nguyên (20).
       - Nếu count < CAPACITY: mảng chưa đầy -> count++ để theo dõi số lượng. */
    if (s_store.count == TEMP_HISTORY_CAPACITY)
    {
        ++s_store.overwriteCount;    /* đã ghi đè bản cũ nhất */
    }
    else
    {
        ++s_store.count;
    }

    /* Bước 5: Di chuyển writeIndex đến vị trí tiếp theo, quay vòng bằng %.
       Ví dụ: (19 + 1) % 20 = 0 -> quay về đầu mảng.
       Đây là phép toán cốt lõi tạo ra hiệu ứng "xoay vòng" của circular buffer. */
    s_store.writeIndex = (s_store.writeIndex + 1U) % TEMP_HISTORY_CAPACITY;
    return HISTORY_STATUS_OK;
}

/**
 * Trả về số bản ghi đang lưu trong store.
 *
 * @return Số bản ghi (0 đến TEMP_HISTORY_CAPACITY).
 *         - 0 = store đang trống (chưa Add lần nào).
 *         - 20 = store đã đầy (đã có 20 bản ghi, bản mới nhất sẽ ghi đè bản cũ).
 */
size_t TemporaryHistory_GetCount(void)
{
    return s_store.count;
}

/**
 * Đọc một bản ghi theo chỉ số newest-first (mới nhất trước).
 *
 * newestIndex = 0 -> bản ghi MỚI NHẤT (vừa ghi gần đây nhất).
 * newestIndex = 1 -> bản ghi CŨ HƠN một bậc (thứ hai mới nhất).
 * newestIndex = count-1 -> bản ghi CŨ NHẤT còn trong store.
 *
 * CÔNG THỨC TÍNH VỊ TRÍ (position) TRONG MẢNG:
 * -----------------------------------------------------------------------
 *  pos = (writeIndex + 2*CAPACITY - 1 - newestIndex) % CAPACITY
 *
 *  Tại sao dùng 2*CAPACITY?
 *  -----------------------------------------------
 *  Trong C, phép '%"' (modulo) với số âm có KẾT QUẢ KHÔNG XÁC ĐỊNH
 *  (implementation-defined) trên một số hệ thống. Ví dụ:
 *    - (-1) % 20 có thể là -1 hoặc 19 tùy compiler/platform.
 *
 *  Để ĐẢM BẢO kết quả luôn dương (an toàn trên mọi nền tảng),
 *  ta cộng thêm 2*CAPACITY trước khi '%'. 2*CAPACITY đủ lớn để
 *  biểu thức luôn dương mà không ảnh hưởng kết quả chia dư.
 *
 *  Ví dụ minh họa:
 *  ----------------
 *  Giả sử CAPACITY = 20, writeIndex = 5, count = 3 (3 bản ghi đầy).
 *  3 bản ghi nằm ở vị trí: records[3], records[4], records[5] (mảng xoay vòng).
 *  Bản ghi mới nhất (newestIndex=0) nằm ở records[4] (writeIndex-1 = 4).
 *
 *    pos = (5 + 40 - 1 - 0) % 20 = 44 % 20 = 4  -> records[4] = MỚI NHẤT ✓
 *    pos = (5 + 40 - 1 - 1) % 20 = 43 % 20 = 3  -> records[3] = thứ hai ✓
 *    pos = (5 + 40 - 1 - 2) % 20 = 42 % 20 = 2  -> records[2] = CŨ NHẤT ✓
 *
 *  Nếu KHÔNG có 2*CAPACITY (chỉ dùng writeIndex - 1 - newestIndex):
 *    Khi writeIndex = 0, newestIndex = 0:
 *      pos = (0 - 1) % 20 = -1 % 20 = ??? (kết quả không xác định trong C!)
 *
 *  Với 2*CAPACITY:
 *    pos = (0 + 40 - 1 - 0) % 20 = 39 % 20 = 19 -> ĐÚNG! (records[19])
 * -----------------------------------------------------------------------
 *
 * @param newestIndex  Chỉ số từ 0 (= mới nhất) đến count-1 (= cũ nhất).
 * @param record       Con trỏ đến vùng nhớ đích để copy bản ghi vào.
 * @return HISTORY_STATUS_OK, INVALID_ARGUMENT, EMPTY, hoặc NOT_FOUND.
 */
HistoryStatus TemporaryHistory_GetByNewestIndex(size_t newestIndex,
                                                MeasurementHistoryRecord* record)
{
    /* Kiểm tra con trỏ đích không được NULL */
    if (record == NULL)
    {
        return HISTORY_STATUS_INVALID_ARGUMENT;
    }
    /* Store đang trống - không có bản ghi nào để đọc */
    if (s_store.count == 0U)
    {
        return HISTORY_STATUS_EMPTY;
    }
    /* newestIndex ngoài phạm vi hợp lệ (phải từ 0 đến count-1) */
    if (newestIndex >= s_store.count)
    {
        return HISTORY_STATUS_NOT_FOUND;
    }

    /*
     * Tính vị trí trong mảng records[]:
     *
     * writeIndex trỏ đến vị trí GHI TIẾP THEO, nên vị trí CỦA BẢN MỚI NHẤT
     * là writeIndex - 1 (đã lùi 1 bước). Lùi thêm newestIndex bước nữa để
     * đến bản ghi cần tìm.
     *
     * Công thức: pos = (writeIndex + 2*CAPACITY - 1 - newestIndex) % CAPACITY
     *   - "+2*CAPACITY" đảm bảo biểu thức luôn dương (xem giải thích ở trên).
     *   - "-1" vì bản mới nhất nằm ở writeIndex-1 (writeIndex đã bị dịch sang vị trí trống).
     *   - "-newestIndex" lùi thêm newestIndex bước để đến bản cần tìm.
     *   - "% CAPACITY" đưa kết quả về vùng [0, CAPACITY-1].
     */
    const size_t pos = (s_store.writeIndex + (2U * TEMP_HISTORY_CAPACITY) - 1U - newestIndex)
                       % TEMP_HISTORY_CAPACITY;
    /* Copy bản ghi từ vị trí tính được vào vùng đích */
    *record = s_store.records[pos];
    return HISTORY_STATUS_OK;
}

/**
 * Tìm và copy một bản ghi theo recordId (tìm kiem tuan tu / linear search).
 *
 * QUY TRÌNH:
 *  - Duyệt qua tất cả bản ghi từ MỚI NHẤT đến CŨ NHẤT.
 *  - Dùng cùng công thức position như GetByNewestIndex (i = newestIndex).
 *  - Nếu tìm thấy recordId cần tìm -> copy ra và trả về OK.
 *  - Nếu duyệt hết mà không thấy -> trả về NOT_FOUND.
 *
 * Tại sao duyệt từ mới nhất trước?
 *  - Trong thực tế, người dùng thường quan tâm bản ghi mới nhất.
 *  - Nếu cần tìm bản ghi mới, tìm sẽ nhanh hơn (dừng sớm ở đầu vòng lặp).
 *  - Nếu cần tìm bản ghi cũ, vẫn duyệt hết nhưng đây là mảng nhỏ (max 20 phần tử)
 *    nên hiệu năng không đáng lo.
 *
 * @param recordId  ID cần tìm (gán bởi TemporaryHistory_Add).
 * @param record    Con trỏ đến vùng nhớ đích.
 * @return HISTORY_STATUS_OK nếu tìm thấy, NOT_FOUND nếu không.
 */
HistoryStatus TemporaryHistory_GetById(uint32_t recordId,
                                       MeasurementHistoryRecord* record)
{
    /* Kiểm tra con trỏ đích không được NULL */
    if (record == NULL)
    {
        return HISTORY_STATUS_INVALID_ARGUMENT;
    }

    /*
     * Duyệt từ i=0 (bản mới nhất) đến i=count-1 (bản cũ nhất).
     * Tại mỗi bước, tính vị trí trong mảng bằng cùng công thức GetByNewestIndex:
     *   pos = (writeIndex + 2*CAPACITY - 1 - i) % CAPACITY
     * i chính là "newestIndex" - số bước lùi từ bản mới nhất.
     */
    for (size_t i = 0U; i < s_store.count; ++i)
    {
        /* Tính vị trí thực tế trong mảng records[] cho bản ghi thứ i mới nhất */
        const size_t pos = (s_store.writeIndex + (2U * TEMP_HISTORY_CAPACITY) - 1U - i)
                           % TEMP_HISTORY_CAPACITY;

        /* So sánh recordId: nếu khớp thì tìm thấy */
        if (s_store.records[pos].recordId == recordId)
        {
            *record = s_store.records[pos];  /* Copy bản ghi ra ngoài */
            return HISTORY_STATUS_OK;
        }
    }

    /* Duyệt hết mà không tìm thấy recordId cần tìm */
    return HISTORY_STATUS_NOT_FOUND;
}

/**
 * Xóa toàn bộ dữ liệu trong store (đưa về trạng thái trống).
 *
 * Lưu ý:
 *  - Chỉ reset count và writeIndex về 0. KHÔNG xóa records[] array.
 *  - nextRecordId và overwriteCount KHÔNG bị reset.
 *    + nextRecordId giữ nguyên: ID mới vẫn tăng tiếp, không bị trùng
 *      với các bản ghi cũ (dù đã xóa, ID cũ không tái sử dụng).
 *    + overwriteCount giữ nguyên: để theo dõi tổng số lần ghi đè từ
 *      khi khởi động (Diagnostic/Troubleshooting).
 *  - Các bản ghi cũ vẫn nằm trong mảng records[] nhưng bị coi là "không hợp lệ"
 *    vì count=0, không hàm Get nào truy cập được chúng.
 *  - Khi Add bản ghi mới, dữ liệu cũ sẽ tự bị ghi đè.
 */
HistoryStatus TemporaryHistory_Clear(void)
{
    s_store.count = 0U;
    s_store.writeIndex = 0U;
    return HISTORY_STATUS_OK;
}

/**
 * Trả về số lần bản ghi cũ nhất đã bị ghi đè (overwrite).
 *
 * Dùng cho diagnostic (chẩn đoán):
 *  - Nếu overwriteCount > 0: nghĩa là đã có bản ghi bị mất dữ liệu
 *    vì mảng đầy. Hệ thống đã chạy đủ lâu để lưu nhiều hơn 20 bản ghi.
 *  - Nếu overwriteCount == 0: tất cả bản ghi đầu tiên vẫn còn nguyên vẹn.
 *  - Giá trị này KHÔNG bị reset bởi Clear(), để theo dõi xuyên suốt
 *    từ khi Init().
 *
 * @return Số lần ghi đè đã xảy ra (0, 1, 2, ...).
 */
uint32_t TemporaryHistory_GetOverwriteCount(void)
{
    return s_store.overwriteCount;
}
