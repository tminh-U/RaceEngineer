# Kết quả tối ưu sao chép và cấp phát telemetry

Ngày 2026-11-09. Đã đo native C++/Qt trên Linux với dữ liệu mẫu hình dạng AC/ACC;
**chưa đo hai game thật trên Windows 8 GB**. Dữ liệu máy đọc được, hash nguồn và
40 lượt timing nằm trong [báo cáo JSON](performance-profile-results.json).

## Thay đổi

Heaptrack chỉ ra sao chép chuỗi/vector đối thủ ở Qt queued signal và facade là
nguồn cấp phát lớn. `TelemetryManager` hiện tạo một `shared_ptr<const RaceState>`
độc lập với state mutable của provider. Queue và `Application` cùng giữ mẫu
immutable đó; không sao chép lại toàn bộ cây đối thủ ở hai bước này. Mỗi mẫu vẫn
được xử lý bởi race/event/Spotter, không bỏ mẫu để giảm tải.

Facade so sánh trực tiếp metadata để nhận biết đổi session; chỉ dựng khóa QString
khi session thực sự đổi. Các khóa QVariantMap cố định dùng QStringLiteral.
Giữ nguyên UI, định dạng dữ liệu, lịch sử, tính chiến thuật và ngữ cảnh LLM.

Đã rà bộ đệm PCM: Qt 6 có thể tiến con trỏ khi xóa đầu QByteArray, nên không thêm
ring buffer chỉ để thay thao tác vốn không sao chép toàn bộ payload này.
RaceHistory có giới hạn số mẫu/lap; không đổi giới hạn hoặc tính năng ở lượt này.

## Phương pháp

[RuntimeProfile](../tools/profile_runtime.cpp) chạy `Application` thật, signal
telemetry thật qua worker/GUI queue; I/O simulator, DirectInput và AI runtime
dùng stub. Mỗi lượt có 6.000 mẫu, 48 đối thủ với chuỗi dài/lịch sử vòng. AC có
thêm tập hình học Spotter; ACC không có tập đó. Đây là replay **tăng tốc**, không
phải chạy 30 Hz trong game hay benchmark FPS.

Baseline là mã tại đầu lượt tối ưu allocation: đã có chế độ nhẹ và Speech API
từ lượt trước, không phải main nguyên gốc. Cả hai variant bật chế độ nhẹ, dùng
cùng harness revision 2 và QML hiện tại. GCC 14, `-O2 -g -DNDEBUG -fPIC`, header Qt
6.8.2 và thư viện Qt 6.8.3; host cloud AMD EPYC 9V74 dùng chung.

Chế độ core ẩn UI. Chế độ QML tải Cockpit 1280×800 thật bằng offscreen/software,
hoàn tất setup và đóng thông báo thiếu model; ép publish một lần trên ba mẫu để
hai variant có cùng 2.001 lần publish. Không có model inference, thiết bị audio
thật, GPU rendering hay tranh tài nguyên với game.

## Số lần cấp phát

Một lượt Heaptrack riêng cho mỗi ô; tính toàn bộ vòng đời tiến trình, gồm startup.
Không dùng thời gian của tiến trình đang gắn profiler để so CPU.

| Kịch bản mẫu | Trước | Sau | Giảm số lần cấp phát |
|---|---:|---:|---:|
| AC, core | 3.650.439 | 1.832.141 | 49,81% |
| ACC, Cockpit QML | 2.773.540 | 1.744.133 | 37,12% |

Peak heap do Heaptrack báo AC 27,85 → 28,43 MB; ACC QML 48,80 → 48,80 MB.
**Giảm allocation churn không đồng nghĩa giảm RAM đỉnh.** Peak RSS không gắn
profiler trong bảng sau cũng gần như giữ nguyên. Không suy ra mức RAM tiết kiệm
của model từ những con số này. Các mục “memory leaked” của Heaptrack bao gồm
allocation còn sống lúc thoát Qt; lượt này không dùng chúng làm kết luận leak.

## CPU và thời gian xử lý

Mỗi variant warm-up một lượt, sau đó năm lượt không gắn profiler với thứ tự
trước/sau đổi qua lại. Bảng là trung vị của năm lượt. CPU là tổng CPU process
trong pha replay, không phải % CPU thực tế khi chơi game. Wall time có thể thấp
hơn CPU vì worker/GUI chạy đồng thời.

| Kịch bản mẫu | CPU trước → sau (ms) | Thay đổi CPU | Wall trước → sau (ms) | Peak RSS trước → sau (MiB) |
|---|---:|---:|---:|---:|
| AC, core | 407,18 → 190,73 | −53,16% | 273,84 → 118,12 | 57,35 → 57,56 |
| AC, QML | 642,65 → 458,40 | −28,67% | 472,55 → 384,96 | 90,84 → 91,61 |
| ACC, core | 228,77 → 114,73 | −49,85% | 156,33 → 73,56 | 57,53 → 56,79 |
| ACC, QML | 473,33 → 382,32 | −19,23% | 389,10 → 345,23 | 89,88 → 89,86 |

Peak RSS gồm startup/audio cache. Timing trên host chung và pha replay ngắn có
dao động; JSON giữ từng lượt và min/max. Số trên mô tả đúng fixture này, không
phải phần trăm giảm CPU toàn app trên máy người dùng.

P95 callback GUI tăng: AC core 8,05 → 35,31 µs, ACC core 2,98 → 17,81 µs;
AC QML 162,97 → 170,77 µs, ACC QML 156,94 → 160,27 µs. Snapshot cũ có thể được
hủy khi facade thay con trỏ cuối cùng, chuyển công việc destructor vào callback
đang tính giờ. Tổng CPU replay giảm nhưng không tuyên bố mọi callback nhanh hơn.

## Hồi quy và bước đo game

- Native `ResourceUsageTests` đạt: queued snapshot cùng identity, mẫu provider
  không bị sửa, snapshot cũ được nhả; giữ session với metadata rỗng tương đương,
  reset khi đổi track/lap rollback, bỏ qua mẫu null. Các ca lifecycle/PTT cũ đạt.
- Nhịp UI với 31 mẫu khoảng 35 ms: nhẹ 10 thông báo, thường 30; 100 mẫu khi ẩn
  không phát UI, mở lại nhận đúng mẫu mới nhất. Timer scheduling có thể lệch một
  thông báo ở biên so với lượt kiểm chứng trước.
- Full UI đạt 16 component, 51 method baseline, 6 route; metadata đạt 12 trạng thái
  ở hai kích thước, không QML warning/log. 28 hash nguồn hai report khớp mã hiện tại.
- Analyzer đạt 15 ca; parser/helper collector PowerShell đạt trên Linux. Không
  biến fixture hoặc kết quả replay thành capture Windows.

Artifact cục bộ: `.tooling/profiling/measured/{before,after}/` chứa trace Heaptrack,
text stack, raw JSON và stderr; `source-sha256.json` chứa 99 hash nguồn
mỗi variant. `.tooling/profiling/native-tests.log` và `ui-after/` chứa hồi quy.
Trace/lượt thăm dò cũ ngoài `measured/` không dùng cho báo cáo này.

Để đo cả **AC và ACC** trên máy người dùng, dùng
[quy trình Windows](windows-performance-capture.md): GameOnly, Normal và
LowResource, cùng kịch bản, lặp ABBA, thu CPU/RAM/frametime. Chưa có máy Windows
hoặc hai game trong môi trường hiện tại nên phần nghiệm thu 8 GB còn mở. Chưa
build, commit, push hoặc phát hành bản Windows mới tại thời điểm lập báo cáo
allocation này.
