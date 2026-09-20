# RaceEngineer AC Companion App

Ứng dụng Python mở rộng telemetry cho **Assetto Corsa (AC)** chạy ngầm trong game, tự động truyền dữ liệu đối thủ (tên tay đua, xe, thứ hạng, tọa độ 3D để bật tính năng **Spotter / Radar**, sector splits) về RaceEngineer qua Windows Shared Memory (`Local\race_engineer_ac_ext`) tốc độ cao và cổng UDP nội bộ `127.0.0.1:9996`.

---

## Cách cài đặt

### Cách 1: Qua Content Manager (Khuyên dùng)
1. Kéo thả trực tiếp thư mục `apps/python/RaceEngineer` vào cửa sổ **Content Manager**.
2. Hoặc copy cả thư mục `RaceEngineer` vào:
   ```text
   <Đường dẫn cài đặt Assetto Corsa>\apps\python\RaceEngineer\
   ```
3. Mở **Content Manager** -> **Settings** -> **Assetto Corsa** -> **Apps** -> tích chọn ô **RaceEngineer**.

### Cách 2: Chạy trực tiếp từ Assetto Corsa gốc
1. Copy thư mục `RaceEngineer` vào:
   ```text
   assettocorsa\apps\python\RaceEngineer\
   ```
2. Vào game, di chuột sang thanh ứng dụng bên phải màn hình và bật biểu tượng **RaceEngineer**.

---

## Dữ liệu được bổ sung cho Assetto Corsa
Khi app Python này được kích hoạt, RaceEngineer sẽ tự động mở khóa các tính năng sau trên AC:
- 🔊 **Hệ thống Spotter vị trí**: Cảnh báo *"Có xe bên trái"*, *"Có xe bên phải"*, *"Kẹp ba"*, *"Bên trái thoáng"*, *"Bên phải thoáng"*.
- 🏎️ **Thông tin đối thủ & Leaderboard**: Tên các tay đua xung quanh, xe dẫn đầu, bảng xếp hạng toàn đoàn (`get_position`, `get_leaderboard`, `get_driver_pace`).
- ⏱️ **Phân tích 3 Sector**: So sánh thời gian từng sector trong vòng chạy (`get_sector_analysis`).
