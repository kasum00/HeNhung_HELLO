/**
 * @file    rtc_service.c
 * @brief   Cài đặt RTC service (đọc/công bố DS1307 + yêu cầu cài giờ).
 * @note    User-owned. Target-only. Snapshot double-buffer cho an toàn SPSC.
 *
 * =====================================================================
 *  TỔNG QUAN KIẾN TRÚC
 * =====================================================================
 *
 *  Nhiệm vụ chính: đọc thời gian từ RTC DS1307 qua I2C, công bố kết quả
 *  cho GUI thread một cách an toàn bằng kỹ thuật DOUBLE-BUFFER.
 *
 *  Hai luồng chính:
 *    - SENSOR TASK: chạy trong RTOS, giữ I2C mutex, gọi Poll() và
 *      ProcessPendingSet() mỗi chu kỳ.
 *    - GUI THREAD: chỉ đọc snapshot (GetSnapshot) và gửi yêu cầu cài giờ
 *      (RequestSet), KHÔNG truy cập I2C trực tiếp.
 *
 *  Flow cơ bản:
 *    1. Sensor task gọi Poll() -> đọc DS1307 -> ghi vào buffer không active
 *       -> flip s_active -> GUI thấy buffer mới ngay lập tức.
 *    2. GUI gọi RequestSet() -> ghi s_setRequest + set cờ s_setPending.
 *    3. Sensor task gọi ProcessPendingSet() -> kiểm tra s_setPending ->
 *       ghi vào DS1307 -> xóa cờ -> tăng s_setGeneration.
 *    4. GUI polling GetLastSetResult() -> thấy generation thay đổi -> biết
 *       đã hoàn tất.
 */

#include "rtc_service.h"
#include "ds1307_driver.h"

/* ====================================================================
 *  DOUBLE-BUFFER PATTERN (SPSC - Single Producer Single Consumer)
 * ====================================================================
 *
 *  s_buf[2] chứa 2 RtcSnapshot: s_buf[0] và s_buf[1].
 *  s_active chỉ số (0 hoặc 1) của buffer ĐANG ĐƯỢC ĐỌC bởi GUI.
 *
 *  Nguyên lý:
 *    - PRODUCER (sensor task): ghi dữ liệu vào buffer KHÔNG active
 *      (index = s_active XOR 1), sau đó flip s_active.
 *    - CONSUMER (GUI thread): đọc từ buffer active (s_buf[s_active]).
 *
 *  Tại sao an toàn?
 *    - Producer chỉ ghi vào buffer mà consumer KHÔNG đang đọc.
 *    - Sau khi ghi xong, một phép ghi đơn byte (s_active = next) là atomic
 *      trên ARM Cortex-M -> consumer thấy dữ liệu mới nhất, không bao giờ
 *      thấy một DateTime bị viết dở (partial write).
 *    - Không cần mutex, không cần lock -> hiệu năng cao trên MCU.
 *
 *  Ví dụ minh họa:
 *    Buộc 1: s_active=0 -> GUI đọc s_buf[0], Sensor ghi s_buf[1]
 *    Flip:   s_active=1 -> GUI đọc s_buf[1] (mới), Sensor ghi s_buf[0]
 */
typedef struct
{
    DateTime dt;   /**< Thời gian đọc được từ DS1307 */
    bool valid;    /**< true nếu đọc thành công, false nếu RTC lỗi/dừng */
} RtcSnapshot;

static RtcSnapshot s_buf[2];          /**< Hai buffer cho double-buffer pattern */
static volatile uint8_t s_active;     /**< Index (0/1) của buffer mà GUI đang đọc */

