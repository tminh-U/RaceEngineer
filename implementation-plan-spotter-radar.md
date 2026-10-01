# Implementation plan: sửa Spotter theo tọa độ radar, bỏ kẹp ba

Ngày: 2026-09-29. Trạng thái: phần lõi đã build thành công; còn xác nhận hướng và overlap trên track AC thực tế.

Đối chiếu mã ngày 30/09/2026: phần truyền hình học qua mapping riêng và loại bỏ
ThreeWide đã được triển khai. Các mục hiện trạng/bước triển khai bên dưới giữ
ngữ cảnh của kế hoạch ban đầu; bước nghiệm thu trên track AC vẫn còn thiếu.
Xem [kiến trúc telemetry và Spotter](docs/architecture/telemetry-radar-spotter.md)
để biết luồng hiện tại, freshness và giới hạn riêng của AC/ACC.

## 1. Kết quả cần đạt

- Spotter chỉ nói **“Có xe bên trái.”** và **“Có xe bên phải.”** khi có xe thật sự chạy cạnh trong dữ liệu còn mới.
- Bỏ hoàn toàn cảnh báo **“Kẹp ba, giữ làn.”**. Khi có xe ở cả hai bên, chỉ dùng hai cảnh báo trái/phải riêng, kiểm tra lại tình trạng trước khi đọc từng câu.
- Công tắc Spotter trên UI và lệnh bật/tắt qua LLM tiếp tục dùng setter hiện có. Tắt phải hủy ngay cảnh báo Spotter đang đọc/chờ.
- Kiểm chứng hướng và thời điểm báo bằng dữ liệu trong AC và radar trong game; test tọa độ giả chưa đủ để xác nhận đúng thực tế.

Đợt này sửa Spotter và dữ liệu hình học cần cho nó. Không thêm radar vào dashboard, không phân tích ảnh, không gọi LLM cho phát hiện xe, không thêm model/dependency, không đổi các tính năng tổng kết vòng, chiến thuật hoặc cảnh báo khác.

## 2. Hiện trạng và lỗi đã có bằng chứng

| Thành phần | Hiện tại | Điểm cần sửa |
| --- | --- | --- |
| `SpotterEngine` | Chiếu khoảng cách X/Z bằng `RaceState.heading`; dùng vùng tâm xe cố định: ngang 1–3,8 m, trước/sau 4 m | Kiểm chứng dấu/hướng góc; xét vùng xe và hướng đối thủ; loại xe ở độ cao khác |
| Trạng thái overlap | Bật sau 180 ms, giữ bên đã rời thêm 1.500 ms | Không dùng trạng thái giữ từ quá khứ làm bằng chứng hiện tại; rút ngắn thời gian nhả |
| Kẹp ba | Ghép hai cờ `leftEngaged_`/`rightEngaged_` | Bỏ tính năng. Đã tái hiện báo kẹp ba với một đối thủ đổi từ trái sang phải, không có hai xe đồng thời |
| AC telemetry | Player từ shared memory AC; đối thủ từ companion; extension có hạn dữ liệu 2 giây | Freshness riêng cho Spotter; không lấy `capturedAt` mới của provider làm bằng chứng tọa độ companion còn mới |
| Radio | Có thể bỏ Spotter mới khi đang đọc Spotter; pending chưa được đối chiếu lại với tình trạng xe trước khi phát | Giữ cảnh báo còn đúng, hủy cảnh báo sai bên/hết overlap; không phát lại lời báo cũ |
| Tests | Các test Spotter hiện dùng `heading = 0` | Thêm hướng quay, vượt xe, đổi bên, dữ liệu stale và radio đang bận |

Chưa kết luận công thức heading hiện tại chắc chắn đảo trái/phải: phải đối chiếu với dữ liệu game ở nhiều hướng trước khi sửa dấu.

## 3. Dữ liệu và hình học

### 3.1. Dùng lại nguồn hiện có

