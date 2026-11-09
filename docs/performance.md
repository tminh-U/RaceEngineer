# Chạy nhẹ khi đua và lựa chọn TTS

## Chế độ nhẹ

**Hệ thống & thiết bị → Chế độ nhẹ — ưu tiên game** mặc định bật. Đổi tùy chọn
rồi khởi động lại app để áp dụng toàn bộ chính sách model và thiết bị AI.

- Telemetry vẫn đọc khoảng 30 Hz; tính lap, chiến thuật, sự kiện và Spotter vẫn xử lý
  từng mẫu. Chỉ bản dữ liệu dành cho QML được giới hạn khoảng 10 Hz. Khi cửa sổ
  bị thu nhỏ/ẩn, ngừng phát thông báo telemetry cho UI; khi mở lại, phát ngay mẫu mới nhất.
- Giữ nguyên Cockpit, bốn ô lốp, chat và các trang khác. Meter microphone cập nhật
  tối đa 10 Hz; audio, pre-roll 80 ms và PTT vẫn nhận đầy đủ chunk.
- STT mặc định 2 luồng, tối đa 2 trong chế độ nhẹ. Worker STT/TTS chạy ở mức ưu tiên
  thấp. VieNeu cũng giới hạn 2 luồng trong chế độ nhẹ, trừ `OMP_NUM_THREADS` do người
  dùng chủ động đặt. Thiết lập 3/4 luồng vẫn được lưu để dùng khi tắt chế độ nhẹ.
- Khi lựa chọn AI là **Tự động**, chế độ nhẹ dùng CPU để tránh tự chiếm GPU/VRAM
  của game. Lựa chọn Vulkan cụ thể do người dùng đặt vẫn được giữ.
- Không nạp model STT/VieNeu chỉ để khởi động UI. Nạp khi thật sự nhận dạng/phát
  câu động; giải phóng context và bộ đệm STT sau 60 giây rảnh. Audio Spotter/cảnh báo
  thu sẵn vẫn được giữ cục bộ và phát qua hàng đợi ưu tiên hiện có.
- Một lần giữ PTT tối đa 30 giây, tự gửi đoạn đã thu, không gửi lại khi thả nút.
  Đây cũng là giới hạn bộ nhớ cho trường hợp nút bị giữ ngoài ý muốn.
- Mẫu telemetry immutable được dùng chung qua worker/GUI queue và facade; tránh
  sao chép lặp lại vector đối thủ. Khóa session chỉ dựng khi metadata đổi.

**Đánh đổi:** câu đầu sau lúc khởi động hoặc sau thời gian rảnh cần nạp lại model,
nên có thể chậm hơn và phát sinh I/O. Nếu cần giọng cục bộ sẵn sàng liên tục và RAM
còn đủ, tắt chế độ nhẹ. Hai chế độ không thay đổi model, thuật toán nhận dạng hoặc
thuật toán telemetry/Spotter.

## Gợi ý cho máy 8 GB

Ưu tiên chế độ nhẹ, TTS online và LLM chạy qua API/máy khác. Khi dùng TTS online,
VieNeu không cần nạp cho hội thoại; các cảnh báo thu sẵn vẫn hoạt động không có mạng.
STT tiếng Việt vẫn chạy cục bộ khi giữ PTT. Đóng bớt trình duyệt/ứng dụng nền và đo
frametime trong chính game đang chơi. Giữ pagefile Windows do hệ thống quản lý.

App không thể bảo đảm cả Windows + AC/ACC + model khác vừa 8 GB: RAM của game,
GPU dùng RAM chung và máy chủ LLM chạy ngoài app đều tính vào tổng tài nguyên.
Giảm các ô lốp không loại bỏ được bộ nhớ của model. Chưa có phép đo RAM/frametime
với game thật trên máy 8 GB cho thay đổi này.
Đã có [kết quả profiling native](performance-allocation-profile.md) và
[bộ đo Windows cho cả AC/ACC](windows-performance-capture.md) để kiểm chứng tiếp.

## TTS online ngoài Google

Vào **AI & giọng nói → TTS khác**, chọn **OpenAI-compatible TTS**:

