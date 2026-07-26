# Hướng dẫn từng bước — HeNhung_HELLO

## Mục lục

1. [Bước 1: Tạo Median Filter](#bước-1-tạo-median-filter)
2. [Bước 2: Tạo Low-pass Filter](#bước-2-tạo-low-pass-filter)
3. [Bước 3: Tích hợp 2 filter mới vào PPG Engine](#bước-3-tích-hợp-2-filter-mới-vào-ppg-engine)
4. [Bước 4: Sửa WaveformView — hiển thị 4 chế độ lọc](#bước-4-sửa-waveformview--hiển-thị-4-chế-độ-lọc)
5. [Bước 5: Đơn giản hóa giao diện — B1 chuyển màn hình](#bước-5-đơn-giản-hóa-giao-diện--b1-chuyển-màn-hình)
6. [Bước 6: Chỉnh sửa màu sắc giao diện](#bước-6-chỉnh-sửa-màu-sắc-giao-diện)

---

## Bước 1: Tạo Median Filter

### 1.1 — Tạo file `DSP/median_filter.h`

```c
#ifndef MEDIAN_FILTER_H
#define MEDIAN_FILTER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bộ lọc trung vị (median filter) trên cửa sổ cố định.
 *
 * Loại bỏ nhiễu xung (spikes) mà không làm mờ peak.
 * Nên dùng N lẻ (5, 7, 9...) để median luôn là 1 giá trị có thật.
 * O(N log N) mỗi sample do sắp xếp, nhưng N nhỏ nên chấp nhận được.
 */

typedef struct
{
    int32_t* buffer;      /* Ring buffer (circular) */
    int32_t* sortBuf;     /* Buffer phụ cho phép sắp xếp mà không mất thứ tự */
    size_t   capacity;    /* Kích thước cửa sổ N (nên lẻ) */
    size_t   count;       /* Số sample đã thấy */
    size_t   writeIndex;  /* Vị trí ghi tiếp */
} MedianFilter;

/**
 * @brief Khởi tạo filter.
 * @param f        Filter cần khởi tạo.
 * @param buf      Buffer tĩnh N phần tử int32_t.
 * @param sortBuf  Buffer phụ N phần tử (dùng cho sắp xếp).
 * @param n        Kích thước cửa sổ (nên lẻ: 5, 7, 9...).
 */
void Median_Init(MedianFilter* f, int32_t* buf, int32_t* sortBuf, size_t n);

/** @brief Xóa toàn bộ dữ liệu trong filter. */
void Median_Reset(MedianFilter* f);

/**
 * @brief Đẩy một sample vào và trả về giá trị trung vị.
 * @param f     Filter.
 * @param input Sample mới.
 * @return Giá trị trung vị của cửa sổ hiện tại.
 */
int32_t Median_Process(MedianFilter* f, int32_t input);

/**
 * @brief True khi đã đủ N sample (cửa sổ đầy).
 * Khi chưa đầy, median lấy trên fewer samples (vẫn hợp lý nhưng chưa ổn định).
 */
bool Median_IsReady(const MedianFilter* f);

#ifdef __cplusplus
}
#endif

#endif /* MEDIAN_FILTER_H */
```

### 1.2 — Tạo file `DSP/median_filter.c`

```c
#include "median_filter.h"
#include <string.h>  /* memcpy */

/* ---- Internal: insertion sort on sortBuf ---- */
static void insertionSort(int32_t* arr, size_t n)
{
    for (size_t i = 1U; i < n; ++i)
    {
        int32_t key = arr[i];
        size_t j = i;
        while (j > 0U && arr[j - 1U] > key)
        {
            arr[j] = arr[j - 1U];
            --j;
        }
        arr[j] = key;
    }
}

void Median_Init(MedianFilter* f, int32_t* buf, int32_t* sortBuf, size_t n)
{
    if (f == NULL || buf == NULL || sortBuf == NULL || n == 0U) { return; }
    f->buffer    = buf;
    f->sortBuf   = sortBuf;
    f->capacity  = n;
    f->count     = 0U;
    f->writeIndex= 0U;
    memset(buf, 0, n * sizeof(int32_t));
}

void Median_Reset(MedianFilter* f)
{
    if (f == NULL) { return; }
    f->count      = 0U;
    f->writeIndex = 0U;
    memset(f->buffer, 0, f->capacity * sizeof(int32_t));
}

int32_t Median_Process(MedianFilter* f, int32_t input)
{
    if (f == NULL || f->buffer == NULL || f->capacity == 0U) { return 0; }

    /* Ghi sample mới vào ring buffer */
    f->buffer[f->writeIndex] = input;
    f->writeIndex = (f->writeIndex + 1U) % f->capacity;
    if (f->count < f->capacity) { ++f->count; }

    /* Copy sang sortBuf và sắp xếp */
    memcpy(f->sortBuf, f->buffer, f->count * sizeof(int32_t));
    insertionSort(f->sortBuf, f->count);

    /* Lấy median (phần tử giữa) */
    return f->sortBuf[f->count / 2U];
}

bool Median_IsReady(const MedianFilter* f)
{
    return (f != NULL) && (f->count >= f->capacity);
}
```

### 1.3 — Thêm vào file `DSP/CMakeLists.txt` hoặc `Makefile`

Nếu project dùng CMake, thêm:
```cmake
${CMAKE_CURRENT_SOURCE_DIR}/DSP/median_filter.c
```

Nếu dùng Makefile, thêm `median_filter.o` vào danh sách object files.

> **Kiểm tra:** Compile thử — không có lỗi là được. Filter này test độc lập được bằng cách in giá trị ra console.

---

## Bước 2: Tạo Low-pass Filter

### 2.1 — Tạo file `DSP/lowpass_filter.h`

```c
#ifndef LOWPASS_FILTER_H
#define LOWPASS_FILTER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Butterworth low-pass filter bậc 2 (IIR, Direct Form II Transposed).
 *
 * Dùng để loại bỏ nhiễu tần số cao (> 4 Hz) khỏi tín hiệu PPG.
 * Tần số cắt 4 Hz phù hợp cho PPG (tín hiệu hữu ích: 0.5–4 Hz).
 * Sample rate mặc định 100 Hz (PPG_SAMPLE_RATE_HZ).
 *
 * O(1) mỗi sample, 5 phép tính điểm dấu phẩy động.
 * KHÔNG cấp phát bộ nhớ động.
 */

typedef struct
{
    /* Hệ số filter (tính offline, hard-code) */
    float b0, b1, b2;   /* Numerator coefficients */
    float a1, a2;        /* Denominator coefficients (a0 = 1.0) */

    /* Trạng thái (Direct Form II Transposed) */
    float w1, w2;        /* Đọc/thư ký nội bộ */
} LowpassFilter;

/**
 * @brief Khởi tạo filter với hệ số Butterworth bậc 2, fc=4 Hz, fs=100 Hz.
 * @param f Filter cần khởi tạo.
 */
void Lowpass_Init(LowpassFilter* f);

/**
 * @brief Khởi tạo filter với hệ số tùy chỉnh.
 * @param f  Filter.
 * @param b0, b1, b2  Hệ số numerator.
 * @param a1, a2      Hệ số denominator.
 */
void Lowpass_InitCoeffs(LowpassFilter* f, float b0, float b1, float b2,
                        float a1, float a2);

/** @brief Xóa trạng thái filter (giữ nguyên hệ số). */
void Lowpass_Reset(LowpassFilter* f);

/**
 * @brief Xử lý một sample.
 * @param f     Filter.
 * @param input Giá trị vào (int32_t, sẽ được ép về float rồi lại ép về int32_t).
 * @return Giá trị đã lọc.
 */
int32_t Lowpass_Process(LowpassFilter* f, int32_t input);

#ifdef __cplusplus
}
#endif

#endif /* LOWPASS_FILTER_H */
```

### 2.2 — Tạo file `DSP/lowpass_filter.c`

```c
#include "lowpass_filter.h"

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
```

> **Kiểm tra:** Compile thử. Có thể test bằng cách cho input là sin wave, kiểm tra output bị attenuate ở tần số > 4 Hz.

---

## Bước 3: Tích hợp 2 filter mới vào PPG Engine

### 3.1 — Sửa `Config/ppg_config.h`

Mở file `Config/ppg_config.h`, tìm enum `PpgFilterMode` (hoặc tìm `PPG_FILTER_RAW`) và **thay entire enum**:

```c
/* Nguồn tín hiệu phân tích/hiển thị chọn được (toàn cục).
   SpO2 luôn dùng RAW RED/IR bất kể mode này. */
typedef enum
{
    PPG_FILTER_RAW = 0,           /* Tín hiệu RAW đã centered (không lọc). */
    PPG_FILTER_MOVING_AVERAGE,    /* Moving average (làm mượt). */
    PPG_FILTER_MEDIAN,            /* Median filter (loại spike). */
    PPG_FILTER_LOWPASS            /* Low-pass Butterworth (loại nhiễu cao tần). */
} PpgFilterMode;
```

**LƯU Ý:** Nếu enum này nằm trong `DSP/ppg_types.h` thay vì `ppg_config.h`, thì sửa ở đó.

### 3.2 — Sửa `DSP/ppg_measurement.c`

**Thêm include ở đầu file:**

```c
#include "median_filter.h"
#include "lowpass_filter.h"
```

**Thêm biến tĩnh (khu vực "/* Trạng thái engine */"):**

```c
/* Median filter (RED + IR) */
static int32_t s_medianBufIr[PPG_MA_WINDOW_MAX];
static int32_t s_medianSortIr[PPG_MA_WINDOW_MAX];
static MedianFilter s_medianIr;

static int32_t s_medianBufRed[PPG_MA_WINDOW_MAX];
static int32_t s_medianSortRed[PPG_MA_WINDOW_MAX];
static MedianFilter s_medianRed;

/* Low-pass filter (RED + IR) */
static LowpassFilter s_lpIr;
static LowpassFilter s_lpRed;
```

**Sửa hàm `Ppg_Init()` — thêm sau phần khởi tạo moving average:**

```c
/* Median filter init */
Median_Init(&s_medianIr, s_medianBufIr, s_medianSortIr, s_maWindowN);
Median_Init(&s_medianRed, s_medianBufRed, s_medianSortRed, s_maWindowN);

/* Low-pass filter init */
Lowpass_Init(&s_lpIr);
Lowpass_Init(&s_lpRed);
```

**Sửa hàm `Ppg_SetMaWindow()` — thêm reset cho 2 filter mới:**

```c
void Ppg_SetMaWindow(uint8_t window)
{
    if (window == 0U || window > PPG_MA_WINDOW_MAX) { return; }
    s_maWindowN = window;
    MovingAverage_Reset(&s_maIr);
    MovingAverage_Reset(&s_maRed);
    Median_Reset(&s_medianIr);      /* THÊM */
    Median_Reset(&s_medianRed);     /* THÊM */
    Lowpass_Reset(&s_lpIr);         /* THÊM */
    Lowpass_Reset(&s_lpRed);        /* THÊM */
}
```

**Sửa hàm `Ppg_PushSample()` — tìm phần xử lý filter mode (gần cuối phần "MEASURING")**

Tìm đoạn code tương tự:

```c
/* Hiện tại chỉ có 2 case */
if (s_filterMode == PPG_FILTER_MOVING_AVERAGE)
{
    (void)MovingAverage_Process(&s_maIr, centeredIr, &filteredIr);
    // ...
}
else
{
    filteredIr = centeredIr;
    // ...
}
```

**Thay bằng 4 case:**

```c
/* Lọc tín hiệu IR theo chế độ đã chọn */
int32_t filteredIr;
int32_t filteredRed;

switch (s_filterMode)
{
case PPG_FILTER_MOVING_AVERAGE:
    (void)MovingAverage_Process(&s_maIr,  centeredIr,  &filteredIr);
    (void)MovingAverage_Process(&s_maRed, centeredRed, &filteredRed);
    break;

case PPG_FILTER_MEDIAN:
    filteredIr  = Median_Process(&s_medianIr,  centeredIr);
    filteredRed = Median_Process(&s_medianRed, centeredRed);
    break;

case PPG_FILTER_LOWPASS:
    filteredIr  = Lowpass_Process(&s_lpIr,  centeredIr);
    filteredRed = Lowpass_Process(&s_lpRed, centeredRed);
    break;

case PPG_FILTER_RAW:
default:
    filteredIr  = centeredIr;
    filteredRed = centeredRed;
    break;
}
```

> **LƯU Ý QUAN TRỌNG:** Bạn cần tìm chính xác vị trí trong `Ppg_PushSample()` mà filteredIr/filteredRed được gán. Code hiện tại có thể dùng `if/else` thay vì `switch`. Hãy đọc kỹ file `ppg_measurement.c` (khoảng dòng 250-400) để tìm đúng chỗ cần sửa.

---

## Bước 4: Sửa WaveformView — hiển thị 4 chế độ lọc

### 4.1 — Sửa `TouchGFX/gui/.../WaveformView.cpp`

**Tìm hàm `onFilterMode()` và thay đổi logic toggle:**

Hiện tại code chỉ toggle giữa 2 chế độ:

```cpp
void WaveformView::onFilterMode(const TextButton&)
{
    const FilterMode next = (filterMode == FilterMode::Raw)
                            ? FilterMode::MovingAverage : FilterMode::Raw;
    // ...
}
```

**Thay bằng cycle qua 4 chế độ:**

```cpp
void WaveformView::onFilterMode(const TextButton&)
{
    /* Cycle: Raw → MovingAverage → Median → Lowpass → Raw */
    int next = static_cast<int>(filterMode) + 1;
    if (next > static_cast<int>(FilterMode::Lowpass))
    {
        next = static_cast<int>(FilterMode::Raw);
    }
    filterMode = static_cast<FilterMode>(next);
    presenter->postCommand(makeSelectFilter(filterMode));
    updateControlLabels(filterMode, maWindow);
    refresh();
}
```

**Sửa hàm `updateControlLabels()` — hiển thị tên filter đầy đủ:**

```cpp
void WaveformView::updateControlLabels(FilterMode mode, uint8_t window)
{
    const char* label;
    switch (mode)
    {
    case FilterMode::Raw:            label = "Raw";      break;
    case FilterMode::MovingAverage:  label = "MovAvg";   break;
    case FilterMode::Median:         label = "Median";   break;
    case FilterMode::Lowpass:        label = "LowPass";  break;
    default:                         label = "Raw";      break;
    }
    modeButton.setLabel(label);

    char buf[8];
    (void)snprintf(buf, sizeof(buf), "N %u", (unsigned)window);
    windowButton.setLabel(buf);
    modeButton.invalidate();
    windowButton.invalidate();
}
```

### 4.2 — Đảm bảo enum `FilterMode` trong GUI khớp với DSP

Tìm file `TouchGFX/gui/include/gui/common/GuiTypes.hpp` hoặc `GuiSnapshots.hpp` và kiểm tra enum `FilterMode`. Nếu chưa có `Median` và `Lowpass`, thêm vào:

```cpp
enum class FilterMode : uint8_t
{
    Raw = 0,
    MovingAverage,
    Median,
    Lowpass
};
```

Đồng thời đảm bảo hàm `makeSelectFilter()` trong `GuiCommands.hpp` gửi đúng enum value.

---

## Bước 5: Đơn giản hóa giao diện — B1 chuyển màn hình

### 5.1 — Sửa `DashboardView.cpp` — thêm B1 chuyển sang Waveform

Mở file `TouchGFX/gui/src/dashboard_screen/DashboardView.cpp`.

**Thêm include ở đầu file:**

```cpp
#include "physical_input_service.h"
```

**Sửa hàm `handleTickEvent()`:**

```cpp
void DashboardView::handleTickEvent()
{
    ++tickCounter;

    /* Kiểm tra nút B1 → chuyển sang Waveform */
    PhysicalInputEvent evt;
    if (PhysicalInput_GetEvent(&evt))
    {
        if (evt == PHYSICAL_INPUT_EVENT_B1_PRESSED)
        {
            FrontendApplication* app = static_cast<FrontendApplication*>(
                Application::getInstance());
            app->requestScreen(ScreenId::Waveform);
            return;
        }
    }

    /* Refresh dữ liệu định kỳ */
    if ((tickCounter % REFRESH_DIVISOR) == 0U)
    {
        refresh();
    }
}
```

### 5.2 — Sửa `WaveformView.cpp` — thêm B1 chuyển về Dashboard

Mở file `TouchGFX/gui/src/waveform_screen/WaveformView.cpp`.

**Thêm include ở đầu file:**

```cpp
#include "physical_input_service.h"
```

**Sửa hàm `handleTickEvent()`:**

```cpp
void WaveformView::handleTickEvent()
{
    ++tickCounter;

    /* Kiểm tra nút B1 → chuyển về Dashboard */
    PhysicalInputEvent evt;
    if (PhysicalInput_GetEvent(&evt))
    {
        if (evt == PHYSICAL_INPUT_EVENT_B1_PRESSED)
        {
            FrontendApplication* app = static_cast<FrontendApplication*>(
                Application::getInstance());
            app->requestScreen(ScreenId::Dashboard);
            return;
        }
    }

    /* Refresh waveform định kỳ */
    if ((tickCounter % REFRESH_DIVISOR) == 0U)
    {
        refresh();
    }
}
```

### 5.3 — Sửa `BootView.cpp` — tự vào Dashboard sau khi boot

Mở file `TouchGFX/gui/src/boot_screen/BootView.cpp`.

Tìm phần auto-navigate (thường là sau khi progress bar đầy). Đảm bảo nó gọi:

```cpp
FrontendApplication* app = static_cast<FrontendApplication*>(
    Application::getInstance());
app->requestScreen(ScreenId::Dashboard);
```

### 5.4 — Bỏ qua HomeView (không cần sửa)

HomeView vẫn tồn tại trong code nhưng không cần dùng. Khi boot xong sẽ nhảy thẳng vào Dashboard. Bấm B1 chuyển giữa Dashboard ↔ Waveform.

> **Kiểm tra:** Build và flash. Boot → thấy Dashboard → bấm B1 → thấy Waveform → bấm B1 → thấy Dashboard.

---

## Bước 6: Chỉnh sửa màu sắc giao diện

### 6.1 — Sửa `TouchGFX/gui/include/gui/common/GuiTheme.hpp`

Đây là file trung tâm điều khiển màu sắc. Thay đổi giá trị RGB theo ý muốn:

**Ví dụ 1 — Giao diện sáng (nền trắng/xám nhạt):**

```cpp
/* Nền */
inline colortype background()   { return Color::getColorFromRGB(240, 243, 248); }
inline colortype surface()      { return Color::getColorFromRGB(255, 255, 255); }
inline colortype surfaceAlt()   { return Color::getColorFromRGB(230, 235, 242); }
inline colortype statusBar()    { return Color::getColorFromRGB(200, 210, 225); }

/* Primary */
inline colortype primary()      { return Color::getColorFromRGB(30, 100, 220); }
inline colortype primaryDark()  { return Color::getColorFromRGB(20, 70, 160); }

/* Trạng thái */
inline colortype ok()           { return Color::getColorFromRGB(34, 170, 85); }
inline colortype warning()      { return Color::getColorFromRGB(240, 150, 0); }
inline colortype error()        { return Color::getColorFromRGB(220, 50, 50); }

/* Chữ */
inline colortype textPrimary()  { return Color::getColorFromRGB(30, 35, 45); }
inline colortype textSecondary(){ return Color::getColorFromRGB(100, 110, 130); }
```

**Ví dụ 2 — Giao diện y tế (nền xanh đậm, kiểu monitor):**

```cpp
/* Nền */
inline colortype background()   { return Color::getColorFromRGB(10, 15, 30); }
inline colortype surface()      { return Color::getColorFromRGB(15, 25, 45); }

/* Primary — xanh cyan */
inline colortype primary()      { return Color::getColorFromRGB(0, 200, 220); }

/* Chữ */
inline colortype textPrimary()  { return Color::getColorFromRGB(200, 255, 255); }
```

### 6.2 — Sửa font chữ (nếu muốn)

Mở file `.f TouchGFX Designer` (file `.touchgfx` trong thư mục TouchGFX) và sửa trong phần Typography. Hoặc sửa trong Designer GUI rồi Generate lại.

---

## Tổng kết thứ tự làm

```
Bước 1: median_filter.h + .c        (tạo mới, ~60 dòng)
Bước 2: lowpass_filter.h + .c       (tạo mới, ~50 dòng)
Bước 3: ppg_config.h + ppg_measurement.c  (sửa, thêm ~30 dòng)
Bước 4: WaveformView.cpp            (sửa, ~20 dòng)
Bước 5: DashboardView.cpp + WaveformView.cpp  (sửa, ~10 dòng mỗi file)
Bước 6: GuiTheme.hpp                (sửa, tùy ý)
```

**Ưu tiên:** Bước 1 → 2 → 3 → 4 → 5 → 6

**Thời gian ước tính:**
- Bước 1-2: 15 phút (tạo file mới)
- Bước 3: 20 phút (tích hợp, cần đọc kỹ ppg_measurement.c)
- Bước 4: 10 phút
- Bước 5: 10 phút
- Bước 6: 5 phút (tùy sở thích)
