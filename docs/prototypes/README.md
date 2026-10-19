# RaceEngineer — bộ prototype Cockpit monochrome

Tham chiếu thiết kế cho mô hình triển khai. Các ảnh cùng một hướng **01 — Cockpit**, monochrome đen–xám–trắng; không phải các phương án để chọn lại. Đây là ảnh tạo sinh, **không phải screenshot app đã được triển khai hoặc bằng chứng kiểm thử**.

Nguồn chức năng: qml/Main.qml tại baseline Git **f01acc553c0d938af74f4ea6a026dbc6571eb06c**, facade Application và các tài liệu kiến trúc. Mọi số liệu, tên giọng, thiết bị và trạng thái trong ảnh là minh họa; production phải lấy giá trị/default/quyền khả dụng từ backend hiện có.

**Xem ảnh:** mở [gallery đầy đủ](index.html), bấm vào từng ảnh để xem kích thước gốc. Gallery chỉ là trang tài liệu tĩnh; không thuộc UI app.

**Cập nhật yêu cầu ngày 2026-10-19:** vùng hình xe trong các prototype đã được người dùng đổi thành bốn ô FL/FR/RL/RR, nền trùng màu Dashboard, mỗi ô có nhiệt độ lốp, áp suất và nhiệt độ phanh (yêu cầu bổ sung mới nhất). Không triển khai lại hình xe theo ảnh cũ; theo AGENTS.md và plan hiện tại.

## Chỉ mục 18 màn hình

| ID | Prototype | Vai trò |
| --- | --- | --- |
| 00 | [Cockpit monochrome](00-cockpit.png) | Mẫu chung cho layout đã chọn, typography, chrome và màu. |
| 01 | [Telemetry](01-telemetry.png) | Động lực học, ga/phanh, vòng và khoảng cách xe. |
| 02 | [Nhiên liệu & chiến thuật](02-fuel-strategy.png) | Số liệu nhiên liệu và trạng thái pit chưa khả dụng. |
| 03 | [Ghi dữ liệu, huấn luyện, chia sẻ](03-record-training-sharing.png) | Các thao tác record/process/train và consent hiện có. |
| 04 | [Kỹ sư & Spotter](04-engineer-spotter.png) | Tên tay đua, phong cách, alert, Spotter và PTT. |
| 05 | [Kết nối AI & tham số](05-ai-connection.png) | Endpoint/model/key, test/save, streaming/reasoning và tham số. |
| 06 | [Giọng cục bộ & benchmark](06-local-voice-benchmark.png) | VieNeu, voice preview, CPU threads, volume và bảng benchmark. |
| 07 | [Google TTS](07-google-tts.png) | Key/model/voice, nghe thử, thêm giọng và trạng thái. |
| 08 | [Hệ thống & thiết bị](08-system-devices.png) | Windows/tray, renderer/compute, PTT, input/output và ducking. |
| 09 | [Lịch sử radio](09-radio-history.png) | Drawer cho log hội thoại hiện có. |
| 10 | [Setup — Chào mừng](10-setup-welcome.png) | Bước 1, tên tay đua. |
| 11 | [Setup — Giọng kỹ sư](11-setup-voice.png) | Bước 2, chọn backend/voice và nghe thử. |
| 12 | [Setup — Kết nối LLM](12-setup-llm.png) | Bước 3, test/save hoặc thiết lập sau. |
| 13 | [Setup — Tối ưu máy](13-setup-optimization.png) | Bước 4, benchmark hoặc nhánh Google không cần benchmark. |
| 14 | [Khởi tạo/lỗi mô hình](14-startup-error.png) | Telemetry tiếp tục, voice chưa sẵn sàng, retry/đóng. |
| 15 | [Tạo giọng Google bằng prompt](15-google-voice-prompt.png) | Modal và preset prompt hiện có. |
| 16 | [Nhân bản giọng từ WAV](16-google-voice-wav.png) | Modal, file mẫu và file ghi âm đồng ý. |
| 17 | [Cockpit cửa sổ nhỏ](17-cockpit-compact.png) | Tham chiếu cách reflow tại viewport logic 880×576. |

## Cách dùng ảnh khi triển khai

1. Đọc AGENTS.md và plan trước. Không thêm/xóa tính năng hoặc sửa backend để khớp chi tiết trong ảnh.
2. Dùng 00 làm chuẩn chung. Các tab con chỉ tổ chức lại control có sẵn. Ảnh 01/02/03 thuộc Phân tích; 04–08 thuộc Thiết lập; 09/14/17 liên quan Cockpit.
3. Giữ ba nhóm Đua / Phân tích / Thiết lập. Các trang có thể cuộn nhưng action quan trọng và trạng thái phải dễ tìm. Layout cửa sổ nhỏ phải reflow, không thu nhỏ toàn bộ chữ.
4. Token chuẩn nằm trong plan. Ảnh tạo sinh có thể sai khác vài pixel, glyph, đơn vị hoặc text; không lấy pixel/chuỗi minh họa trong ảnh làm nguồn nghiệp vụ.
5. Dùng dữ liệu persisted/runtime thực tế, không chép các switch ON/OFF hoặc con số mẫu thành defaults. Ví dụ consent chia sẻ vẫn mặc định tắt.
6. Khi cùng control xuất hiện ở hơn một vị trí, dùng cùng property/setter và giữ đồng bộ, không tạo setting/luồng độc lập mới.
7. Các file picker WAV và menu hệ thống/tray tiếp tục dùng UI native và hành vi hiện có. Không dựng file manager hoặc menu tác vụ mới theo ảnh.

