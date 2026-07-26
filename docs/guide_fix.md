# Guide Fix — PPG Analyzer Project

## Tổng quan các lỗi đã fix

| #   | Lỗi                                                                       | Mức độ     | File ảnh hưởng                                                   |
| --- | ------------------------------------------------------------------------- | ---------- | ---------------------------------------------------------------- |
| 1   | RED channel waveform không hiển thị được                                  | Trung bình | `ppg_types.h`, `ppg_measurement.c`, `application_gui_bridge.cpp` |
| 2   | Filter mode GUI label map sai (Median/Lowpass/Med+LP hiện thành "MovAvg") | Trung bình | `application_gui_bridge.cpp`                                     |
| 3   | Settings screen chưa kết nối thật (vẫn dùng mock)                         | Thấp       | `application_gui_bridge.cpp`, `application_gui_bridge.hpp`       |
| 4   | Placeholder text chưa thay ("nam chan be du", "Coder bi nghe")            | Thấp       | `BootView.cpp`, `AboutView.cpp`, `MockGuiDataProvider.cpp`       |

---

## Fix 1: RED Channel Waveform

### Vấn đề

- `PpgResult` chỉ có 1 buffer `waveform[]` cho tín hiệu IR
- Bridge copy IR vào cả `irSamples[]` và `redSamples[]` → RED button hoạt động nhưng hiển thị dữ liệu IR
- `redChannelValid = false` → GUI không biết RED có thật không

### Giải pháp

Thêm buffer waveform RED song song với IR trong engine DSP.

### Files thay đổi

#### `DSP/ppg_types.h`

```diff
+    /* Cửa sổ waveform RED đã centered (cũ nhất..mới nhất). */
+    int16_t  redWaveform[PPG_WAVE_POINTS];
+    uint16_t redWaveformCount;
```

#### `DSP/ppg_measurement.c`

1. Thêm biến static cho RED waveform ring buffer:

```diff
+ static int16_t s_waveRed[PPG_WAVE_POINTS];
+ static uint16_t s_waveRedHead;
+ static uint16_t s_waveRedFill;
```

2. Thêm hàm `analysisRed()` (bản sao của `analysisIr()` cho kênh RED):

```c
static int32_t analysisRed(void)
{
    return (s_filterMode == PPG_FILTER_RAW) ? s_lastCenteredRed : s_lastRedFiltered;
}
```

3. Trong `resetMeasurement()`: reset thêm RED waveform buffer.

4. Trong `pushWaveformPoint()`: thêm code push RED waveform (dùng chung auto-range envelope):

```c
/* RED waveform: dùng chung auto-range envelope với IR. */
const int32_t redCentered = analysisRed();
int32_t redMapped = PPG_WAVE_ZERO + ((redCentered - mid) * span) / s_displayRange;
redMapped = clamp32(redMapped, PPG_WAVE_ZERO - span, PPG_WAVE_ZERO + span);
s_waveRed[s_waveRedHead] = (int16_t)redMapped;
s_waveRedHead = (uint16_t)((s_waveRedHead + 1U) % PPG_WAVE_POINTS);
if (s_waveRedFill < PPG_WAVE_POINTS) { ++s_waveRedFill; }
```

5. Trong `Ppg_GetResult()`: copy RED waveform ra output (giống IR).

#### `Application/application_gui_bridge.cpp`

```diff
-    snapshot.redChannelValid = false;
+    snapshot.redChannelValid = (g_sensorOk != 0);

-    // Không còn copy IR vào redSamples[]
+    // Copy redWaveform[] vào redSamples[]
```

---

## Fix 2: Filter Mode GUI Label Mapping

### Vấn đề

```cpp
// CŨ — chỉ phân biệt RAW vs "còn lại"
snapshot.filterMode = (ppg_.filterMode == PPG_FILTER_RAW) ? FilterMode::Raw
                                                         : FilterMode::MovingAverage;
```

→ Median, Lowpass, MedianLowpass đều bị map thành `MovingAverage` → nút hiện "MovAvg" sai.

### Giải pháp

Map đầy đủ 5 chế độ:

```cpp
switch (ppg_.filterMode)
{
case PPG_FILTER_RAW:            snapshot.filterMode = FilterMode::Raw;            break;
case PPG_FILTER_MOVING_AVERAGE: snapshot.filterMode = FilterMode::MovingAverage;  break;
case PPG_FILTER_MEDIAN:         snapshot.filterMode = FilterMode::Median;         break;
case PPG_FILTER_LOWPASS:        snapshot.filterMode = FilterMode::Lowpass;        break;
case PPG_FILTER_MEDIAN_LOWPASS: snapshot.filterMode = FilterMode::MedianLowpass;  break;
default:                        snapshot.filterMode = FilterMode::Raw;            break;
}
```

---

## Fix 3: Settings Screen → Real Data

### Vấn đề

- `getConfigurationSnapshot()` delegate về `MockGuiDataProvider` → giá trị settings là giả lập
- `getSystemInfoSnapshot()` cũng dùng mock → firmware version là placeholder
- Settings commands (SetBrightness, SetBuzzerEnabled, ...) bị forward sang mock

### Giải pháp

