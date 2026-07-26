# Hướng dẫn Filter Chain cho tín hiệu PPG

## Tổng quan

Filter chain là kỹ thuật nối nhiều bộ lọc liên tiếp để xử lý tín hiệu PPG (photoplethysmogram). Mỗi lọc xử lý một loại nhiễu cụ thể, giúp tín hiệu đầu ra sạch hơn so với việc chỉ dùng một lọc đơn lẻ.

## Cấu trúc lọc

```
Raw Signal → Median Filter → Lowpass Filter → Clean Signal
```

| Bước | Lọc | Vai trò |
|------|-----|---------|
| 1 | Median Filter | Loại bỏ nhiễu xung (spike) từ vận động |
| 2 | Lowpass Filter | Loại bỏ nhiễu tần số cao còn lại |

## Tại sao cần nối chuỗi?

### Nếu chỉ dùng Median Filter
- ✅ Loại bỏ spike tốt
- ❌ Vẫn còn nhiễu tần số cao
- ❌ Tín hiệu chưa đủ mượt

### Nếu chỉ dùng Lowpass Filter
- ✅ Loại bỏ nhiễu tần số cao
- ❌ Peak bị mờ khi có spike
- ❌ Không xử lý được nhiễu xung

### Nếu nối chuỗi (Median → Lowpass)
- ✅ Loại bỏ spike trước (tránh peak bị mờ)
- ✅ Loại bỏ nhiễu tần số cao sau
- ✅ Tín hiệu sạch, giữ nguyên shape của peak

## Thông số khuyến nghị

### Median Filter
- **Window size (N):** 5, 7, hoặc 9 (nên dùng số lẻ)
- N越大 → loại bỏ spike tốt hơn nhưng chậm hơn
- Khuyến nghị: **N = 5** cho PPG

### Lowpass Filter
- **Cutoff frequency (fc):** 4-5 Hz
- **Sample rate (fs):** 100 Hz (hoặc tần số lấy mẫu thực tế)
- **Order:** 2 (Butterworth)
- fc越低 → tín hiệu mượt hơn nhưng có thể mất peak

## Ví dụ cấu hình

```c
// Median Filter: window size = 5
#define MEDIAN_WINDOW_SIZE  5

// Lowpass Filter: Butterworth order 2, fc = 4Hz, fs = 100Hz
// Hệ số tính bằng Python:
//   from scipy.signal import butter
//   b, a = butter(N=2, Wn=4.0/(100.0/2), btype='low')
#define LP_B0  0.020083F
#define LP_B1  0.040167F
#define LP_B2  0.020083F
#define LP_A1 -1.561018F
#define LP_A2  0.641352F
```

## Luồng xử lý

```
1. Đọc sample mới từ cảm biến MAX30102
       ↓
2. Đưa vào Median Filter
   - Loại bỏ spike (nhiễu xung)
   - Trả về giá trị trung vị
       ↓
3. Đưa vào Lowpass Filter
   - Loại bỏ nhiễu tần số cao
   - Trả về tín hiệu mượt
       ↓
4. Tín hiệu sạch → sử dụng cho:
   - Hiển thị waveform
   - Tính BPM
   - Tính SpO2
```

## Code ví dụ (C)

```c
#include "median_filter.h"
#include "lowpass_filter.h"

// Bộ nhớ tĩnh
static int32_t medianBuf[MEDIAN_WINDOW_SIZE];
static int32_t medianSortBuf[MEDIAN_WINDOW_SIZE];

// Filter instances
static MedianFilter medianFilter;
static LowpassFilter lowpassFilter;

// Khởi tạo filter chain
void FilterChain_Init(void)
{
    Median_Init(&medianFilter, medianBuf, medianSortBuf, MEDIAN_WINDOW_SIZE);
    Lowpass_Init(&lowpassFilter);
}

// Xử lý 1 sample qua filter chain
int32_t FilterChain_Process(int32_t rawSample)
{
    // Bước 1: Median Filter (loại bỏ spike)
    int32_t afterMedian = Median_Process(&medianFilter, rawSample);

    // Bước 2: Lowpass Filter (loại bỏ nhiễu tần số cao)
    int32_t afterLowpass = Lowpass_Process(&lowpassFilter, afterMedian);

    return afterLowpass;
}

// Reset filter chain
void FilterChain_Reset(void)
{
    Median_Reset(&medianFilter);
    Lowpass_Reset(&lowpassFilter);
}
```

## So sánh hiệu quả

| Tiêu chí | Chỉ Median | Chỉ Lowpass | Chain (Median → Lowpass) |
|----------|------------|-------------|--------------------------|
| Loại bỏ spike | ⭐⭐⭐ | ⭐ | ⭐⭐⭐ |
| Loại bỏ nhiễu HF | ⭐ | ⭐⭐⭐ | ⭐⭐⭐ |
| Giữ nguyên peak | ⭐⭐⭐ | ⭐ | ⭐⭐⭐ |
| Độ trễ | Thấp | Thấp | Trung bình |
| CPU usage | Thấp | Thấp | Trung bình |

## Lưu ý

1. **Thứ tự quan trọng:** Median phải đi trước Lowpass
   - Nếu Lowpass đi trước → spike bị lan rộng, peak bị mờ
   - Median đi trước → spike biến mất trước khi Lowpass xử lý

2. **Window size của Median:** Nên dùng số lẻ (5, 7, 9)
   - Số chẵn → median có thể không phải giá trị thực tế

3. **Cutoff frequency của Lowpass:** Không quá thấp
   - PPG có tần số chủ yếu 0.5-4 Hz
   - fc quá thấp sẽ lọc mất tín hiệu hữu ích

## Tài liệu tham khảo

- Elgendi, M. (2012). "On the analysis of finger photoplethysmogram signals." Current Cardiology Reviews, 8(1), 24-36.
- Madgavkar, S. et al. (2019). "A Review on Signal Processing Techniques for PPG." IEEE Access.