### Cây điều hướng chuẩn

```text
Đua → Cockpit → Lịch sử radio (drawer)
Phân tích
  Telemetry
  Nhiên liệu & chiến thuật
    Tổng quan
    Dữ liệu & huấn luyện
Thiết lập
  Kỹ sư & Spotter
  AI & giọng nói
    Kết nối AI
    Giọng cục bộ
    Google TTS → Thêm giọng (prompt / WAV)
  Hệ thống & thiết bị → Chạy lại thiết lập ban đầu
```

Setup vẫn là modal bốn bước; không phải nhóm navigation mới. Các khác biệt nhỏ về padding, kiểu chọn tab hoặc glyph giữa ảnh phải được thống nhất qua component/token chung lúc triển khai.

## Đối chiếu chức năng và các trạng thái phải giữ

### 00/01/17 — Cockpit và Telemetry

- Số liệu chỉ lấy từ telemetry đã công bố; sơ đồ xe là trình bày vị trí bánh, không là radar hoặc mô phỏng mới.
- Phân biệt 0/missing/disconnected. Không giả định có thời gian vòng mục tiêu, sector hoặc dữ liệu đối thủ khi provider không cung cấp.
- Tầm hoạt động nhiên liệu lấy ước tính C++ đã có, ghi rõ “ước tính”. Thang RPM là cách vẽ giá trị, không tự suy ra redline xe hoặc cảnh báo.
- Ảnh 17 mô tả bố cục nhỏ, không chứng minh app đã render ở đúng kích thước/DPI đó. Mô hình triển khai vẫn phải chạy kiểm tra Qt thực tế.

### 02/03 — Chiến thuật, record/train và chia sẻ

- Chiến thuật chưa khả dụng: pit lap là —; tự tính pit bị khóa, lời giải thích vẫn đủ tương phản. Không tạo pit recommendation mới hoặc bật giả model.
- Giữ record toggle, realistic setup confirmation, process, train pace, train lốp, cancel job, mở storage và hướng dẫn log. Process/train chỉ khi không connected và không busy theo điều kiện hiện tại; nút Dừng chỉ hiện khi busy.
- Chỉ xác nhận realistic setup khi không có Race đang hoạt động; phải xác nhận lại theo phiên. Giữ yêu cầu tối thiểu 3 phiên/60 cặp cho các model nghiên cứu như nguồn hiện có.
- Pace/lốp không tự chọn vòng pit. Không thêm báo cáo accuracy, chart training hoặc optimizer ngoài app cũ.
- Consent sharing mặc định tắt. Toggle khả dụng khi endpoint đã cấu hình hoặc sharing đang bật; không thêm form endpoint/token nếu app cũ không có.
- **Gửi lại** vẫn là chức năng cần giữ, chỉ hiện khi sharingConfigured && sharingEnabled. Ảnh 03 thể hiện sharing OFF nên không vẽ nút này; không được suy ra phải xóa chức năng.
- Giữ thông tin dữ liệu gửi, nội dung không gửi và việc dữ liệu đã gửi không tự thu hồi. Không thêm confirmation flow mới thay cho hành vi hiện tại.

### 04/08 — Kỹ sư, Spotter, PTT và thiết bị

- Tên tay đua, ba phong cách Tối giản/Tiêu chuẩn/Chi tiết; alerts nhiên liệu, lốp quá nóng, lap delta/new best, cờ, hư hại, lap summary.
- Spotter enabled khác capability: trạng thái lấy backend. Warning gap 0,5–2,5 m với bước 0,1 m, không thêm radar/map/slider độ thông minh.
- PTT bàn phím native thực tế dùng **Ctrl+Space** theo src/app/Application.cpp:eventFilter; UI cũ và ảnh ghi Space thiếu modifier. Khi code dùng nhãn Ctrl+Space, giữ nguyên handler native. DirectInput giữ binding/gán lại. Không thêm remap keyboard hoặc handler QML kích hoạt lần hai.
- Giữ volume radio và ducking −12 dB. Input refresh khi mở danh sách và output selection vẫn theo backend.
- Start with Windows, minimize-to-tray, minimize-on-close, UI GPU renderer và AI compute là các lựa chọn riêng. Tray unavailable thì khóa điều khiển liên quan; không giả định tray luôn có.
- Compute CPU/Vulkan lấy danh sách thực tế, tách khỏi GPU vẽ UI. Giữ thông báo restart và startup setup action.

### 05 — Kết nối AI

