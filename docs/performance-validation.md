# Kiểm chứng tối ưu tài nguyên và Speech API

Ngày: 2026-11-09. Báo cáo ghi nhận lượt kiểm chứng Linux trước khi chuẩn bị release
v1.0.7; tại thời điểm đo này chưa tạo/phát hành EXE Windows mới.

Lượt tiếp theo đã tối ưu shared snapshot/metadata/key và chạy profiling có
baseline riêng. Xem [kết quả allocation](performance-allocation-profile.md) cùng
[quy trình đo AC/ACC Windows](windows-performance-capture.md). Các số nhịp UI bên
dưới là lượt kiểm chứng đầu, không phải phép đo game thật.

## Kết quả đã thực thi

| Kiểm tra | Kết quả |
|---|---|
| Compile C++ bằng GCC 14, Qt 6.8 | Các nguồn C++ thay đổi và hai test executable compile/link thành công |
| `ResourceUsageTests` | PASS: nạp STT/TTS khi dùng, nhả context khi rảnh, giới hạn 2 luồng, nạp lại, lỗi khởi tạo TTS không treo radio, PTT giới hạn/không gửi hai lần, lưu cấu hình và facade thật |
| `GoogleTtsBackendTests` | PASS: Google và Speech API qua HTTP mock; request, Bearer riêng, TLS/URL, lỗi JSON/auth, retry/cancel, stream PCM thiếu mẫu |
| `GoogleTtsBackendTests --playback-device RaceEngineerTest` | PASS: PCM 24 kHz qua QAudioSink, phát trước EOF, chunk có kích thước lẻ, pause/resume giữ vị trí và không tổng hợp lại; cả Google và Speech API |
| Full QML suite | PASS: 16/16 component, 51/51 method facade baseline, 6 route, 20 render, parity đạt; 0 QML warning/Qt log ở UI hiện tại |
| Metadata regression | PASS: 12 tình huống ở 880×576 và 1280×800; 0 warning/log |
| Focused UI regressions | PASS: 3/3 nhóm; shortcut giọng Google/local/Speech API, giữ draft, startup notice và thông báo lỗi API |
| Gallery | 58 screenshot + 3 ảnh cận ô lốp; Cockpit, TTS khác và chế độ nhẹ ở 880×576, 1280×800, 1600×1000 |

Facade C++ nhận 31 mẫu cách nhau khoảng 35 ms trong mỗi chế độ:

| Chế độ | Mẫu đầu vào | Thông báo telemetry cho UI | 100 mẫu khi ẩn |
|---|---:|---:|---:|
| Nhẹ | 31 | 11 | 0 thông báo UI |
| Thường | 31 | 31 | 0 thông báo UI |

Mở lại cửa sổ phát ngay giá trị mới nhất (299 km/h trong fixture), không phát
snapshot cũ; disconnect bỏ qua giới hạn nhịp UI. Tính toán race/event vẫn nằm trước
nhánh giới hạn presentation trong `Application::onStateUpdated`.

Kiểm thử lifecycle dùng stub thay AI runtime, rút timer rảnh xuống 10 ms để kiểm
tra chuyển trạng thái/free/reload; cấu hình production là 60 giây. Đây không phải
phép đo RAM hay thời gian inference của model thật. Kiểm thử TTS dùng audio im
lặng và PulseAudio null sink, không phải loa/microphone vật lý. First-audio của
Speech API trong lượt HTTP mock cuối là 8 ms, trước khi server gửi nửa cuối sau
500 ms; **không phải** độ trễ OpenAI ngoài Internet hay âm thanh nghe được.

## Môi trường và cách chạy

Cloud Linux dùng Python 3.12.14, PySide/Qt 6.8.3; C++ dùng header/tool Qt 6.8.2 từ
gói Debian đã xác minh qua APT và shared library Qt 6.8.3 của PySide. Harness CMake
cục bộ bật PIC theo yêu cầu của Qt, chạy các file production trực tiếp, không sửa
third_party. Resource test dùng stub cho DirectInput và AC/ACC provider để không
đọc game/thiết bị đang chạy; không suy ra khả năng chạy app native Windows từ đó.

Các lệnh đã chạy trong `/workspace/RaceEngineer`:

```bash
.tooling/ui/bin/cmake --build .tooling/native/build --parallel 4
QT_QPA_PLATFORM=offscreen XDG_CACHE_HOME=/workspace/RaceEngineer/.cache \
  .tooling/native/build/ResourceUsageTests
.tooling/native/build/GoogleTtsBackendTests
bash .tooling/native/check-playback.sh
.tooling/ui/bin/python .tooling/validate_ui.py full
```

UI tester còn chạy metadata, focused regression và capture gallery với output
chuyển vào `.tooling/performance-validation/`. 21/21 SHA-256 của nguồn được full
UI report ghi nhận khớp với file hiện tại. Root đã xem Cockpit cả ba kích thước,
tab TTS mới và control chế độ nhẹ. Các ảnh/report lịch sử được giữ nguyên.

Artifact cục bộ:

- `.tooling/native/{resource-tests,google-tests,playback-tests}.log`.
- `.tooling/performance-validation/ui-final-output.json`.
- `.tooling/performance-validation/metadata-final-summary.json`.
- `.tooling/performance-validation/focused-final-online-shortcut-summary.json`.
- `.tooling/performance-validation/ui-final-gallery/`.

Trong full suite, PySide đôi lúc vô hiệu hóa wrapper Python của Main trong khi
engine vẫn giữ đúng cửa sổ C++: cùng pointer, marker, route, visibility/geometry;
không có `destroyed`/`closing`. Test dùng cơ chế reacquire sẵn có, vẫn giữ kiểm tra
identity/route/geometry. Đây là giới hạn của harness; không có workaround ownership
hay thay vòng đời cửa sổ trong production.

## Phần chưa kiểm chứng

- MSVC/Windows build, Windows Credential Manager và package/installer của thay đổi này.
- AC/ACC, tay lái, microphone/loa vật lý, model STT/VieNeu thật và GPU Vulkan.
- Độ trễ, chất lượng giọng, quota và chi phí với key OpenAI/Google thật.
- Tổng working set/VRAM, frametime hoặc mức dùng CPU khi Windows + game cùng chạy
  trên máy 8 GB. Không cam kết mức giảm phần trăm hoặc không còn giật lag.

Trên Windows có đủ SDK, các target được đăng ký để chạy lại qua CTest:

```powershell
cmake --build build --target ResourceUsageTests GoogleTtsBackendTests --parallel
ctest --test-dir build --output-on-failure -R "ResourceUsageTests|GoogleTtsBackendTests"
```

Các lệnh Windows trên là hướng dẫn tiếp tục kiểm chứng, chưa chạy trong cloud
Linux này. Khi đo máy 8 GB, so sánh cùng game/session/cấu hình, tách idle,
PTT/STT, hội thoại TTS, chuyển tab và thu nhỏ UI; ghi RAM/VRAM và frametime thay
vì chỉ FPS trung bình. Xem [hướng dẫn sử dụng và đánh đổi](performance.md).