- Giữ `TelemetryManager -> RaceState -> SpotterEngine -> Application -> MessageDispatcher -> TTS`; không tạo một hệ thống Spotter thứ hai.
- AC: companion tiếp tục cung cấp WorldPosition. Lấy thêm các `TyreContactPoint` FL/FR/RL/RR trong cùng lượt cập nhật để dựng hướng thân xe từ tâm bánh sau tới tâm bánh trước; không suy hướng thân xe chỉ từ vận tốc, vì xe có thể trượt hoặc chạy lùi.
- Đối chiếu vector này với `physics.heading` của player để chốt quy ước trái/phải. Kiểm tra bánh thiếu dữ liệu, điểm không hữu hạn, vector suy biến và giá trị không hợp lý.
- `TyreContactPoint` là nguồn đã thấy được sử dụng trong source Car Radar. Đây vẫn phải được thử trên bản AC/xe đang chạy trước khi coi là nguồn tin cậy cho app.
- Chỉ lấy hình học chi tiết của xe gần; cache thông tin tĩnh theo mẫu xe khi đã xác thực. Giữ nhịp telemetry hiện có khoảng 30 Hz, không tăng tần số polling.

### 3.2. Hình dạng xe và giới hạn phải công khai

- Ưu tiên kích thước thân xe đã xác thực nếu API/dữ liệu sẵn có cung cấp được, kết hợp hướng của từng xe để chiếu phạm vi thân xe vào hệ tọa độ player.
- Nếu chỉ có bốn điểm bánh: dùng footprint bánh cộng biên an toàn đã hiệu chỉnh. Đây là **xấp xỉ thân xe**, không được ghi là kích thước/collider thật. Kiểm chứng ít nhất một xe GT và một xe có kích thước khác.
- Không thêm công cụ giải nén/convert mesh xe hoặc bắt buộc CSP chỉ để lấy collider. CSP Radar là nguồn tham khảo và công cụ đối chiếu, không phải dependency runtime của app.
- Không tự chế hướng/kích thước từ dữ liệu thiếu. Khi thiếu hướng tin cậy hoặc snapshot quá cũ, ngừng phát cảnh báo tương ứng và hiện lý do tại dòng Spotter hiện có.

### 3.3. Truyền dữ liệu mới mà không phá companion cũ

- Giữ nguyên binary layout và mapping `race_engineer_ac_ext` v1 đang dùng cho leaderboard, gaps và dữ liệu khác.
- Hình học Spotter dùng mapping bổ sung có magic/version/sequence riêng, được đọc trong `AcExtensionClient`, với bộ đệm cố định và giới hạn số xe hiện có. Cách này tránh resize mapping v1 khi app/companion cũ vẫn đang chạy.
- Snapshot bổ sung phải có player pose, car ID, vị trí/footprint cần thiết và dấu nhận biết frame mới. Chỉ cập nhật thời điểm nhận khi sequence thay đổi; mapping tồn tại không có nghĩa publisher còn cập nhật.
- Khi đổi phiên, reconnect hoặc sequence của publisher khởi động lại, bỏ geometry và trạng thái cũ. Tạo frame mới với danh sách xe rỗng cũng phải xóa xe từng tồn tại.
- Nếu companion cũ chưa có geometry, telemetry khác vẫn chạy. Spotter báo thiếu nguồn hình học đã xác thực; không âm thầm quay về thuật toán sai rồi báo là chính xác.
- Khi phát hành, đóng gói companion mới trong `extras/AssettoCorsa/apps/python/RaceEngineer` và hướng dẫn cập nhật bản trong thư mục AC/Content Manager. Installer RaceEngineer hiện không tự cài addon vào AC; cập nhật app riêng sẽ chưa đủ cho nguồn geometry mới.
- ACC dùng provider hiện có: kiểm tra việc ánh xạ `carID`/`playerCarID` sang `carCoordinates`, quy ước heading và freshness. Chỉ báo trái/phải với dữ liệu đủ điều kiện; không dùng dữ liệu AC cho ACC. Nghiệm thu AC và ACC được ghi riêng.

## 4. Xác định trái/phải và điều kiện phát lời