- Giữ endpoint/model/API key (password), test connection và save. Giữ điều kiện modified/requesting đang có, không chép trạng thái ảnh thành kết quả kết nối.
- Tool calling là trạng thái chỉ đọc “Luôn bật”. Streaming lưu theo form, reasoning dùng setter hiện có. Không đổi cách áp dụng setting chỉ vì sắp xếp lại UI.
- Temperature 0–1, output tokens 16–128 theo UI baseline. Context 4 lượt và timeout hiển thị readonly trong baseline; không tự thêm input.
- Trạng thái phải có: chưa cấu hình, chưa test, requesting, connected, rate limited, authentication error, server offline và provider error. Dùng icon/nhãn/viền grayscale, không chỉ đổi sắc xám.

### 06/07 — Giọng nói và benchmark

- VieNeu và Google TTS dùng cùng setting backend hiện tại. Voice options lấy từ danh sách runtime; không thêm marketplace/delete/profile manager.
- VieNeu: voice selection, nghe thử, 2/3/4 CPU threads. Thread selector giữ điều kiện disconnected, Idle, không benchmark/restoring và không external override.
- Benchmark: start/cancel, progress, timestamp nếu có, stale/unavailable reason và các cột threads/p95/CPU/RAM. Mẫu số trong ảnh không phải phép đo thực tế.
- Giữ chú thích p95 mục tiêu 1.000 ms và việc kết quả không bảo đảm hết khựng trong game. Khi cấu hình đổi, trạng thái stale và yêu cầu đo lại lấy từ backend.
- Google: model/voice/key-save, nghe thử, thêm giọng, status và first-audio latency khi đã có. Hai model trong source hiện tại là gemini-3.8-flash-lite-tts / gemini-3.8-flash-tts; không cập nhật hay mở rộng danh sách trong việc thiết kế.
- Không tự phát audio hoặc gọi API khi chỉ chuyển tab. Giữ thông báo có thể phát sinh phí.

### 09 — Lịch sử

- Chỉ role/text, plaintext; không thêm timestamp/search/export/delete khi baseline không có.
- Drawer là cách truy cập lại hội thoại vốn đã hiển thị ở dashboard. ListView ảo hóa, lazy load; không ép xuống cuối khi người dùng đang đọc phía trên.
- Gửi và PTT vẫn qua invokable cũ. PTT release/cancel/mất focus/đổi trang phải kết thúc đúng một lần.

### 10–13 — Setup bốn bước

- Giữ đúng 4 bước, Back/Continue/Finish và Bỏ qua thiết lập. Tên tay đua, voice và config lưu qua backend hiện có.
- Bước voice có nhánh Google tương ứng 07 trong panel cuộn, không thêm bước thứ năm.
- Bước LLM có test/save và Thiết lập sau; Continue ở bước này vẫn lưu form như logic hiện tại.
- Bước tối ưu: benchmark start/cancel và footer khóa theo busy state. Nếu backend Google thì hiển thị “Không cần benchmark TTS local” thay cho bảng.
- Skip/Finish vẫn gọi logic finishSetup hiện tại; không biến setup thành quy trình đăng ký hoặc xác nhận mới.

### 14 — Startup

- startupReady/progress/error từ backend. Khi voice warm-up lỗi, telemetry có thể vẫn cập nhật nhưng askText/PTT tiếp tục theo gate native.
- Giữ retryStartup và dismiss notice. Đóng thông báo không được tự bỏ gate hoặc bật text fallback.
- Trạng thái normal loading dùng cùng panel với progress; error thêm nhãn lỗi/retry. Không thêm nút download/repair model hay tiến trình từng mô hình nếu facade không cung cấp.

### 15/16 — Modal giọng Google

- Tạo bằng prompt: tên tối đa 80 ký tự, mô tả từ bonoVoiceDescription/preset hiện có, OK tạo, Cancel đóng.
- WAV: source file và consent file, định dạng/mẫu giọng hướng dẫn như baseline. Không thêm live recorder, upload zone hay checkbox consent mới.
- Câu ghi âm đồng ý phải giữ nguyên chuỗi hiện tại trong source, không sao chép câu bị tạo sinh sai từ ảnh.
- File picker tiếp tục dùng FileDialog native với bộ lọc WAV. Giữ thông báo phí và việc giọng tạo thành công sẽ lưu/chọn.

## Trạng thái bàn giao

Đã hoàn tất và rà **18 PNG**. [manifest.json](manifest.json) ghi nguồn, phiên bản ảnh, kích thước, dung lượng và SHA-256; các bản copy khớp file tạo sinh. [Gallery](index.html) hiển thị đủ 18 ảnh và liên kết tới bản gốc trong thư mục này.

Đã sửa các tab/bước wizard và action ngoài baseline do ảnh tạo sinh thêm sai. Chi tiết glyph/padding/số minh họa phải theo source và token. Bộ prototype không thay thế bảng đối chiếu chức năng baseline → UI mới, kiểm tra tương tác hoặc build Windows. Lượt tạo prototype trước đây chỉ tạo ảnh/tài liệu; bản frontend triển khai sau đó và ảnh Qt thật được bàn giao tại [ui-validation/README.md](../ui-validation/README.md).
