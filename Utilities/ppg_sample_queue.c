/**
 * @file    ppg_sample_queue.c
 * @brief   Cài đặt ring RAW-sample SPSC lock-free. Target-only.
 * @note    User-owned. Một producer (sensor task), một consumer (DSP task).
 *
 * ==========================================================================
 *  GIẢI THÍCH TỔNG QUAN CHO SINH VIÊN:
 * ==========================================================================
 *
 *  Đây là hàng đợi vòng (ring buffer) hoạt động theo mô hình SPSC
 *  (Single Producer - Single Consumer) trên vi điều khiển Cortex-M4.
 *
 *  MÔ HÌNH SPSC LÀ GÌ?
 *  - Chỉ CÓ MỘT task duy nhất được quyền GHI vào queue (producer = sensor task
 *    đọc FIFO từ cảm biến MAX30102).
 *  - Chỉ CÓ MỘT task duy nhất được quyền ĐỌC từ queue (consumer = DSP task
 *    thực hiện thuật toán đo nhịp tim/SpO2).
 *  - Vì mỗi biến (s_head, s_tail) chỉ được MỘT task ghi nên KHÔNG CẦN mutex!
 *    Mutex chỉ cần thiết khi nhiều task cùng ghi chung một biến (race condition).
 *
 *  VÌ SAO KHÔNG CẦN MUTEX?
 *  Trên Cortex-M4 (ARM Cortex-M4F):
 *  1. Các phép đọc/ghi 32-bit là ATOMIC (không bị chia đôi giữa các bus cycle).
 *  2. Biến được khai báo volatile => compiler không tối ưu bỏ reads/writes.
 *  3. ARM DMB (Data Memory Barrier) đảm bảo thứ tự ghi bộ nhớ với thiết bị
 *     ngoại vi và cache (nếu có).
 *  => Kết hợp 3 yếu tố trên, SPSC lock-free an toàn trên Cortex-M4.
 *
 *  LUỒNG DỮ LIỆU:
 *  Sensor Task (producer):
 *    MAX30102 -> ReadFifo() -> Push() -> s_ring[] -> s_head++
 *
 *  DSP Task (consumer):
 *    Pop() -> s_ring[] -> s_tail++ -> Algorithm Engine -> BPM, SpO2
 * ==========================================================================
 */

#include "ppg_sample_queue.h"
#include "stm32f4xx_hal.h"   /* __DMB() từ CMSIS */

/* --------------------------------------------------------------------------
 *  KÍCH THƯỚC HÀNG ĐỢI & MASK
 * --------------------------------------------------------------------------
 *  QUEUE_CAPACITY = 256: đây là LŨY THỪA CỦA 2 (2^8 = 256).
 *
 *  VÌ SAO PHẢI LÀ LŨY THỪA CỦA 2?
 *  - Khi chia lấy dư (modulo) cho 256, thay vì dùng phép chia % tốn resource,
 *    ta dùng phép AND bit: (index + 1) & 255.
 *  - Phép AND chỉ tốn 1 chu kỳ CPU, trong khi phép % (chia thực) có thể mất
 *    2-12 chu kỳ tùy kiến trúc.
 *  - Trên Cortex-M4 không có chia phần cứng nhanh, nên bitwise AND là trick
 *    tối ưu quan trọng cho code chạy real-time.
 *
 *  VÍ DỤ: index = 255, next = (255 + 1) & 255 = 256 & 255 = 0
 *          => quay về đầu mảng (wrap-around) mà KHÔNG cần if/else.
 * --------------------------------------------------------------------------
 */
#define QUEUE_CAPACITY   256U           /* lũy thừa của 2: 2^8 = 256 */
#define QUEUE_MASK       (QUEUE_CAPACITY - 1U)  /* 255 = 0xFF, dùng cho phép AND */

static PpgRawSample s_ring[QUEUE_CAPACITY];  /* mảng vòng chứa các mẫu thô PPG */

/* --------------------------------------------------------------------------
 *  BIẾN HEAD VÀ TAIL
 * --------------------------------------------------------------------------
 *  s_head: chỉ SENSOR TASK (producer) được quyền ghi.
 *          Là chỉ số ghi tiếp theo trong mảng s_ring[].
 *
 *  s_tail: chỉ DSP TASK (consumer) được quyền ghi.
 *          Là chỉ số đọc tiếp theo trong mảng s_ring[].
 *
 *  QUAN TRỌNG: volatile báo compiler KHÔNG được tối ưu hóa việc đọc biến này.
 *  Ví dụ: khi đọc s_tail trong Push(), compiler PHẢI đọc từ bộ nhớ RAM
 *  mỗi lần, không được dùng giá trị cached từ lần đọc trước.
 *  Vì DSP task đang chạy song song, s_tail có thể thay đổi bất cứ lúc nào.
 *
 *  s_head == s_tail  => hàng đợi RỖNG (không có mẫu để đọc).
 *  (s_head + 1) & MASK == s_tail => hàng đợi ĐẦY (không còn slot trống).
 * --------------------------------------------------------------------------
 */
