# Giới thiệu về Dự án điều khiển ra vào bằng RFID
## MFRC522 và giao tiếp SPI với STM32

### MFRC522 là gì?

**MFRC522** là một module đọc RFID (Radio Frequency Identification – nhận dạng bằng sóng vô tuyến) phổ biến, sử dụng chip **NXP MFRC522**. Module hoạt động ở tần số **13,56 MHz** và hỗ trợ nhiều loại thẻ/tag RFID theo tiêu chuẩn **ISO/IEC 14443A**, chẳng hạn như thẻ **MIFARE Classic**.

Module cho phép **đọc mã nhận dạng duy nhất (UID)** được lưu trong thẻ hoặc tag RFID mà không cần tiếp xúc vật lý. Việc giao tiếp được thực hiện không dây thông qua hiện tượng cảm ứng điện từ. Module thường có phạm vi phát hiện khoảng **2–5 cm**, phù hợp với các hệ thống nhận dạng ở khoảng cách ngắn và yêu cầu kiểm soát truy cập.

![RFID module RC522](MFRC522_RFID/picture/RFID)

**Nguồn: Internet**

MFRC522 có thiết kế nhỏ gọn, tiết kiệm năng lượng và hỗ trợ nhiều giao thức giao tiếp như **SPI, I²C và UART**. Trong đó, SPI thường được sử dụng nhờ tốc độ cao và cách kết nối tương đối đơn giản. Module có thể dễ dàng kết nối với các vi điều khiển như Arduino, ESP32 và STM32 để xây dựng các hệ thống bảo mật và tự động hóa sử dụng RFID.

### Tại sao sử dụng MFRC522 với vi điều khiển STM32?

Dòng vi điều khiển **STM32** được sử dụng rộng rãi trong các ứng dụng nhúng nhờ hiệu năng cao, mức tiêu thụ năng lượng thấp và khả năng cấu hình linh hoạt các thiết bị ngoại vi. Khi kết hợp với module RFID MFRC522, STM32 có thể xử lý hiệu quả việc phát hiện thẻ theo thời gian thực, trao đổi dữ liệu và xác thực quyền truy cập.

Sử dụng STM32 với MFRC522 cho phép:

- Giao tiếp thông qua **SPI tốc độ cao**, giúp đọc thẻ nhanh hơn.
- Xử lý dữ liệu RFID theo thời gian thực nhờ **nhân ARM Cortex-M**.
- Sử dụng nhiều **GPIO và các thiết bị ngoại vi** như UART hoặc LCD để hiển thị và ghi nhận dữ liệu RFID.
- Tích hợp chức năng xác thực bằng RFID vào các hệ thống **IoT và tự động hóa**.

Sự kết hợp này phù hợp để xây dựng các **hệ thống kiểm soát ra vào thông minh**, máy chấm công hoặc các mô hình thanh toán không tiếp xúc.

### Tổng quan về giao tiếp SPI trên STM32

**SPI (Serial Peripheral Interface)** là một giao thức giao tiếp nối tiếp đồng bộ, cho phép thiết bị master trao đổi dữ liệu với một hoặc nhiều thiết bị slave.

SPI sử dụng **4 tín hiệu chính**:

- **MOSI (Master Out Slave In):** Đường truyền dữ liệu từ STM32 đến MFRC522.
- **MISO (Master In Slave Out):** Đường truyền dữ liệu từ MFRC522 đến STM32.
- **SCK (Serial Clock):** Tín hiệu xung clock do STM32 tạo ra.
- **NSS (Slave Select):** Dùng để chọn/kích hoạt MFRC522 khi giao tiếp.

Trong dự án này, **STM32 đóng vai trò SPI Master**, còn **MFRC522 đóng vai trò SPI Slave**. Dữ liệu được truyền qua mỗi xung clock, giúp SPI trở thành giao thức giao tiếp nhanh và đáng tin cậy cho việc trao đổi dữ liệu với RFID.
