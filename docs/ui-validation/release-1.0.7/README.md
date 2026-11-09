# Kiểm chứng RaceEngineer v1.0.7

Ngày 2026-11-09. Người dùng yêu cầu kiểm thử lại, push GitHub và tạo release mới.

## Kiểm chứng cục bộ đã đạt

- Compile/link lại nguồn C++ bằng GCC 14, Qt 6.8 (harness dùng stub AI, simulator và DirectInput).
- ResourceUsageTests: lifecycle model, giới hạn thread/PTT, shared snapshot, nhịp UI, khôi phục và ranh giới session.
- GoogleTtsBackendTests: Google/Speech API qua HTTP mock; PCM streaming, retry/cancel, auth/format, pause/resume. Phát lại qua PulseAudio null sink đạt; không phải loa thật hoặc Internet TTS.
- 15 ca analyzer Python và kiểm tra helper/parser PowerShell đạt trên Linux.
- Full UI: 16 component, 51 method baseline, 6 route; metadata 12 trạng thái ở hai kích thước; focused 3 nhóm đều đạt, không QML warning/Qt log.
- Root đã xem Cockpit 880×576, 1280×800, 1600×1000 và trang TTS khác. 36 hash nguồn từ ba UI report khớp; ảnh/report lịch sử giữ nguyên.

Các report, log và ảnh đi kèm nằm trong thư mục này. `linux-validation.json` ghi hash nguồn được kiểm chứng. Gallery cục bộ có 58 ảnh tại `.tooling/release-1.0.7/ui/gallery/`.

## Windows và phát hành

Build Windows MSVC/Qt, CTest, helper collector Windows, kiểm chứng native/packaged UI, manifest/version/icon và checksum sẽ được xác nhận từ workflow Windows release trước khi công khai bản phát hành.

Chưa kiểm chứng game AC/ACC thật, frametime trên máy 8 GB, Vulkan GPU vật lý, microphone/loa, giọng/API key thật hoặc UAC tương tác. [Quy trình đo Windows](../../windows-performance-capture.md) giữ riêng phần nghiệm thu này.