static volatile uint32_t s_head;        /* chỉ producer (sensor task) ghi */
static volatile uint32_t s_tail;        /* chỉ consumer (DSP task) ghi */
static volatile uint32_t s_dropped;     /* đếm số lần Push bị từ chì do đầy */

/**
 * @brief  Đặt lại hàng đợi về trạng thái rỗng.
 * @note   Gọi trong App_Init() TRƯỚC KHI scheduler bắt đầu.
 *         Lúc này chưa có task nào đọc/ghi nên KHÔNG CẦN DMB hay atomic.
 */
void PpgQueue_Reset(void)
{
    s_head = 0U;       /* head = 0: vị trí ghi tiếp theo là slot đầu tiên */
    s_tail = 0U;       /* tail = 0: vị trí đọc tiếp theo cũng là slot đầu tiên */
                       /* head == tail => hàng đợi rỗng */
    s_dropped = 0U;    /* reset bộ đếm số mẫu bị bỏ */
}

/**
 * @brief  Đẩy một mẫu PPG thô vào hàng đợi (chỉ sensor task gọi).
 *
 * ==========================================================================
 *  LUỒNG THỰC THI CHI TIẾT (cho sinh viên chưa quen lock-free):
 * ==========================================================================
 *
 *  BƯỚC 1 - KIỂM TRA CON TRỎ NULL:
 *    Nếu con trỏ sample == NULL => trả về false ngay, không làm gì cả.
 *
 *  BƯỚC 2 - TÍNH VỊ TRÍ TIẾP THEO:
 *    head = s_head (vị trí ghi hiện tại)
 *    next = (head + 1) & QUEUE_MASK  (vị trí ghi tiếp theo, đã wrap-around)
 *
 *  BƯỚC 3 - KIỂM TRA HÀNG ĐỢI ĐẦY:
 *    Nếu next == s_tail => hàng đợi ĐẦY (slot tiếp theo đang bị consumer đọc).
 *    Increment s_dropped và trả về false. Sample bị BỎ (drop) - đây là mất
 *    dữ liệu nhưng chấp nhận được trong real-time: quan trọng hơn là không
 *    được ghi đè lên dữ liệu mà consumer đang đọc.
 *
 *  BƯỚC 4 - GHI DỮ LIỆU:
 *    Copy toàn bộ struct sample vào s_ring[head].
 *    Lúc này dữ liệu đã nằm trong RAM nhưng consumer chưa thấy được ngay.
 *
 *  BƯỚC 5 - __DMB() - ARM DATA MEMORY BARRIER:
 *    Đây là điểm QUAN TRỌNG NHẤT trong lock-free SPSC!
 *
 *    ARM Cortex-M4 có pipelining và write buffer. Khi CPU ghi dữ liệu vào
 *    RAM, phép ghi có thể ĐƯỢC ĐẶT VÀO write buffer và chưa thực sự
 *    hoàn thành trên bus bộ nhớ khi lệnh tiếp theo chạy.
 *
 *    Nếu KHÔNG có DMB: CPU có thể ghi s_ring[head] VÀ s_head = next THEO
 *    THỨ TỰ NGẪU NHIÊN. Consumer có thể thấy s_head đã cập nhật (next)
 *    nhưng đọc s_ring[head] vẫn là dữ liệu CŨ (chưa ghi xong)!
 *
 *    DMB bắt CPU PHẢI hoàn thành TẤT CẢ phép ghi trước nó TRƯỚC KHI
 *    thực hiện bất kỳ phép ghi nào sau nó. Đảm bảo:
 *      [ghi s_ring[head]]  =>  DMB  =>  [ghi s_head = next]
 *    Consumer thấy s_head mới thì ĐẢM BẢO s_ring[head] đã sẵn sàng.
 *
 *  BƯỚC 6 - "PUBLISH" HEAD:
 *    Ghi s_head = next. Đây là tín hiệu cho consumer biết slot mới có dữ liệu.
 *    Chỉ sau dòng này, consumer mới có thể đọc được sample vừa Push.
 * ==========================================================================
 */
