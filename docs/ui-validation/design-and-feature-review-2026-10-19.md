# Review đối chiếu thiết kế và tính năng — 2026-10-19

## Vòng sửa sau audit

Theo yêu cầu “thế bạn sửa đi”, agent chính đã trực tiếp sửa production và review, một agent test giữ vai trò kiểm tra.

- Khôi phục tên đối thủ/fallback và giá trị binding trong Hệ thống; sửa ReadonlyRow width=0 ở Engineer để có thể đọc khi cuộn tới.
- Thêm `qml/TelemetryPage.qml` và resource CMake: thao tác lái trái, phiên/đối thủ phải, ledger thời gian bên dưới. Bốn bánh/nhiên liệu vẫn có phần bổ sung có thể cuộn tới.
- Dashboard chỉnh masthead/số liệu theo preview, km/h cạnh tốc độ, RPM có mốc trung gian và ga/phanh dạng phân đoạn; giữ nền đen/bốn ô/lốp/áp suất/phanh.
- Heading workspace base32/52, số nhiên liệu46/76, vẫn nhân1,15; bề mặt canvas, nút44px có padding, pit status hai hàng và lối tắt tới tab dữ liệu hiện có.
- Suite `2026-10-19T04:12:02Z` pass, 15 QML ready, 0 cảnh báo; root đã kiểm tra hash nguồn khớp. Audit sau sửa `2026-10-19T04:13:38Z` đạt19/19 kiểm tra riêng; gallery `2026-10-19T04:18:33Z` có52 ảnh Qt và4capture toàn grid tại `04:18:56Z`. Root đã review ảnh cuối 880/1280/1600, Telemetry, binding System/Engineer và phần bổ sung cuộn tới. Hash QML/CMake/run_ui khớp suite; toàn bộ hash nguồn gallery khớp hiện tại. Suite giữ hash capture_review cũ vì script chụp ảnh đổi sau suite, không sửa lại provenance.

Các phát hiện trong mục audit bên dưới là lịch sử trước vòng sửa, không mô tả trạng thái production mới. Không tuyên bố khớp từng pixel ảnh tạo sinh hoặc chứng minh tích hợp native từ facade giả.

## Kết luận audit trước khi sửa

**Trước vòng sửa: chưa đạt nghiệm thu khớp thiết kế và chưa bảo toàn đầy đủ dữ liệu trên giao diện.** Có một nhóm dữ liệu baseline đã mất khỏi UI: tên xe phía trước/phía sau. Các kiểm tra UI pass trước đó vẫn là bằng chứng cho phạm vi đã chạy, nhưng kết luận đầy đủ tính năng từ 51/51 method là quá rộng.

Lượt này là audit: không sửa production. Agent chính trực tiếp đọc baseline/code và xem ảnh; chỉ `ui_tests` kiểm tra độc lập và bổ sung kiểm tra thiếu sót.

## Nguồn đối chiếu

- Thiết kế chọn ban đầu: [R1 Cockpit](../design-previews/R1-cockpit.png) và [Cockpit monochrome](../prototypes/00-cockpit.png).
- Prototype các workspace: [chỉ mục](../prototypes/README.md).
- Baseline app: `f01acc553c0d938af74f4ea6a026dbc6571eb06c:qml/Main.qml` (1659 dòng), không dùng preview làm nguồn chức năng.
- Bộ ảnh khi audit trước sửa: [ZIP lịch sử](RaceEngineer-UI-brakes-2026-10-19.zip), gallery `2026-10-19T03:40:18Z`, báo cáo UI `03:39:08Z`. Link gallery/report bên ngoài hiện đã được cập nhật sang bản sau sửa; xem trạng thái mới ở đầu tài liệu.
- Đã xem trực tiếp Cockpit 1280×800/1600×1000; đối chiếu prototype và ảnh Qt 1600×1000 của Telemetry, Fuel/Strategy, Engineer/Spotter, AI connection, System/devices; xem thêm radio history 1280×800. Prototype 1586×992 gần cùng tỷ lệ với cửa sổ 1600×1000 nên sai lệch cấp bậc chữ nhìn thấy rõ, không chỉ do cửa sổ nhỏ.

## Các sai lệch đã xác nhận

