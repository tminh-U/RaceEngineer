# Bắt đầu với RaceEngineer

> Dùng bản tái phát hành v1.0.6 có mục **Kết nối game** và **Phiên bản & cập nhật**. Nếu bản v1.0.6 đã tải trước đây chưa có các mục này, tải lại installer từ GitHub Releases.

## 1. Cài đặt khi đã thoát game

1. Thoát AC, ACC và RaceEngineer. Tải installer từ [GitHub Releases](https://github.com/tminh-U/RaceEngineer/releases/latest).
2. Chạy `RaceEngineer-X.X.X-Setup.exe`. Installer kèm model/giọng và companion, tự tìm game trong các thư viện Steam.
3. Với AC tìm được, installer cài/cập nhật `apps/python/RaceEngineer` và bật `__FEATURES/ENABLED=1`, `RaceEngineer/ACTIVE=1` trong `Documents/Assetto Corsa/cfg/python.ini`.
4. Với ACC tìm được, installer thiết lập `broadcasting.json` trong thư mục Documents của tài khoản Windows. Giữ cổng hợp lệ và password hiện có; cấu hình mới dùng cổng 9000 cùng connection password tự tạo.
5. Mở RaceEngineer từ màn hình hoàn tất hoặc Start Menu.

Các file có nội dung thay đổi được sao lưu cạnh file gốc với hậu tố `.raceengineer-backup-…`. Những tùy chọn khác trong cấu hình game được giữ lại. Game đang chạy, lỗi quyền ghi hoặc JSON hỏng có thể khiến thiết lập chưa hoàn tất; xem bước 2 để xử lý. Installer không cài lại game.

Nếu thư mục AC cần quyền admin, Windows hiện UAC cho tiến trình chép companion. Chấp nhận để chép ba file đi kèm; bước bật Python app trong Documents vẫn chạy bằng tài khoản đang dùng app. Nếu từ chối, có thể thử lại trong app hoặc cài companion qua Content Manager.

**Yêu cầu chạy bản dựng sẵn:** Windows x64, CPU hỗ trợ AVX2, driver/runtime Vulkan hoạt động và AC hoặc ACC. Không cần Visual Studio/CMake/Qt SDK để chạy installer. Microphone dùng cho PTT; hội thoại AI qua API cần cấu hình provider và kết nối mạng. Cảnh báo local không yêu cầu LLM.

## 2. Kiểm tra kết nối game

Mở **Thiết lập → Hệ thống & thiết bị → Kết nối game · AC / ACC**, bấm **Kiểm tra lại**.

### Assetto Corsa

- Nếu chưa tìm thấy: bấm **Chọn thư mục AC**, chọn thư mục chứa `acs.exe` hoặc `acs_x86.exe`.
- Bấm **Cài / cập nhật companion** để chép đúng bản đi kèm và bật Python app. Thoát game trước khi bấm.
- Khởi động lại AC / Content Manager sau khi thiết lập. Có thể kiểm tra app trong **Content Manager → Settings → Assetto Corsa → Apps → RaceEngineer**.
- Vào một phiên chạy trên track. Cockpit phải chuyển từ **Chờ AC / ACC** sang trạng thái kết nối và có dữ liệu xe. Telemetry cơ bản dùng shared memory; companion bổ sung dữ liệu đối thủ và hình học bánh xe cho Spotter.

Trạng thái “đã cài đúng bản và bật” xác nhận file/cấu hình; chỉ khi vào track mới kiểm chứng được telemetry thực tế.

### Assetto Corsa Competizione

- Nếu chưa phát hiện ACC, cài hoặc mở game một lần rồi thoát và bấm **Kiểm tra lại**.
- App nhận diện ACC qua thư viện Steam hoặc thư mục cấu hình đã tồn tại; nhận diện này chưa xác nhận game đang cài hay đang gửi dữ liệu.
- Bấm **Thiết lập ACC UDP** khi game đã đóng, sau đó khởi động lại ACC.
- Cấu hình nằm tại `Documents/Assetto Corsa Competizione/Config/broadcasting.json`; đường dẫn theo Windows Documents, kể cả khi đã chuyển sang OneDrive/ổ khác.
- Tên khóa chính thức là **`updListenerPort`** (đúng theo cách viết của ACC). App vẫn đọc khóa cũ `udpListenerPort`; khi sửa, hai khóa được đồng bộ nếu khóa cũ đang tồn tại. Không tự đổi một cổng đang hợp lệ.
- JSON hỏng hoặc password sai kiểu dữ liệu sẽ được báo lỗi và giữ nguyên file. Sửa file rồi thử lại; các bản sao lưu có thể dùng để phục hồi cấu hình cũ.
- Vào track để kiểm tra kết nối. Shared memory cung cấp telemetry cơ bản; UDP bổ sung đối thủ/leaderboard. UDP ACC không cung cấp hình học bánh xe mà proximity Spotter hiện tại cần.

**Bản portable:** giải nén cả gói và mở `bin/RaceEngineer.exe`. Portable không chạy bước thiết lập của installer; dùng các nút ở trên. Không di chuyển riêng EXE khỏi các thư mục model, Qt runtime và `extras` đi kèm.

## 3. Thiết lập giọng nói và AI

1. Hoàn tất thiết lập ban đầu: tên người lái, giọng kỹ sư, kết nối AI và số luồng TTS. Có thể mở lại bằng **Hệ thống & thiết bị → Chạy lại thiết lập ban đầu**.
2. Trong **Hệ thống & thiết bị → Âm thanh**, chọn đúng microphone và thiết bị phát.
3. Trong **AI & giọng nói**, chọn VieNeu local hoặc Google TTS; bấm **Nghe thử** khi không kết nối game. Google TTS cần key riêng.
4. Để hỏi AI, nhập base URL tương thích OpenAI, model ID và API key ở **Kết nối AI**, lưu rồi kiểm tra kết nối. Cấu hình LLM độc lập với backend TTS.
5. Giữ **Ctrl+Space** để nói rồi thả để gửi, hoặc gán nút vô lăng bằng DirectInput. Có thể nhập câu hỏi vào radio dock.
6. Vào track, kiểm tra trạng thái **Kỹ sư & Spotter** và bật các cảnh báo cần dùng. Dữ liệu thiếu được hiển thị là `—`.

Benchmark TTS và công việc train chỉ chạy khi đã ngắt kết nối game. Đọc [hướng dẫn ghi log](logging-guide.md) trước khi thu dữ liệu Race: cần xác nhận cài đặt thực tế cho từng phiên. Chia sẻ dữ liệu mặc định tắt, chỉ khả dụng khi đã có nơi nhận và người dùng bật đồng ý. Model pace/lốp local vẫn là kết quả nghiên cứu, chưa tự bật khuyến nghị pit.

## 4. Kiểm tra và cài bản cập nhật

1. Mở **Thiết lập → Hệ thống & thiết bị → Phiên bản & cập nhật**.
2. Bấm **Kiểm tra cập nhật**. App đọc bản stable trên GitHub; với cùng số phiên bản, đối chiếu commit để phát hiện bản tái phát hành.
3. Bấm **Mở bản phát hành**, tải installer mới. Thoát game và RaceEngineer rồi chạy installer; cài đè cùng thư mục để giữ cấu hình cá nhân.
4. Mở app, kiểm tra lại AC companion / ACC UDP. Với portable, giải nén đầy đủ vào một thư mục rồi dùng các nút thiết lập game.

Kiểm tra cập nhật chỉ chạy khi bấm nút. App không tự tải hoặc chạy installer. Lỗi mạng/GitHub rate limit được hiển thị để thử lại; nếu build thiếu commit hoặc khác nhánh nguồn, app báo chưa thể đối chiếu thay vì khẳng định đã mới nhất.

## Chẩn đoán thiết lập installer

Installer gọi `RaceEngineer.exe --configure-simulators`, hoàn tất trước khi nạp các model giọng nói. Kết quả được ghi tại thư mục dữ liệu local của app, file `simulator-setup.json`; file này không chứa password ACC. Mục kiểm tra trong app luôn đọc lại trạng thái hiện tại, không dùng báo cáo cũ để kết luận đã sẵn sàng.