bool PpgQueue_Push(const PpgRawSample* sample)
{
    if (sample == 0)                     /* kiểm tra con trỏ NULL trước */
    {
        return false;
    }
    const uint32_t head = s_head;        /* snapshot vị trí ghi hiện tại */
    /* Tính vị trí ghi tiếp theo. & QUEUE_MASK tương đương % 256 nhưng nhanh hơn. */
    const uint32_t next = (head + 1U) & QUEUE_MASK;
    if (next == s_tail)                  /* slot tiếp theo đang được consumer dùng */
    {
        ++s_dropped;                     /* hàng đợi ĐẦY: sample bị bỏ (drop) */
        return false;
    }
    /* Ghi dữ liệu mẫu vào slot tại vị trí head. */
    s_ring[head] = *sample;
    __DMB();         /* ---- MEMORY BARRIER ----
                      * Bắt buộc toàn bộ phép ghi s_ring[head] hoàn thành
                      * trên bus bộ nhớ TRƯỚC KHI s_head được cập nhật.
                      * Without this, consumer có thể thấy head mới nhưng
                      * đọc phải dữ liệu cũ/undefined trong ring buffer. */
    s_head = next;   /* "publish": giờ consumer thấy được sample vừa Push */
    return true;
}

/**
 * @brief  Lấy một mẫu PPG thô ra khỏi hàng đợi (chỉ DSP task gọi).
 *
 * ==========================================================================
 *  LUỒNG THỰC THI CHI TIẾT:
 * ==========================================================================
 *
 *  BƯỚC 1 - KIỂM TRA CON TRỎ NULL.
 *
 *  BƯỚC 2 - KIỂM TRA HÀNG ĐỢI RỖNG:
 *    Nếu tail == s_head => không có mẫu nào để đọc => trả về false.
 *    (Consumer không đọc được gì, thử lại ở lần lặp tiếp.)
 *
 *  BƯỚC 3 - ĐỌC DỮ LIỆU:
 *    Copy dữ liệu từ s_ring[tail] vào buffer sample của caller.
 *
 *  BƯỚC 4 - __DMB() - ARM DATA MEMORY BARRIER:
 *    Tương tự Push: bắt buộc phép đọc hoàn thành TRƯỚC KHI tail được cập nhật.
 *
 *    Nếu KHÔNG có DMB: CPU có thể "đọc trước" (speculative read) hoặc compiler
 *    có thể reorder lệnh. DMB đảm bảo:
 *      [đọc s_ring[tail]]  =>  DMB  =>  [ghi s_tail = next]
 *    Producer thấy s_tail mới thì ĐẢM BẢO slot đó đã được đọc xong.
 *
 *  BƯỚC 5 - "GIẢI PHÓNG" SLOT:
 *    s_tail = (tail + 1) & QUEUE_MASK: đánh dấu slot vừa đọc là TRỐNG.
 *    Producer giờ có thể ghi dữ liệu mới vào slot này.
 * ==========================================================================
 */
bool PpgQueue_Pop(PpgRawSample* sample)
{
    if (sample == 0)                     /* kiểm tra con trỏ NULL */
    {
        return false;
    }
    const uint32_t tail = s_tail;        /* snapshot vị trí đọc hiện tại */
    if (tail == s_head)                  /* head == tail => hàng đợi RỖNG */
    {
        return false;                    /* không có mẫu nào, trả về false */
    }
    *sample = s_ring[tail];             /* đọc dữ liệu từ slot tại vị trí tail */
    __DMB();         /* ---- MEMORY BARRIER ----
                      * Đảm bảo phép đọc s_ring[tail] hoàn thành trên bus
                      * TRƯỚC KHI s_tail được cập nhật. Without this, producer
                      * có thể thấy tail mới và ghi đè lên slot mà consumer
                      * chưa kịp đọc xong. */
    /* Giải phóng slot: di chuyển tail đến vị trí tiếp theo. */
    s_tail = (tail + 1U) & QUEUE_MASK;
    return true;
}

/**
 * @brief  Trả về tổng số mẫu bị bỏ (drop) do hàng đợi đầy.
 * @note   Được bridge (GUI) đọc để hiển thị thông tin chẩn đoán.
 *         Nếu số này tăng liên tục => consumer (DSP task) đang xử lý
 *         chậm hơn tốc độ producer (sensor task) produce.
 */
uint32_t PpgQueue_DroppedCount(void)
{
    return s_dropped;
}