1. Với snapshot mới, đổi vị trí/phạm vi mỗi đối thủ sang hệ tọa độ player: khoảng cách ngang, trước/sau và chênh cao. Dùng vector hướng đã xác thực, có test ở các góc 0°, 90°, 180°, 270° và góc bất kỳ.
2. Một xe được coi là chạy cạnh khi vùng trước/sau của nó giao vùng player và khoảng cách ngang nằm trong vùng cần chừa chỗ. Xét hướng xe đối thủ khi chiếu phạm vi; không thay bằng một ngưỡng tâm xe giống nhau cho mọi xe.
3. Loại player, xe mất kết nối, tọa độ không hợp lệ, snapshot quá cũ và xe trên tầng/cầu khác. Hiệu chỉnh điều kiện chênh cao để không loại nhầm xe chạy cạnh trên dốc/đường nghiêng.
4. Tách quan sát overlap của frame hiện tại khỏi debounce. Khởi điểm hiệu chỉnh: engage 180 ms như hiện tại; clear 250 ms thay cho 1.500 ms; dữ liệu Spotter quá 250 ms không được dùng phát lời. Những số này là giá trị khởi điểm, cần đối chiếu game, không phải bảo đảm chính xác sẵn có.
5. Trạng thái hai bên độc lập. Không có nhánh ghép thành kẹp ba, không thêm câu thay thế như “hai bên” hoặc “ba xe”. Không thêm lời báo clear vốn đã được tắt.
6. Khi đủ dữ liệu ở tốc độ thấp, vẫn xét hình học; không dùng ngưỡng 15 km/h để che lỗi hướng. Trường hợp đứng yên, trong pit và xe chạy lùi phải được kiểm tra riêng.

## 5. Radio phải báo tình trạng còn đúng

- Dùng nguồn `MessageSource::ProximitySpotter` hiện có; tối đa một cảnh báo chờ cho mỗi bên. Không lưu một danh sách lời báo theo các frame cũ.
- Trước lúc đọc, kiểm tra lại dữ liệu mới nhất và overlap của bên tương ứng. Xe rời vùng, đổi bên, mất dữ liệu, đổi phiên hoặc tắt công tắc thì hủy câu không còn đúng, kể cả câu đang đọc nếu cần.
- Nếu đang đọc bên trái và bên phải mới có xe, không bỏ vô điều kiện cảnh báo bên phải. Đợi câu hiện tại/cảnh báo ưu tiên cao kết thúc rồi đọc bên phải nếu nó vẫn đúng.
- Khi hai bên cùng có xe, phát từng câu trái/phải còn hợp lệ, ưu tiên bên mới xuất hiện; nếu xuất hiện cùng lúc thì ưu tiên bên gần hơn. Không gom thành kẹp ba.
- Giữ ưu tiên Spotter dưới Critical. Chỉ lặp theo một lần overlap mới hoặc theo chính sách cooldown đã kiểm chứng; không coi “đã enqueue” là “người lái đã nghe”.
- Tái sử dụng `cancelBySource` và cơ chế pending hiện có; bổ sung thông tin bên/freshness tối thiểu cần cho kiểm tra trước phát. Các thay đổi chỉ áp dụng ProximitySpotter, giữ nguyên hành vi tổng kết và các nguồn radio khác.

## 6. Các bước triển khai

| Bước | Công việc và file chính | Điều kiện hoàn thành |
| --- | --- | --- |
| P0 | Bỏ `ThreeWide` trong `SpotterEngine`, `EventEngine.h`, mapping nguồn ở `Application`; sửa tests, `scripts/spotter_phrases.json` và cache `three_wide` | Không còn đường phát/generate câu kẹp ba; trái/phải và công tắc vẫn hoạt động |
| P1 | Kiểm chứng API bánh/hướng/footprint trong AC; cập nhật companion, `AcExtensionClient`, `RaceState`, AC provider; kiểm tra ACC provider | Snapshot đúng car ID, hướng và freshness; mapping v1 không bị thay layout; nêu rõ giới hạn footprint |
| P2 | Sửa phép chiếu, vùng overlap và debounce trong `SpotterEngine` | Test góc quay, xe khác kích thước, đổi bên, stale và độ cao pass; không tạo kẹp ba |
| P3 | Nối hủy/cập nhật cảnh báo theo bên trong `Application`/`MessageDispatcher`; thêm trạng thái dữ liệu vào dòng Spotter đang có | Câu hết hiệu lực không phát; radio bận không làm mất câu còn đúng; tắt hủy đúng nguồn |
| P4 | Test tự động, replay snapshot và đối chiếu radar trong game; cập nhật `REQUIREMENTS.md` theo kết quả thật | Hoàn thành các tiêu chí nghiệm thu bên dưới, phân biệt rõ simulator nào đã thử thực tế |