/* ====================================================================
 *  SET REQUEST FLOW (Luồng yêu cầu cài giờ)
 * ====================================================================
 *
 *  GUI không truy cập I2C trực tiếp (vì I2C mutex đang bị sensor task giữ).
 *  Thay vào đó:
 *
 *    Bước 1: GUI gọi RequestSet(&newTime):
 *      - Copy giá trị DateTime vào s_setRequest.
 *      - Set s_setPending = true (cờ báo cho sensor task biết có yêu cầu).
 *      - Thứ tự ghi: dữ liệu TRƯỚC, cờ SAU -> sensor task luôn thấy dữ liệu hợp lệ.
 *
 *    Bước 2: Sensor task gọi ProcessPendingSet():
 *      - Kiểm tra s_setPending == true?
 *      - Nếu có: copy s_setRequest vào biến cục bộ, gọi DS1307_SetDateTime().
 *      - Ghi kết quả (thành công/lỗi) vào s_setResult.
 *      - Tăng s_setGeneration (để GUI biết yêu cầu đã hoàn tất).
 *      - Xóa s_setPending = false.
 *      - Nếu ghi thành công: gọi Poll() lại để cập nhật snapshot mới nhất.
 *
 *    Bước 3: GUI gọi GetLastSetResult():
 *      - Đọc s_setResult và s_setGeneration.
 *      - Nếu generation thay đổi so với lần trước -> biết yêu cầu đã hoàn tất.
 */
static DateTime s_setRequest;                /**< DateTime mà GUI muốn cài vào RTC */
static volatile bool s_setPending;           /**< Cờ: true = có yêu cầu cài giờ chờ xử lý */
static volatile RtcStatus s_setResult = RTC_STATUS_OK; /**< Kết quả của lần cài giờ gần nhất */
static volatile uint32_t s_setGeneration;    /**< Số thứ tự yêu cầu, tăng +1 mỗi lần hoàn tất */

/**
 * @brief  Khởi tạo RTC service.
 *
 * Flow thực thi:
 *   1. Đặt cả hai buffer về invalid (chưa có dữ liệu hợp lệ).
 *   2. Xóa cờ setPending, reset generation về 0.
 *   3. Gọi DS1307_Init(): kiểm tra DS1307 có phản hồi trên I2C bus không.
 *   4. Nếu Init thành công -> gọi Poll() lần đầu để đọc thời gian hiện tại
 *      và công bố vào buffer (sau đó GUI sẽ thấy dữ liệu hợp lệ).
 */
RtcStatus RtcService_Init(I2C_HandleTypeDef* hi2c)
{
    s_active = 0U;             /* Bắt đầu với buffer 0 là active (GUI đọc buf[0]) */
    s_buf[0].valid = false;    /* Chưa có dữ liệu RTC hợp lệ */
    s_buf[1].valid = false;
    s_setPending = false;      /* Không có yêu cầu cài giờ nào chờ xử lý */
    s_setGeneration = 0U;     /* Reset số thứ tự yêu cầu */

    /* DS1307_Init sẽ ping DS1307 qua I2C (HAL_I2C_IsDeviceReady) */
    RtcStatus s = DS1307_Init(hi2c);
    if (s == RTC_STATUS_OK)
    {
        /* Đọc lần đầu: điền buffer[1] (non-active) rồi flip -> buffer[0] có dữ liệu */
        RtcService_Poll();
    }
    return s;
}

/**
 * @brief  Đọc RTC DS1307 và công bố snapshot qua double-buffer.
 *
 * Flow thực thi (chỉ gọi từ sensor task, đang giữ I2C mutex):
 *   1. DS1307_ReadDateTime(): đọc 7 byte từ DS1307 (register 0x00-0x06)
 *      chứa giây, phút, giờ, thứ, ngày, tháng, năm ở dạng BCD.
 *      Chuyển đổi BCD -> nhị phân và trả về DateTime.
 *
 *   2. Tính index buffer "tiếp theo" (next = s_active XOR 1):
 *      - Nếu s_active=0 -> next=1 (ghi vào buf[1], GUI đang đọc buf[0])
 *      - Nếu s_active=1 -> next=0 (ghi vào buf[0], GUI đang đọc buf[1])
 *      => Đảm bảo KHÔNG BAO GIỜ ghi vào buffer mà GUI đang đọc.
 *
 *   3. Ghi dữ liệu vào s_buf[next]: DateTime + cờ valid.
 *      - valid=true nếu đọc thành công, false nếu RTC dừng/lỗi.
 *
 *   4. Flip s_active = next: "publish" buffer mới.
 *      - Chỉ cần ghi 1 byte (uint8_t) là atomic trên ARM -> GUI thấy ngay.
 *      - Sau dòng này, GetSnapshot() sẽ đọc được dữ liệu mới nhất.
 */
