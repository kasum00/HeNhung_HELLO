# Cảm Biến Nhịp Tim - Nguyên Lý & Xử Lý Tín Hiệu

Cảm biến nhịp tim hoạt động chủ yếu dựa trên công nghệ quang học, thu thập tín hiệu thay đổi thể tích máu và chuyển đổi thành dữ liệu số qua các thuật toán xử lý tín hiệu số (DSP).

Dưới đây là chi tiết về nguyên lý, cách lấy dữ liệu và quy trình xử lý tín hiệu từ cảm biến nhịp tim phổ biến hiện nay (như dòng cảm biến **MAX30102** hoặc **Pulse Sensor**).

---

## 1. Nguyên lý hoạt động (Photoplethysmography - PPG)

Cảm biến nhịp tim quang học hoạt động dựa trên phương pháp **quang thể tích đồ (PPG)**, đo lường sự thay đổi thể tích của cơ quan bằng cách chiếu ánh sáng vào da và đo lượng ánh sáng phản xạ hoặc xuyên thấu.

```
[ Đèn LED (Green/IR) ] ---> [ Da & Mạch Máu ] ---> [ Cảm biến quang (Photodiode) ]
                                    |
                         (Máu co bóp theo nhịp)
                                    |
                                    V
                        [Lượng ánh sáng thay đổi]
```

### Phân tích từng bước:

- **Phát sáng:** Cảm biến phát ra ánh sáng (thường là LED màu xanh lá đối với đồng hồ thông minh vì hấp thụ tốt trong máu ở nông, hoặc LED hồng ngoại IR đối với các thiết bị y tế).

- **Hấp thụ và phản xạ:**
  - Khi tim **co bóp (tâm thu)**: máu được tống đến các mao mạch ở đầu ngón tay hoặc cổ tay nhiều hơn → thể tích máu tăng lên → hấp thụ nhiều ánh sáng hơn → ánh sáng phản xạ lại **ít đi**.
  - Khi tim **giãn (tâm trương)**: thể tích máu giảm → hấp thụ ít ánh sáng hơn → ánh sáng phản xạ lại **nhiều hơn**.

- **Chuyển đổi tín hiệu:** Cảm biến quang (Photodiode) tiếp nhận lượng ánh sáng phản xạ liên tục này và chuyển thành **dòng điện biến thiên (tín hiệu Analog)**.

---

## 2. Cách lấy dữ liệu từ cảm biến (Data Acquisition)

Tùy thuộc vào loại cảm biến bạn sử dụng, cách lấy dữ liệu từ phần cứng về vi điều khiển (như Arduino, ESP32, STM32) sẽ chia làm hai dạng chính:

### Dạng 1: Tín hiệu Analog (Ví dụ: Pulse Sensor)

- **Kết nối:** Chân Signal của cảm biến nối với chân **ADC** (Analog-to-Digital Converter) của vi điều khiển.
- **Lấy dữ liệu:** Vi điều khiển thực hiện đọc liên tục giá trị điện áp (ví dụ từ `0V` đến `5V`) trả về các giá trị số từ `0` đến `1023` (đối với ADC 10-bit).
- **Tần số lấy mẫu (Sampling Rate):** Cần lấy mẫu định kỳ đều đặn, thông thường là **50 Hz đến 100 Hz** (tức là 10ms đến 20ms đọc một lần) bằng cách sử dụng bộ định thời (Timer Interrupt).

### Dạng 2: Tín hiệu Số Digital (Ví dụ: MAX30102 / MAX30105)

- **Kết nối:** Cảm biến tích hợp sẵn bộ ADC chất lượng cao bên trong và giao tiếp với vi điều khiển qua giao thức **I²C** (chân SDA và SCL).
- **Lấy dữ liệu:** Vi điều khiển chỉ cần gọi các hàm thư viện đọc thanh ghi (Registers) của cảm biến qua mạng I²C để lấy trực tiếp giá trị số (ví dụ: dữ liệu 18-bit của dải LED Đỏ và LED Hồng ngoại).

---

## 3. Cách xử lý dữ liệu (Data Processing)

Tín hiệu thô (Raw Data) nhận được từ cảm biến thường rất "nhiễu" do tay bị rung, ánh sáng môi trường lọt vào hoặc nhiễu tần số dòng điện (50 Hz / 60 Hz). Do đó, dữ liệu cần đi qua **3 bước xử lý chính**:

### Bước 1: Lọc nhiễu (Filtering)

Tín hiệu PPG thô bao gồm hai thành phần:
- **Thành phần một chiều (DC):** từ mô, xương cố định.
- **Thành phần xoay chiều (AC):** thay đổi theo nhịp tim.

- **Lọc bỏ DC:** Sử dụng bộ lọc thông cao (High-pass Filter) hoặc thuật toán trừ giá trị trung bình trượt (Moving Average) để loại bỏ phần DC ổn định, chỉ giữ lại sóng xung của mạch máu.
- **Lọc bỏ nhiễu cao tần:** Sử dụng bộ lọc thông thấp (Low-pass Filter) để triệt tiêu các sóng nhiễu nhỏ, sắc nhọn do cử động rung lắc nhẹ gây ra, làm mịn đường cong đồ thị nhịp tim.

### Bước 2: Xác định đỉnh sóng (Peak Detection)

Để tính được nhịp tim, thuật toán phải tìm được khoảng thời gian giữa hai đỉnh sóng liên tiếp (gọi là khoảng cách **Inter-Beat Interval - IBI**, đơn vị mili-giây).

- Thuật toán thiết lập một **ngưỡng động (Dynamic Threshold)** chạy ở mức 50% biên độ của sóng.
- Khi tín hiệu vượt qua ngưỡng này và bắt đầu đi xuống, một **"đỉnh" (Peak)** được ghi nhận.

### Bước 3: Tính toán chỉ số BPM (Beats Per Minute)

Sau khi xác định được khoảng thời gian IBI giữa các nhịp tim, chỉ số nhịp tim mỗi phút (BPM) được tính toán theo công thức:

$$
\text{BPM} = \frac{60000}{\text{IBI (tính bằng ms)}}
$$

**Ví dụ:** Nếu khoảng cách giữa 2 đỉnh liên tiếp đo được là `800 ms`, thì:

> BPM = 60000 / 800 = **75 nhịp/phút**

**Làm mịn kết quả:** Để tránh chỉ số bị nhảy liên tục, thuật toán thường lấy trung bình cộng của **4 đến 8 khoảng IBI** gần nhất trước khi hiển thị ra màn hình.