## 7. Kiểm tra và nghiệm thu

Tái sử dụng `tests/RaceStateTests.cpp` và backend radio giả hiện có; bổ sung kiểm tra publisher/parser bằng dữ liệu giả khi cần. Không gọi API LLM hoặc tải model để test Spotter.

| Tình huống | Kết quả bắt buộc |
| --- | --- |
| Chỉ có xe trái / chỉ có xe phải ở mọi hướng quay | Đúng bên; xoay cả player và đối thủ cùng một góc không đổi kết quả |
| Xe chỉ ở trước/sau, chưa chạy cạnh | Không báo trái/phải |
| Một xe đổi trái sang phải; hoặc xe trái đã rời trước khi xe phải đến | Không kẹp ba; không đọc câu trái đã cũ |
| Hai xe đồng thời hai bên | Chỉ các câu trái/phải riêng còn đúng; không câu ghép |
| Vượt và bị vượt trên đường thẳng/cua; xe xoay/trượt/chạy lùi | Hướng theo thân xe đã xác thực, không theo hướng vận tốc suy đoán |
| Xe trên cầu, dốc, pit hoặc mất kết nối | Không báo từ tầng khác/dữ liệu cũ; không bỏ xe chạy cạnh chỉ vì đường nghiêng |
| Companion dừng cập nhật nhưng mapping còn tồn tại; frame rỗng; sequence restart | Hủy cảnh báo cũ; không coi snapshot cũ là mới |
| Radio đang đọc Spotter hoặc Critical | Sau đó chỉ đọc bên vẫn còn overlap; không phát lại một chuỗi cảnh báo cũ |
| UI/LLM tắt Spotter; đổi simulator/track/car/session; disconnect | Reset trạng thái và hủy đúng audio; giữ nguyên công tắc và cảnh báo khác |
| Companion v1 cũ | Telemetry khác vẫn hoạt động; Spotter thông báo nguồn hình học chưa đủ |

Chạy build và `ctest --test-dir build-production --output-on-failure`. Dùng một bản ghi ngắn cho từng tình huống trong AC, lưu vị trí/footprint, tuổi frame, overlap và thời điểm enqueue/start/stop để đối chiếu với radar/video. Nếu AC/ACC chưa thể chạy trong môi trường kiểm tra, ghi rõ bước live còn thiếu; không ghi “đã đúng thực tế” chỉ từ unit test.

Điểm dừng: chỉ báo trái/phải đúng theo các tình huống đã nghiệm thu, không phát kẹp ba, không đọc cảnh báo mất hiệu lực, không phá mapping/công tắc/radio khác. Không tự mở rộng thành dashboard radar hoặc phân tích kỹ thuật lái.

## 8. Nguồn tham khảo

- [Car Radar - code lấy WorldPosition và TyreContactPoint để xác định hướng xe](https://github.com/itsjustdel/Car-Radar/blob/master/assettocorsa/apps/python/carRadar/carRadar.py).
- [CSP Radar - code dùng vị trí, hướng và kích thước/shape của xe để vẽ radar](https://github.com/ac-custom-shaders-patch/app-csp-defaults/blob/main/Radar/Radar.lua).

Các source trên là tham khảo cách lấy/biểu diễn dữ liệu, không chứng minh thuật toán mới của RaceEngineer đã đúng. Kết quả phải được kiểm chứng bằng implementation và dữ liệu game của app.