void RtcService_Poll(void)
{
    DateTime dt;
    /* Đọc 7 byte từ DS1307 register 0x00-0x06, chuyển BCD -> nhị phân */
    const RtcStatus s = DS1307_ReadDateTime(&dt);

    /* Tính index buffer "tiếp theo" (luôn là index đối diện với s_active) */
    const uint8_t next = (uint8_t)(s_active ^ 1U);

    /* Ghi vào buffer KHÔNG active (an toàn, GUI không đọc buffer này) */
    s_buf[next].dt = dt;
    s_buf[next].valid = (s == RTC_STATUS_OK);   /* RTC dừng/lỗi -> đánh giá không hợp lệ */

    /* "Publish": flip index -> GUI GetSnapshot() sẽ đọc buffer mới này */
    s_active = next;
}

/**
 * @brief  Xử lý yêu cầu cài giờ đang chờ (chỉ gọi từ sensor task).
 *
 * Flow thực thi:
 *   1. Kiểm tra s_setPending: nếu false -> không có yêu cầu -> return ngay.
 *   2. Copy s_setRequest vào biến cục bộ req (bảo vệ khỏi race condition:
 *      có thể GUI đang viết RequestSet ở luồng khác).
 *   3. Gọi DS1307_SetDateTime(&req):
 *      - Validate DateTime (ngày hợp lệ, năm nhuận...)
 *      - Mã hóa nhị phân -> BCD
 *      - Ghi 7 byte vào DS1307 register 0x00-0x06 qua I2C
 *      - Đọc lại và xác minh (readback verification)
 *   4. Lưu kết quả (thành công/lỗi) vào s_setResult.
 *   5. Tăng s_setGeneration: GUI polling GetLastSetResult() sẽ thấy
 *      generation thay đổi -> biết yêu cầu đã hoàn tất.
 *   6. Xóa s_setPending = false (đánh dấu đã xử lý xong).
 *   7. Nếu ghi thành công -> gọi Poll() lại để snapshot chứa thời gian mới nhất.
 */
void RtcService_ProcessPendingSet(void)
{
    /* Không có yêu cầu chờ xử lý -> thoát ngay */
    if (!s_setPending)
    {
        return;
    }

    /* Copy vào biến cục bộ: đề phòng GUI đang ghi s_setRequest ở luồng khác */
    DateTime req = s_setRequest;

    /* Ghi vào DS1307: validate -> mã hóa BCD -> I2C write -> readback verify */
    const RtcStatus r = DS1307_SetDateTime(&req);

    s_setResult = r;            /* Lưu kết quả để GUI đọc qua GetLastSetResult() */
    ++s_setGeneration;          /* Tăng generation -> GUI biết yêu cầu đã hoàn tất */
    s_setPending = false;       /* Xóa cờ: đã xử lý xong */

    if (r == RTC_STATUS_OK)
    {
        /* Đọc lại RTC và cập nhật snapshot: GUI sẽ thấy thời gian mới nhất */
        RtcService_Poll();
    }
}

