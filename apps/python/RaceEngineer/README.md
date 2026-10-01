# RaceEngineer AC Companion App

Ứng dụng Python mở rộng telemetry cho **Assetto Corsa (AC)** chạy ngầm trong game,
truyền dữ liệu đối thủ, thứ hạng, tọa độ 3D, gaps và sector splits về RaceEngineer
ở khoảng 30 Hz. Telemetry legacy dùng Windows Shared Memory
`Local\race_engineer_ac_ext` và UDP fallback `127.0.0.1:9996`.

Hình học Spotter dùng mapping riêng `Local\race_engineer_ac_spotter`, gồm vị trí
và bốn điểm tiếp xúc bánh xe của từng xe. UDP legacy không mang dữ liệu hình học
này. Companion cũ vẫn tương thích với telemetry legacy nhưng không bật được
Spotter mới; hãy cập nhật app Python trong AC khi cập nhật RaceEngineer.

RaceEngineer dùng footprint bánh xe để xác định xe bên trái/phải; chưa có radar
vẽ vị trí đối thủ trên dashboard. Hướng và thời điểm overlap còn cần kiểm chứng
trên track AC thực tế. Xem
[kiến trúc telemetry và Spotter](../../../docs/architecture/telemetry-radar-spotter.md).

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
- 🔊 **Hệ thống Spotter vị trí**: Cung cấp hình học bánh xe và tọa độ xe cho cảnh báo *"Có xe bên trái"* / *"Có xe bên phải"* của RaceEngineer.
- 🏎️ **Thông tin đối thủ & Leaderboard**: Tên các tay đua xung quanh, xe dẫn đầu, bảng xếp hạng toàn đoàn (`get_position`, `get_leaderboard`, `get_driver_pace`).
- ⏱️ **Phân tích 3 Sector**: So sánh thời gian từng sector trong vòng chạy (`get_sector_analysis`).