| Mức | Phát hiện | Bằng chứng / tác động |
| --- | --- | --- |
| P2 — mất dữ liệu trên UI | Tên xe phía trước và phía sau không còn được hiển thị. | Baseline Main.qml dòng 1121–1122 dùng `backend.telemetry.opponentAhead/opponentBehind` ở subtitle hai gap row. Current `qml/DrivingData.qml` chỉ đọc `gapAheadSeconds/gapBehindSeconds`; `qml/Main.qml:389` card Phiên hiện tại chỉ có thời gian, tiến độ và latestEvent. Toàn bộ QML không có `opponentAhead/opponentBehind`. Backend còn dữ liệu nhưng người dùng không còn xem được tên đối thủ. |
| P2 — lệch thiết kế | Cockpit chưa giữ đúng tỷ lệ số liệu, chữ và khoảng trống của ảnh đã chốt. | Track/simulator cách và căn khác, vùng đầu có nhiều khoảng trống; `km/h` nằm dưới tốc độ, preview đặt bên cạnh. Số nhiên liệu/tiêu thụ/gap và headline có cấp bậc khác. Xem `qml/Cockpit.qml:77`, `qml/DrivingData.qml:17`, `qml/DrivingData.qml:95`. |
| P2 — lệch thiết kế | Ga/phanh mảnh và thấp hơn preview; RPM giản lược scale. | `Pedal` trong DrivingData dùng thanh rộng 14/22px, cao 40–112px, preview dùng thanh phân đoạn cao/rõ. RPM hiện chỉ có 0 và 9.000+, preview có các mốc trung gian. Giá trị vẫn hiện và hoạt động; đây là sai lệch trình bày. Không coi mốc RPM trong ảnh là giới hạn động cơ thật. |
| P2 — lệch thiết kế | Telemetry đang dùng lại toàn bộ Cockpit instrument thay vì bố cục Telemetry riêng trong prototype. | `qml/Main.qml:379` tạo DrivingData cao theo availableHeight bên dưới heading. Lưới bánh/nhiên liệu chiếm vùng giữa/phải; phần Phiên hiện tại và tiến độ nằm bên dưới cần cuộn. Prototype 01 có vị trí/vòng/tiến độ/đối thủ ở cột phải và thời gian ở đáy. Vị trí/vòng vẫn xem được ở Cockpit, không ghi nhận là bị xóa toàn app. |
| P2 — lệch thiết kế | Cấp bậc chữ của các trang phụ và nút hành động chưa khớp prototype. | Heading các trang thường là `pageTextSize(28)` = 32px; số nhiên liệu `pageTextSize(38)` = 44px, nhỏ rõ so với mẫu ở gần cùng kích thước. Tăng ngoài dashboard 15% đã đúng công thức, nhưng không chứng minh gần prototype. Các nút/form nhỏ, thẻ xám dày và whitespace lớn khiến trang khác ảnh. Xem `qml/EngineerSpotterPage.qml:40`, `qml/SystemDevicesPage.qml:39`, `qml/FuelStrategyPage.qml:126`, `qml/LlmConnectionForm.qml:128`. |
| P3 — thông tin bị chuyển, nhãn dễ gây hiểu nhầm | System/devices có hàng “Nút đang gán” nhưng chỉ ghi trạng thái DirectInput và nút Gán lại, không còn giá trị binding. | `qml/SystemDevicesPage.qml:362` không đọc directInputBinding. Giá trị vẫn hiện ở `qml/EngineerSpotterPage.qml:408`, nên không coi đây là tính năng bị xóa toàn app. Cần làm rõ vị trí/nhãn khi sửa. |

### Những thay đổi đã được người dùng chốt

Không ghi nhận các điểm sau là lỗi so với ảnh gốc: bỏ xanh để dùng monochrome; bỏ hình xe thay bốn ô FL/FR/RL/RR; ô trùng nền Dashboard; thêm nhiệt độ phanh; font ngoài dashboard +15%; Dashboard không cuộn. Giữ các quyết định này khi chỉnh lại tỷ lệ/layout.

Dữ liệu minh họa như Monza, model xe, vị trí, nhiên liệu hoặc trạng thái bật/tắt trong ảnh không phải default cần sao chép vào production.

## Đối chiếu phạm vi tính năng

- Sáu workspace baseline vẫn có đường truy cập qua Đua / Phân tích / Thiết lập.
- Các nhóm radio/text/PTT/history, chiến thuật, record/process/train/cancel/sharing, cảnh báo/Spotter, AI config, TTS local/Google/benchmark, setup/startup, thiết bị/DirectInput/tray vẫn có control và handler. Báo cáo runtime trước đó ghi nhận đủ 51 method baseline được QML gọi; vẫn phải hiểu đó là UI → facade giả.
- Kiểm kê trường `backend.telemetry.<field>` baseline có 16 tên trường; nguồn QML hiện tại giữ 14, thiếu `opponentAhead` và `opponentBehind`. Dữ liệu còn trong namespace backend không đảm bảo có bề mặt hiển thị.
- Kiểm tra cũ `tests/ui/parity_audit.py` chỉ lấy cấp đầu `backend.telemetry`, không đối chiếu đầy đủ field bên trong hoặc nơi thông tin xuất hiện, nên bỏ sót hai trường trên.
- Source `src/**` không đổi so với baseline. Diff CMake chỉ bổ sung font/resource/component QML, không bỏ native dependency. Đây là kiểm tra nguồn, không phải chạy chức năng native.

## Xác nhận độc lập từ test agent

Audit runtime `2026-10-19T03:51:04Z`: [baseline-surface-audit.json](baseline-surface-audit.json). Lệnh: `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software .tooling/ui/bin/python tests/ui/baseline_surface_audit.py`.

Telemetry đang active nhận hai tên đối thủ riêng qua facade giả, hiện đúng gap `1,234 s` / `2,345 s`, nhưng không render tên; nhãn “Chưa có xe trước/sau” khi thiếu đối thủ cũng không còn. Kết quả scoped baseline feature parity là **false**. Probe xác nhận vị trí/vòng vẫn ở Cockpit và binding DirectInput vẫn ở Engineer/Spotter. QML hashes khớp full suite trước, không sửa production hoặc tái tạo gallery/ZIP trong lượt audit này. README và parity matrix đã đính chính coverage.

## Các việc phát hiện lúc audit (đã xử lý trong vòng sửa phía trên)

1. Khôi phục tên đối thủ vào bề mặt Telemetry có thể đọc/cuộn tới, giữ fallback khi chưa có xe. Bổ sung kiểm tra dữ liệu có/không có và cập nhật.
2. Sửa thông tin “Nút đang gán” ở System nếu tiếp tục giữ hàng đó, hoặc chỉ đường rõ tới nơi xem binding.
3. Căn lại hierarchy/spacing/các instrument và workspace theo prototype, giữ các quyết định mới của người dùng; kiểm tra cả cửa sổ lớn và nhỏ.
4. Test phải bao gồm dữ liệu hiển thị/các trường lồng nhau và khả năng truy cập, ngoài method/property coverage.

Chưa chạy Windows native, simulator thật, âm thanh/DirectInput/nhà cung cấp thật, file picker OS hoặc packaged qrc. Không kết luận các tích hợp đó đã hoạt động từ facade giả.