/**
 * @brief  GUI đọc thời gian snapshot mới nhất (KHÔNG cần I2C mutex).
 *
 * Flow thực thi (gọi từ GUI thread):
 *   1. Đọc s_active vào biến cục bộ idx (snapshot index, chỉ đọc 1 byte -> atomic).
 *   2. Copy DateTime và valid từ s_buf[idx] ra tham số đầu ra.
 *
 * Tại sao an toàn?
 *   - GUI chỉ đọc buffer active, sensor task chỉ ghi buffer non-active.
 *   - s_active là uint8_t, đọc/ghi atomic trên ARM Cortex-M (no lock needed).
 *   - DateTime struct (~10 byte) có thể không atomic khi copy, nhưng trong
 *     thực tế GUI chỉ cần đọc "gần đúng" (thời gian thay đổi từng giây).
 *   - Nếu cần chính xác 100%, GUI có thể đọc 2 lần và so sánh.
 */
void RtcService_GetSnapshot(DateTime* dateTime, bool* valid)
{
    /* Đọc index hiện tại: buffer nào đang là "mới nhất" */
    const uint8_t idx = s_active;

    if (dateTime != 0)
    {
        *dateTime = s_buf[idx].dt;     /* Copy DateTime ra ngoài */
    }
    if (valid != 0)
    {
        *valid = s_buf[idx].valid;     /* true = RTC hoạt động tốt */
    }
}

/**
 * @brief  GUI gửi yêu cầu cài giờ mới (KHÔNG cần I2C mutex).
 *
 * Flow thực thi (gọi từ GUI thread):
 *   1. Kiểm tra dateTime != NULL.
 *   2. Copy DateTime vào s_setRequest (bộ nhớ chung với sensor task).
 *   3. Set s_setPending = true -> sensor task ProcessPendingSet() sẽ xử lý.
 *
 * THỨ TỰ QUAN TRỌNG: ghi dữ liệu TRƯỚC, set cờ SAU.
 *   - s_setRequest = *dateTime (ghi dữ liệu)
 *   - s_setPending = true (set cờ publish)
 *   => Sensor task chỉ thấy s_setPending=true SAU KHI dữ liệu đã đầy đủ.
 *   => Đây là kỹ thuật "publish sau dữ liệu" (data-first publish).
 */
void RtcService_RequestSet(const DateTime* dateTime)
{
    if (dateTime == 0)
    {
        return;
    }
    s_setRequest = *dateTime;     /* Copy DateTime vào bộ nhớ chung (trước khi set cờ) */
    s_setPending = true;          /* Set cờ: sensor task sẽ xử lý ở lần Poll tiếp theo */
}

/**
 * @brief  GUI đọc kết quả của yêu cầu cài giờ gần nhất.
 *
 * Flow thực thi (gọi từ GUI thread):
 *   1. Đọc s_setResult: trạng thái của lần ghi RTC gần nhất (OK / lỗi I2C / ...).
 *   2. Đọc s_setGeneration: số thứ tự yêu cầu, tăng +1 mỗi khi ProcessPendingSet()
 *      hoàn tất một lần ghi.
 *
 * Cách GUI sử dụng để biết yêu cầu đã hoàn tất:
 *   - Lưu generation hiện tại: prev_gen = s_setGeneration.
 *   - Gửi RequestSet().
 *   - Polling GetLastSetResult() mỗi frame.
 *   - Khi generation > prev_gen -> yêu cầu đã được xử lý.
 *   - Đọc status để biết thành công hay thất bại.
 *
 * Tại sao dùng generation thay vì cờ done?
 *   - Nếu GUI gửi 2 yêu cầu liên tiếp, cờ done sẽ bị reset.
 *   - Generation luôn tăng -> GUI biết ĐÚNG BAO NHIÊU yêu cầu đã hoàn tất.
 */
void RtcService_GetLastSetResult(RtcStatus* status, uint32_t* generation)
{
    if (status != 0)
    {
        *status = s_setResult;          /* OK, I2C_ERROR, READBACK_MISMATCH, ... */
    }
    if (generation != 0)
    {
        *generation = s_setGeneration;  /* Số thứ tự: tăng +1 mỗi lần ghi xong */
    }
}