1. Endpoint: `https://api.openai.com/v1/audio/speech`.
2. Model: `gpt-4o-mini-tts`; voice: `coral` (hoặc voice hợp lệ khác của dịch vụ).
3. Nhập API key riêng cho TTS, bấm **Lưu cấu hình**, sau đó **Nghe thử** khi đã
   ngắt kết nối simulator. Không tự sinh audio chỉ vì mở trang/lưu cấu hình.

Key lưu trong Windows Credential Manager, không ghi vào `settings.json`. Bỏ trống
key giữ key hiện tại nếu endpoint không đổi. Đổi endpoint mà không nhập key mới sẽ
xóa key cũ; không tự gửi credential của nhà cung cấp trước sang server mới.
Nút **Xóa key** xóa riêng credential TTS này, không ảnh hưởng LLM/Google.

Máy chủ tương thích khác phải hỗ trợ `POST` Speech API với `model`, `voice`, `input`
và `response_format: "pcm"`; trả PCM mono 24 kHz, signed 16-bit little-endian, không
có header WAV. HTTPS bắt buộc cho máy chủ từ xa; HTTP chỉ được dùng cho localhost.
API dùng SSE, WebSocket, MP3/WAV riêng không tự tương thích chỉ nhờ đổi URL.

| Dịch vụ | Phù hợp | Tích hợp ở thay đổi này |
|---|---|---|
| [OpenAI Speech](https://developers.openai.com/api/docs/guides/text-to-speech) | Đa ngôn ngữ, có tiếng Việt; giọng tự nhiên, PCM streaming | Có, qua tab TTS khác |
| [ElevenLabs Flash v2.5](https://elevenlabs.io/docs/overview/models) | Ưu tiên phản hồi nhanh, nhiều lựa chọn giọng; kiểm tra ngôn ngữ/voice theo model | Gợi ý; cần adapter API riêng hoặc gateway tương thích |
| [Azure Speech Neural](https://learn.microsoft.com/en-us/azure/ai-services/speech-service/language-support) | Tiếng Việt `vi-VN-NamMinhNeural`, `vi-VN-HoaiMyNeural`; nhiều giọng quốc tế | Gợi ý; cần adapter API riêng hoặc gateway tương thích |

Giọng được tổng hợp bằng AI. Dịch vụ có thể tính phí; không dùng key LLM thay key
TTS khi hai dịch vụ khác nhau. Chất lượng tiếng Việt cần nghe thử với thuật ngữ đua xe.

## Độ trễ và kiểm chứng

Google đã dùng streaming PCM. Thay đổi này giảm bộ đệm **yêu cầu** cho QAudioSink
từ 48.000 xuống 9.600 byte (1 giây xuống 200 ms ở PCM 24 kHz mono), dùng lại bộ
phát streaming, pause/resume, giới hạn bộ nhớ và retry cho Speech API. Qt/driver
có thể điều chỉnh kích thước thực tế. Đây không phải cam kết giảm đúng 800 ms độ trễ.

Số **Audio đầu tiên** đo từ lúc app bắt đầu yêu cầu TTS đến lúc có PCM và mở bộ
phát. Nó không tính thời gian STT/LLM trước đó và không đo trực tiếp âm thanh ra loa.
Hội thoại hiện gửi TTS sau khi nhận câu trả lời hoàn chỉnh từ LLM, tránh đọc trước
nội dung tool/reasoning chưa được xử lý xong. Model LLM/độ dài câu/mạng và thời gian
sinh giọng của nhà cung cấp vẫn ảnh hưởng mạnh tới tổng độ trễ. Không công bố mức
tiết kiệm RAM, CPU hay FPS khi chưa đo với game/model thật.

`ResourceUsageTests` kiểm tra facade C++ thật, nhịp thông báo UI, khôi phục snapshot,
PTT và vòng đời model bằng stub AI, DirectInput và simulator provider; đây không
phải kiểm thử game/thiết bị thật, chất lượng giọng hay bộ nhớ inference.
`GoogleTtsBackendTests` kiểm tra giao thức Google/Speech API qua HTTP mock, không gọi
API tính phí. Xem [báo cáo kiểm chứng](performance-validation.md) cho kết quả thực thi.