Thêm `draftConfig_` + `activeConfig_` vào `ApplicationGuiBridge`:

#### `application_gui_bridge.hpp`

```diff
+    GuiConfigurationSnapshot draftConfig_;
+    GuiConfigurationSnapshot activeConfig_;
+    bool draftDirty() const;
```

#### `application_gui_bridge.cpp`

1. Constructor khởi tạo config mặc định:

```cpp
ApplicationGuiBridge::ApplicationGuiBridge()
    : ppg_(), generation_(1U),
      draftConfig_(makeDefaultConfig()),
      activeConfig_(makeDefaultConfig())
{}
```

2. `postCommand()` xử lý settings locally:

```cpp
case GuiCommandType::SetMinimumSqi:      draftConfig_.minimumSqiPercent = ...; break;
case GuiCommandType::SetLoggingEnabled:  draftConfig_.loggingEnabled = ...;    break;
case GuiCommandType::SetBuzzerEnabled:   draftConfig_.buzzerEnabled = ...;     break;
case GuiCommandType::SetAdaptiveLedEnabled: draftConfig_.adaptiveLedEnabled = ...; break;
case GuiCommandType::SetBrightness:      draftConfig_.brightnessPercent = ...; break;
case GuiCommandType::ApplySettings:      activeConfig_ = draftConfig_;         break;
case GuiCommandType::CancelSettings:     draftConfig_ = activeConfig_;         break;
case GuiCommandType::RestoreDefaults:    draftConfig_ = makeDefaultConfig();   break;
```

3. `getConfigurationSnapshot()` trả draft config thật:

```cpp
bool ApplicationGuiBridge::getConfigurationSnapshot(GuiConfigurationSnapshot& s)
{
    s = draftConfig_;
    s.generation = generation_++;
    s.dirty = draftDirty();
    return true;
}
```

4. `getSystemInfoSnapshot()` trả thông tin thật:

```cpp
bool ApplicationGuiBridge::getSystemInfoSnapshot(GuiSystemInfoSnapshot& s)
{
    s.projectName = "PPG Signal Analyzer";
    s.firmwareVersion = "v1.0.0";
    s.buildProfile = "Release";
    s.mcu = "STM32F429ZIT6";
    s.displayResolution = "240 x 320";
    s.sensorName = "MAX30102";
    s.algorithmStatus = "Real-time";
    return true;
}
```

---

## Fix 4: Placeholder Text

### Files thay đổi

| File                          | Cũ                          | Mới                           |
| ----------------------------- | --------------------------- | ----------------------------- |
| `BootView.cpp:72`             | `"Firmware nam chan be du"` | `"Firmware v1.0.0"`           |
| `AboutView.cpp:73`            | `"Nam chan be du."`         | `"Not a medical device."`     |
| `AboutView.cpp:80`            | `"Coder bi nghe."`          | `"For educational use only."` |
| `MockGuiDataProvider.cpp:321` | `"nam chan be du"`          | `"v1.0.0"`                    |

---

## Kiểm tra sau khi fix

###编译 (Build)

```bash
# Từ thư mục gốc project
make -j4
# Hoặc build qua STM32CubeIDE /gcc/
```

### Test trên phần cứng

1. **RED channel waveform:**
   - Vào màn hình Waveform
   - Bấm nút "IR" → chuyển sang "RED"
   - Kiểm tra đồ thị RED hiển thị khác IR (màu đỏ)
   - Bấm lại chuyển về "IR" (màu cyan)

2. **Filter mode labels:**
   - Bấm nút filter mode liên tục
   - Kiểm tra lần lượt: Raw → MovAvg → Median → LowPass → Med+LP → Raw
   - Mỗi chế độ phải hiện đúng label trên nút

3. **Settings screen:**
   - Vào Settings từ Home
   - Thay đổi bất kỳ setting nào (Filter, SQI, Buzzer...)
   - Kiểm tra nút hiện "Unsaved changes"
   - Bấm Apply → nút chuyển sang "Saved"
   - Bấm Cancel → revert về giá trị trước đó

4. **About screen:**
   - Vào About từ Home
   - Kiểm tra firmware version hiện "v1.0.0"
   - Kiểm tra disclaimer hiện "Not a medical device." / "For educational use only."

5. **Boot screen:**
   - Reset board
   - Kiểm tra version trên splash screen hiện "Firmware v1.0.0"

6. **USART telemetry:**
   - Kết nối USART1 (921600 baud) với PC
   - Mở terminal (PuTTY/Screen/Minicom)
   - Kiểm tra CSV data vẫn truyền正常

---

## Lưu ý

- **Settings chưa điều khiển phần cứng thật:** `buzzerEnabled`, `adaptiveLedEnabled`, `brightnessPercent` hiện chỉ lưu giá trị trong bridge, chưa gửi xuống driver. Cần bổ sung sau nếu muốn settings điều khiển thật sự.
- **History vẫn RAM-only:** Mất khi reset board. Cần SD/Flash backend để lưu vĩnh viễn.
- **SpO2 calibration curve** dùng giá trị empirical cho MAX30102. Nếu dùng cảm biến khác cần hiệu chuẩn lại.
