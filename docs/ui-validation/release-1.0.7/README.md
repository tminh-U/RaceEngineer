# Kiểm chứng RaceEngineer v1.0.7

Đã phát hành ngày 2026-11-09 theo yêu cầu kiểm thử lại, push GitHub và tạo release mới.

- [Release v1.0.7](https://github.com/tminh-U/RaceEngineer/releases/tag/v1.0.7), được đặt là latest stable.
- Nguồn binary/tag: [95d33cce64055759ea9c3d08fcd0f880836089f0](https://github.com/tminh-U/RaceEngineer/commit/95d33cce64055759ea9c3d08fcd0f880836089f0).
- [Windows build 42620614821](https://github.com/tminh-U/RaceEngineer/actions/runs/42620614821) thành công; MSVC x64, Qt 6.8.3, AVX2 và Vulkan.

## Windows và package đã đạt

- 8/8 CTests, gồm TTS benchmark/settings migration, hai ca sở hữu thread, StrategyRecorder, RaceState, AC/ACC metadata qua shared memory tổng hợp, Google/Speech API qua HTTP mock và ResourceUsageTests.
- 15 ca analyzer Python; helper collector PowerShell kiểm tra API bộ nhớ Windows thật. Không chạy ETW hoặc game thật.
- EXE build và EXE trong package đều đạt native setup UI; cả hai thoát bình thường, exit 0. Root đã xem bảy ảnh Windows đi kèm.
- Version CLI/manifest là 1.0.7; icon của app và installer khớp ICO nguồn. Root đối chiếu đủ 1.762 file trong portable với manifest, checksum cả installer/ZIP, hash EXE và GitHub asset digests.
- Checksum tải công khai không cần xác thực khớp artifact; latest API trả v1.0.7, không draft/prerelease. Model/voice giữ từ runtime v1.0.5 đã kiểm checksum, EXE được build lại từ nguồn trên.

Bản văn bản Windows trong repo dùng LF; artifact công khai giữ nguyên byte gốc và được đối chiếu riêng.

Bằng chứng: `publish-verification.json`, `public-release.json`, `release-manifest.json`, `SHA256SUMS.txt`, `windows-validation.txt`, kết quả native/packaged và các ảnh `windows-setup-step-*.png`.

## Linux và UI đã đạt

- Compile/link lại nguồn C++ bằng GCC 14, Qt 6.8; harness dùng stub AI/simulator/DirectInput.
- TtsBenchmarkTests và hai ca lifecycle đạt; cấu hình cũ thiếu cpu_threads giữ mặc định 4, cài mới mặc định 2 và chế độ nhẹ cap runtime riêng.
- ResourceUsageTests đạt lifecycle model, thread/PTT, shared snapshot, nhịp UI/khôi phục, ranh giới session và trạng thái restart AI.
- GoogleTtsBackendTests đạt giao thức Google/Speech API, retry/cancel, auth/format; phát PCM streaming/pause/resume qua PulseAudio null sink đạt.
- Full UI: 16 component, 51 method baseline, 6 route; metadata 12 trạng thái ở hai kích thước; focused 3 nhóm đều đạt, không QML warning/Qt log. 36 hash từ ba report khớp; root đã xem Cockpit ở 880×576, 1280×800, 1600×1000 và tab TTS khác. Gallery cục bộ có 58 ảnh.

Các report/log/ảnh Linux đi kèm và `linux-validation.json` ghi phạm vi, hash nguồn tương ứng.

## Lỗi phát hiện trước khi công khai

Lượt [42616315346](https://github.com/tminh-U/RaceEngineer/actions/runs/42616315346) compile/link đạt, 7/8 CTests đạt; test migration bắt được thay đổi mặc định của cấu hình TTS cũ thiếu cpu_threads. Đã sửa production để giữ giá trị cũ, giữ nguyên assertion, rồi kiểm tra lại. Test dùng thư mục tạm trên Linux.

Lượt [42618601910](https://github.com/tminh-U/RaceEngineer/actions/runs/42618601910) được dừng trước phát hành khi root rà mã và tái hiện lỗi báo restart AI ở chế độ nhẹ + auto. App hiện so với lựa chọn cấu hình lúc mở thay vì device runtime đã áp dụng chính sách CPU. Ca mới thất bại trước sửa, đạt sau sửa; đổi thiết bị thật vẫn yêu cầu restart, đổi về lựa chọn ban đầu thì bỏ yêu cầu. Các binary của hai lượt này không được phát hành.

## Giới hạn

Chưa chạy AC/ACC thật hoặc đo frametime trên máy 8 GB; chưa xác minh Vulkan GPU vật lý, microphone/loa, chất lượng/độ trễ giọng qua API key thật hay UAC tương tác. Native setup thoát sạch ở chế độ nhẹ mặc định, không chứng minh shutdown khi model thật đang inference/warmup. [Quy trình đo Windows](../../windows-performance-capture.md) giữ riêng phần nghiệm thu game; [profiling allocation](../../performance-allocation-profile.md) là replay tổng hợp, không phải FPS game.
