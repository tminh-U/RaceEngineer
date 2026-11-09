# Kiểm chứng RaceEngineer v1.0.7

Ngày 2026-11-09. Người dùng yêu cầu kiểm thử lại, push GitHub và tạo release mới.

## Kiểm chứng cục bộ đã đạt

- Compile/link lại nguồn C++ bằng GCC 14, Qt 6.8 (harness dùng stub AI, simulator và DirectInput).
- TtsBenchmarkTests và hai ca lifecycle đạt sau khi giữ mặc định 4 luồng cho file cấu hình cũ thiếu trường cpu_threads; cài mới vẫn mặc định 2, chế độ nhẹ áp dụng cap riêng. Test settings dùng thư mục tạm trên Linux.
- ResourceUsageTests: trạng thái restart AI đúng khi chế độ nhẹ ép auto sang CPU; đổi lựa chọn yêu cầu restart, đổi về lựa chọn lúc mở app thì bỏ yêu cầu. Ca mới đã tái hiện lỗi trước sửa và đạt sau sửa.
- ResourceUsageTests: lifecycle model, giới hạn thread/PTT, shared snapshot, nhịp UI, khôi phục và ranh giới session.
- GoogleTtsBackendTests: Google/Speech API qua HTTP mock; PCM streaming, retry/cancel, auth/format, pause/resume. Phát lại qua PulseAudio null sink đạt; không phải loa thật hoặc Internet TTS.
- 15 ca analyzer Python và kiểm tra helper/parser PowerShell đạt trên Linux.
- Full UI: 16 component, 51 method baseline, 6 route; metadata 12 trạng thái ở hai kích thước; focused 3 nhóm đều đạt, không QML warning/Qt log.
- Root đã xem Cockpit 880×576, 1280×800, 1600×1000 và trang TTS khác. 36 hash nguồn từ ba UI report khớp; ảnh/report lịch sử giữ nguyên.

Các report, log và ảnh đi kèm nằm trong thư mục này. `linux-validation.json` ghi hash nguồn được kiểm chứng. Gallery cục bộ có 58 ảnh tại `.tooling/release-1.0.7/ui/gallery/`.

## Windows và phát hành

Lượt Windows đầu [42616315346](https://github.com/tminh-U/RaceEngineer/actions/runs/42616315346) compile/link đạt, 7/8 CTests đạt; ca migration TTS phát hiện thay đổi mặc định khi đọc cấu hình cũ thiếu cpu_threads. Đã sửa production để giữ giá trị cũ và chạy lại kiểm tra cục bộ; chưa phát hành binary của lượt lỗi. Build Windows và các bước package sẽ được chạy lại trước khi công khai release.

Chưa kiểm chứng game AC/ACC thật, frametime trên máy 8 GB, Vulkan GPU vật lý, microphone/loa, giọng/API key thật hoặc UAC tương tác. [Quy trình đo Windows](../../windows-performance-capture.md) giữ riêng phần nghiệm thu này.

Lượt Windows thứ hai [42618601910](https://github.com/tminh-U/RaceEngineer/actions/runs/42618601910) được dừng trước phát hành sau khi root rà mã và tái hiện lỗi báo restart AI ở chế độ nhẹ + auto. App hiện so với lựa chọn cấu hình lúc khởi động thay vì lựa chọn runtime đã áp dụng chính sách CPU. Nguồn cuối sẽ được build và kiểm chứng lại đầy đủ.
