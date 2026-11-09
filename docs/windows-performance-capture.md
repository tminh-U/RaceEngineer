# Đo AC và ACC trên Windows 8 GB

Bộ đo gồm [collector PowerShell](../tools/measure-race-performance.ps1) và
[analyzer Python](../tools/analyze_race_performance.py). Collector chạy trên máy
Windows đang chơi game; không tự mở game, thay cấu hình hoặc bật chế độ nhẹ.
Cloud Linux hiện tại chưa chạy được phép đo này.

## Chuẩn bị

1. Dùng EXE Windows **v1.0.7 trở lên** chứa các tối ưu này, hoặc build từ mã nguồn
   tương ứng theo [build guide](build.md). Bản v1.0.6 chưa chứa các thay đổi này.
2. Tải **PresentMon 2.x Console Application** từ
   [GameTechDev/PresentMon](https://github.com/GameTechDev/PresentMon/releases).
   Dùng executable console hỗ trợ `--v1_metrics`, `--qpc_time`, `--session_name`;
   giao diện Intel PresentMon không thay thế executable này.
3. Mở PowerShell **64 bit** trên máy 8 GB. Collector dùng thành phần sẵn có của
   Windows PowerShell 5.1/PowerShell 7. Python 3.10+ chỉ cần cho bước phân tích.
   Nếu PresentMon báo thiếu quyền ETW, chạy lại terminal bằng quyền Administrator
   và dùng thư mục kết quả mới.
4. Chọn cùng xe, track, thời tiết, số đối thủ, camera, cấu hình đồ họa và giới hạn
   FPS. Dùng replay có telemetry hoạt động hoặc cùng kịch bản đua offline lặp lại.
   Kiểm tra app thực sự nhận telemetry AC/ACC khi đo cùng app. Giữ nguyên driver,
   chế độ nguồn, ứng dụng nền và vị trí bắt đầu giữa các lượt.

CPU/GPU/driver, RAM lắp đặt và RAM Windows sử dụng được được ghi tự động. Không
cần nhập cấu hình máy. Collector nhận AC qua `acs.exe`, ACC qua
`AC2-Win64-Shipping.exe`, app qua `RaceEngineer.exe`. Nếu có nhiều instance,
chỉ định `-GameProcessId`/`-AppProcessId`.

## Các lượt đo cho từng game

Mỗi lượt mặc định 120 giây. Sau khi tải track/model xong, đợi trạng thái ổn định
rồi bắt đầu cùng điểm trong kịch bản. Trong PowerShell tại thư mục repo/kit:

```powershell
$pm = 'C:\Tools\PresentMon\PresentMon.exe' # thay bằng executable console đã tải
```

Đóng RaceEngineer hoàn toàn, giữ game đang chạy, đo đối chứng:

```powershell
.\tools\measure-race-performance.ps1 -Game AC -Mode GameOnly -Scenario 'monza-48cars-idle' -PresentMonPath $pm -OutputDirectory .\captures\ac-gameonly-1
```

Mở app, tắt **Hệ thống & thiết bị → Chế độ nhẹ**, khởi động lại app rồi đo:

```powershell
.\tools\measure-race-performance.ps1 -Game AC -Mode Normal -Scenario 'monza-48cars-idle' -PresentMonPath $pm -OutputDirectory .\captures\ac-normal-1
```

Bật **Chế độ nhẹ**, khởi động lại app rồi đo:

```powershell
.\tools\measure-race-performance.ps1 -Game AC -Mode LowResource -Scenario 'monza-48cars-idle' -PresentMonPath $pm -OutputDirectory .\captures\ac-low-1
```

Lặp lại **cả ba bước** cho ACC, mỗi bước đổi cấu hình/khởi động lại như trên:

```powershell
.\tools\measure-race-performance.ps1 -Game ACC -Mode GameOnly -Scenario 'monza-48cars-idle' -PresentMonPath $pm -OutputDirectory .\captures\acc-gameonly-1
.\tools\measure-race-performance.ps1 -Game ACC -Mode Normal -Scenario 'monza-48cars-idle' -PresentMonPath $pm -OutputDirectory .\captures\acc-normal-1
.\tools\measure-race-performance.ps1 -Game ACC -Mode LowResource -Scenario 'monza-48cars-idle' -PresentMonPath $pm -OutputDirectory .\captures\acc-low-1
```

Các lệnh ACC là ba lượt độc lập, không chạy liên tiếp khi chưa thay trạng thái
app. Thay tên scenario bằng điều kiện thật; tên giống nhau chỉ hợp lệ khi điều
kiện đo cũng giống nhau. `-Mode` là nhãn do người đo khai báo, không tự chứng minh
setting đang có hiệu lực. `-BuildLabel` có thể ghi revision đang thử; collector
cũng ghi SHA-256 của game/app/PresentMon.

Thử tiếp các scenario riêng: Cockpit mở; thu nhỏ app; PTT/STT; phát hội thoại TTS.
Với voice, dùng cùng câu, model, provider và thời điểm kích hoạt. Đo riêng câu đầu
sau hơn 60 giây rảnh vì chế độ nhẹ sẽ phải nạp lại model; không trộn lượt lạnh và
lượt đã nạp. Ghi các yếu tố này vào scenario/ghi chú. Giữ Spotter và các tính năng
core như cấu hình thực tế, không tắt để làm đẹp số liệu.

Cho mỗi game/scenario, lặp **Normal → LowResource → LowResource → Normal**
(ABBA), dùng thư mục mới cho mỗi lượt, và lặp lượt GameOnly. Một lần chạy đơn lẻ
không đủ tách ảnh hưởng cache, nhiệt độ và tải nền.

## Phân tích

```powershell
python tools/analyze_race_performance.py captures/ac-low-1 --baseline captures/ac-gameonly-1 --output captures/ac-low-vs-game.json
python tools/analyze_race_performance.py captures/ac-low-1 --baseline captures/ac-normal-1 --output captures/ac-low-vs-normal.json
python tools/analyze_race_performance.py captures/acc-low-1 --baseline captures/acc-gameonly-1 --output captures/acc-low-vs-game.json
python tools/analyze_race_performance.py captures/acc-low-1 --baseline captures/acc-normal-1 --output captures/acc-low-vs-normal.json
```

Analyzer từ chối lượt lỗi, thiếu mẫu, sai clock/schema, dữ liệu mô phỏng và so
sánh khác game/máy/scenario/binary game/thời lượng. Nó chọn swap chain nhiều frame
nhất trong đúng PID/cửa sổ QPC, giữ các frame chậm và không gộp nhiều swap chain.
Xem p95/p99/max frametime và số frame trên 50/100 ms, không chỉ FPS trung bình.
Đây là khoảng cách giữa các lần present, không đo độ trễ hiển thị hay âm thanh.

RAM/CPU app, game và collector được ghi riêng mỗi giây; CPU chuẩn hóa trên tổng
số logical processor. Working set có trang dùng chung nên không cộng RAM app và
game để suy ra tổng. Xem thêm RAM vật lý còn trống thấp nhất và system commit.
`private_commit` là bộ nhớ commit riêng, không phải private resident RAM.
`lifetime_working_set_peak` có thể xảy ra trước khoảng đo; các peak còn lại là mẫu
1 Hz nên có thể bỏ lỡ đỉnh rất ngắn. Chưa thu VRAM hoặc hard page faults.

Không có PresentMon có thể dùng `-ResourcesOnly`, sau đó phân tích với
`--allow-resources-only`; kết quả không có frametime và không thể kết luận game
hết giật. Collector có overhead, không được coi là hoàn toàn miễn phí.

Mỗi thư mục kết quả có `manifest.json`, `resources.csv`, `frames.csv` và log
PresentMon. Lượt thiếu/thoát game sớm được đánh dấu lỗi; không lấy làm bằng chứng
đạt yêu cầu. Capture dùng ETW session riêng và chỉ dọn session của chính nó nếu
lỗi. Công cụ không đưa ra verdict tự động “8 GB đạt/không lag”.

## Kiểm chứng công cụ

Đã chạy 15 ca analyzer bằng dữ liệu fixture và kiểm tra parser/helper PowerShell
trên Linux: định dạng số vi-VN, nhận diện tiến trình/hash, CSV/manifest, layout
C# 64 bit và chặn Linux. Chưa chạy Windows P/Invoke, ETW hay hai game thật.

```powershell
python tests/test_race_performance_capture.py
powershell -NoProfile -File tests/test_race_performance_collector.ps1
```
