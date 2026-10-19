# RaceEngineer — plan triển khai Cockpit cho mô hình tiếp nhận

## Phát hành 1.0.6 — theo yêu cầu mới nhất

- [x] Tạo logo monochrome mới; thay master, 10 PNG/ICO, header, tài nguyên app/tray/installer và social preview; hai README dùng chung logo, sửa đường dẫn README tiếng Việt.
- [x] Thay ảnh README bằng Cockpit Qt mới với dữ liệu minh họa, giữ wordmark/font/header typography hiện có.
- [x] Tăng CMake/project và ví dụ packaging lên 1.0.6; viết changes theo cấu trúc release trước tại `docs/releases/v1.0.6.md`.
- [x] Kiểm tra frontend/branding: 15 QML, 51/51 facade methods, 12 breakpoints, 25/25 edge, 3/3 regression, 0 warning. Xem [bằng chứng](ui-validation/release-1.0.6/README.md).
- [ ] Commit/push nguồn đã kiểm tra; kiểm tra lại trên GitHub.
- [ ] Build/CTest/package/verify bản Windows mới, sau đó publish release với installer/ZIP và source commit chính xác.

Yêu cầu commit/push/release mới thay giới hạn không publish trong phần lịch sử. Backend/thuật toán/settings defaults không đổi; không phát hành binary cũ dưới tên phiên bản mới. `.agents/` và `skills-lock.json` không đưa vào commit ứng dụng.

## Audit bổ sung — đã sửa và kiểm chứng (2026-10-19T14:04:22Z)

Theo yêu cầu “Sửa đi”, đã xử lý cả 3 lỗi. Kết quả mới: **25/25 ca edge đạt**, thêm **3/3 nhóm regression** cho viewport/cuộn/thao tác startup, mapping trạng thái API và shortcut voice lần đầu/lần sau/giữ draft. Không cảnh báo QML/Qt, hash nguồn báo cáo khớp; root đã xem ảnh Qt. [Báo cáo/ảnh sau sửa](ui-validation/followup-fix/README.md). Kết quả [22/25 trước sửa](ui-validation/followup-review-2026-10-19.md) giữ làm lịch sử.

- [x] Detail API chỉ dịch chuỗi thành công chuẩn khi state Connected; lỗi unavailable/chi tiết khác giữ nguyên. Không đổi provider.
- [x] Startup notice giới hạn chiều cao theo cửa sổ, cuộn nội dung lỗi dài, giữ title/retry/close cố định. Kiểm tra đầu/cuối nội dung ở 880×576, thao tác retry/close và trạng thái lỗi ngắn/loading đều đạt.
- [x] Shortcut “Cấu hình giọng nói” mở Giọng cục bộ / Google TTS theo backend. Kiểm tra cả lần đầu lazy-load/lần sau và draft chưa lưu được giữ.

Các ca dữ liệu dài/số lớn ở sáu kích thước, binding/remap, API detail dài, bàn phím/PTT và refresh sau inactive/minimize đã đạt trong facade giả. Chưa phát hiện thêm tính năng bị xóa trong phạm vi này; chưa xác minh native Windows/simulator/audio/DirectInput/provider thật. Không dùng kết quả này để kết luận toàn app hết lỗi.

## Vòng sửa sau review người dùng

### Triển khai sửa các phát hiện audit

- [x] Khôi phục tên đối thủ và nhãn khi chưa có xe, vị trí/vòng ngay trong Telemetry; binding DirectInput ở System và sửa ReadonlyRow width=0 ở Engineer.
- [x] Tách `TelemetryPage.qml`: thao tác lái trái, phiên/đối thủ phải, thời gian phía dưới; bốn bánh/nhiên liệu bổ sung vẫn truy cập được khi cuộn.
- [x] Chỉnh tỷ lệ Cockpit, km/h cạnh tốc độ, mốc RPM và thanh pedal phân đoạn; giữ bốn ô đen có lốp/áp suất/phanh và không cuộn.
- [x] Heading ngoài dashboard base32/52 theo compact, số nhiên liệu46/76; vẫn nhân1,15. Nút44px có padding/hit target, bề mặt canvas phẳng; pit status hai hàng và shortcut tới tab dữ liệu như prototype.
- [x] Suite 2026-10-19T04:12:02Z pass15QML/51method/0warning; audit 2026-10-19T04:13:38Z đạt19/19 kiểm tra dữ liệu/binding; gallery 2026-10-19T04:18:33Z có52ảnh và4capture grid tại 2026-10-19T04:18:56Z. Root đã xem ảnh cuối; Cockpit không cuộn, Telemetry hiển thị tên/gap/ledger, phần bổ sung/binding cuộn tới được. Hash nguồn production khớp; src/** không đổi. Native Windows/simulator/thiết bị thật chưa kiểm chứng.
- Theo yêu cầu sửa audit, Dashboard được đổi base font/layout để gần preview; header không đổi, Dashboard textScale vẫn1 và các trang khác vẫn×1,15. Đây thay yêu cầu trước về giữ nguyên tuyệt đối pixel font Dashboard.


### Lịch sử audit trước sửa — các mục đã được xử lý phía trên

- Người dùng yêu cầu đối chiếu trực tiếp với preview và tìm tính năng bị mất. Đây là lượt audit, không tự sửa production.
- **Đã xác nhận mất tên xe phía trước/phía sau trên UI Telemetry**: baseline đọc `telemetry.opponentAhead/opponentBehind`, hiện không QML nào đọc hai trường. Gap giây vẫn hiện; backend không đổi. 51/51 method coverage trước đây không đủ chứng minh toàn bộ dữ liệu/tính năng được giữ.
- **Thiết kế chưa khớp prototype** ngoài những thay đổi đã được người dùng chốt: tỷ lệ chữ/số và khoảng trống Cockpit, thanh ga/phanh, layout Telemetry riêng, cấp bậc chữ/nút/thẻ trên các workspace phụ.
- System/devices không hiện giá trị nút đã gán ở hàng “Nút đang gán”; vẫn xem được binding trong Kỹ sư & Spotter. Ghi nhận là thông tin bị chuyển/nhãn gây hiểu nhầm, không kết luận xóa binding toàn app.
- Chi tiết và bằng chứng: [review trực tiếp](ui-validation/design-and-feature-review-2026-10-19.md). Kết quả pass của suite cũ chỉ áp dụng các kiểm tra thực sự có trong suite; không dùng làm kết luận toàn bộ parity hay độ khớp thiết kế.
- [x] Khôi phục hai tên đối thủ và kiểm tra render/update/fallback.
- [x] Hiển thị binding tại System/devices và sửa width=0 tại Engineer.
- [x] Căn lại hierarchy/layout đã ghi nhận, giữ monochrome/bốn ô/phanh/font +15%/no-scroll; root đã review ảnh Qt. Không khẳng định trùng từng pixel ảnh tạo sinh.


### Yêu cầu mới nhất: nền đồng màu Dashboard và nhiệt độ phanh

- [x] Đổi nền bốn ô sang `theme.canvas` (`#111111`) để trùng Dashboard.
- [x] Thêm dòng PHANH (°C) cho FL/FR/RL/RR từ facade `brakeTemperatures` sẵn có; nhãn nhiệt độ lốp đổi thành LỐP cho rõ. Giữ áp suất psi, font và dữ liệu 0/thiếu; Accessible.name chứa đủ ba giá trị. Runtime đã pass mapping và cập nhật trực tiếp trên Cockpit/Telemetry.
- [x] Suite `2026-10-19T03:39:08Z`: 51/51 method baseline qua facade giả, 14 QML ready, 0 cảnh báo QML/Qt, 12 breakpoint Cockpit. Phanh đúng bốn index, 0/thiếu/cả array vắng và cập nhật trên cùng item; Accessible.name và màu nền đúng. Font/no-scroll hồi quy đều pass. Root đã xem ảnh Qt thật Cockpit 880×576, 1280×800, 1600×1000 và toàn bộ grid Telemetry; đủ ba dòng, đơn vị, padding, không cắt/chồng. Gallery `2026-10-19T03:40:18Z` có 50 ảnh và 4 capture toàn grid; hash nguồn được kiểm tra. `src/**` không đổi; native Windows/simulator/thiết bị thật chưa kiểm chứng.
- **Phân vai mới nhất:** agent chính tự implement và review; chỉ giữ `ui_tests` Luna max để test. Mọi ghi chép về hai coder bên dưới là lịch sử đã bị thay thế. Không đổi `src/**`, không upload/publish.

### Đã hoàn tất trước đó: thay hình xe bằng bốn ô bánh

- [x] Bỏ hình xe/Canvas, thay bằng lưới 2×2: FL trái trên, FR phải trên, RL trái dưới, RR phải dưới. Mỗi ô hiển thị nhiệt độ (°C) và áp suất (psi) hiện có, đúng index 0–3; kiểm tra giá trị riêng từng index và trộn 0/thiếu trên Cockpit/Telemetry đã pass.
- [x] Reviewer đã xem ảnh Qt tại 880×576 và 1280×800: ô bằng nhau, khoảng cách/padding cân, chữ và đơn vị không chồng/cắt. Kiểm tra targeted đạt 12 breakpoint và 5 kích thước; Dashboard không cuộn, font dashboard/header không tăng, Telemetry và các mục khác giữ hệ số 1,15.
- [x] Suite `2026-10-19T03:23:27Z` pass: mapping riêng bốn index, 0/missing, ô bằng nhau/không chồng/cắt chữ, giữ font Cockpit và Telemetry +15%; 12 breakpoint, 51/51 method baseline qua QML/facade giả, 0 cảnh báo QML/Qt. Gallery `2026-10-19T03:24:47Z` có 50 ảnh Qt mới, hash nguồn khớp; reviewer đã xem ảnh ô tại 880/1280/1600.
- [x] [ZIP bốn ô bánh](ui-validation/RaceEngineer-UI-tyres-2026-10-19.zip) gồm 50 ảnh gallery và 4 ảnh cận cảnh. Telemetry có capture toàn bộ item Qt để xem đủ bốn ô ngoài viewport cuộn. Không chứa crop hình xe cũ; đã kiểm tra CRC, hash nguồn gallery khớp, `git diff --check` sạch và `src/**` không đổi. Native/thiết bị/provider thật chưa kiểm chứng.

Yêu cầu này thay phần hình xe trong prototype và các kết quả lịch sử dưới đây. Không thêm/xóa chức năng, không sửa backend. Lưới ô thay cách trình bày cùng dữ liệu bốn bánh.

### Lịch sử: căn xe và font ngoài dashboard (phần hình xe đã được thay thế)

- [x] Căn sơ đồ và các label FL/FR/RL/RR theo chính tọa độ bánh trên Canvas. Hai phía hướng vào xe, cùng khoảng cách 12px ở 1280×800; aspect 180:400, không kéo giãn x/y độc lập. Reviewer đã xem crop và ảnh toàn màn hình tại `ui-validation/core-review/`.
- [x] Đã áp dụng `pageTextSize(base)=round(base×1.15)` cho các workspace và form/setup/dialog; Telemetry dùng `textScale=1.15`. Cockpit/header giữ font hiện tại. Nhãn nhiên liệu Telemetry được wrap để không mất chữ khi font tăng; bảng đo font lấy đúng workspace đang hiển thị trong `core-review/source-and-geometry.json`.
- [x] Suite `2026-10-19T03:02:36Z` (10:02:36 giờ Việt Nam) pass: aspect/tâm bánh/gap/căn chữ được đo, font ngoài dashboard nhân 1,15 và header/Cockpit giữ nguyên ở 880×576, 1280×800, 1600×1000, 1366×728, 1280×720. Có source audit đủ mười component và kiểm tra glyph/control bounds. Full suite đạt 51/51 method baseline qua QML/facade giả, 0 cảnh báo QML/Qt; `src/**` không đổi. Bằng chứng: `ui-validation/render-report.json`, mục `scaled_page_fonts_and_vehicle_geometry`.
- [x] Gallery `2026-10-19T03:04:10Z` có 50 ảnh Qt thật, 0 cảnh báo, hash nguồn khớp. Reviewer đã xem lại Cockpit/crop xe/Telemetry/Engineer/AI/System. [ZIP ngày 2026-10-19](ui-validation/RaceEngineer-UI-2026-10-19.zip) gồm gallery, 6 crop trước/sau, trang chỉ mục và báo cáo; CRC được kiểm tra. Giải nén, mở `index.html` hoặc `vehicle-alignment-crops/index.html`. Chỉ tải xuống, không upload qua dịch vụ ngoài. Native Windows/audio/simulator/DirectInput/provider vẫn chưa kiểm chứng.

- Cockpit không cuộn: đủ telemetry, bốn lốp và radio trong viewport; điều chỉnh cửa sổ mặc định/tối thiểu phù hợp màn hình, không cắt/ẩn dữ liệu. Các form/history vẫn được cuộn khi cần.
- Font phải gần preview Cockpit gốc: headline/số liệu đậm chắc, body sans regular/medium rõ tiếng Việt. Rà font family/style/weight thực tế và rendering; không chỉ thay tên font.
- Rà đầy đủ hành vi của các control/action baseline bằng kiểm tra runtime, bao gồm setter/argument, save timing và gates. Phân biệt rõ static audit, facade giả và tích hợp native chưa kiểm chứng.
- Hai coder Luna sửa theo ownership, Luna test kiểm tra; root chỉ review. Báo cáo vòng trước bên dưới là baseline của vòng sửa này, chưa phải nghiệm thu yêu cầu mới.

### Kết quả sửa giao diện

- [x] Cửa sổ mặc định mục tiêu 1440×900, tối thiểu 880×576; giới hạn theo màn hình/vùng làm việc khả dụng. Cockpit dùng viewport tĩnh, không còn Flickable. Layout ngắn giảm masthead/dock và mật độ instrument; không bỏ dữ liệu.
- [x] Review render Qt ở 880×576, 1280×640/800 và 1366×728: đủ telemetry, bốn lốp và radio, không cắt dòng cuối. Các kích thước/breakpoint khác chờ báo cáo test cuối.
- [x] Đã xác định Qt 6.8.3 raster font variable giống nhau ở weight 400/500/600 trong mẫu kiểm tra. Chuyển sang các face tĩnh Regular/Medium/SemiBold, tên family RaceEngineer Sans; body Medium, wordmark Sans SemiBold, display condensed đậm. Nguồn/giấy phép: [README font](../assets/fonts/README.md).
- [x] Sửa DeviceRow trong SystemDevices: ComboBox từng có width=0 do dùng width binding trong ColumnLayout; nay dùng Layout.fillWidth và probe xác nhận chọn được bằng bàn phím.
- [x] Suite mở rộng `2026-10-18T13:00:54Z` (20:00:54 giờ Việt Nam) pass: 51/51 method được UI baseline tham chiếu có lời gọi động vào facade nghiêm ngặt; có kiểm tra các action, property write, tham số và gates bằng đầu vào Qt. Đây là kiểm chứng UI, không phải 51 tích hợp native đã chạy thật.
- [x] 12 trường hợp breakpoint Cockpit kiểm tra kích thước yêu cầu/thực tế và bounds số liệu/radio, gồm các chiều cao laptop. 20 ảnh route từ suite và 50 ảnh gallery được chụp lại; gallery `2026-10-18T13:01:50Z`, source hash khớp, 0 cảnh báo QML/Qt.
- [x] Font audit xác nhận family/weight thực tế: input/body RaceEngineer Sans 500, wordmark 600, numeric Mono 700, display Barlow 700; các face Sans tĩnh raster khác nhau. Reviewer đã đối chiếu Cockpit và các trang System/Engineer/AI sau sửa.

**Kết luận vòng sửa:** đã đáp ứng no-scroll Cockpit và typography, sửa lỗi thiết bị width=0, hoàn tất kiểm tra hành vi UI trong Qt/PySide6 với facade giả. `src/**` không đổi, `git diff --check` sạch. Audio/simulator/provider/DirectInput/Windows native, packaging qrc và DPI thiết bị thật vẫn chưa được kiểm chứng; xem báo cáo hiện tại để tách từng phạm vi.

**Trạng thái vòng trước: Đã hoàn thiện frontend và kiểm chứng UI bằng Qt/PySide6 + facade giả.** Phân vai hai coder/một test ở vòng trước đã được thay bằng agent chính implement/review và một test agent theo yêu cầu mới nhất. Báo cáo cuối: [ui-validation/README.md](ui-validation/README.md), [kết quả JSON](ui-validation/render-report.json), [ảnh render thật](ui-validation/index.html). Native Windows/thiết bị thật chưa kiểm chứng. Phần 2 chỉ ghi lịch sử lúc tiếp nhận.

## 1. Quyết định đã chốt

- Mục tiêu duy nhất: **01 — Cockpit**. [Chuẩn monochrome](prototypes/00-cockpit.png), [layout chọn ban đầu](design-previews/R1-cockpit.png).
- [Bộ prototype toàn ứng dụng và đối chiếu trạng thái](prototypes/README.md), [gallery 18 ảnh](prototypes/index.html): dùng để triển khai các workspace, setup/dialog và responsive. Không phải các hướng thiết kế để chọn lại hoặc bằng chứng UI đã chạy.
- **Giữ nguyên phạm vi tính năng:** không thêm tính năng mới; không xóa, tự vô hiệu hóa hoặc làm mất bất kỳ tính năng hiện có nào. Giữ đầy đủ tùy chọn, tích hợp, thao tác và hành vi. Chỉ thay layout/trình bày và tối ưu thực thi, bảo toàn chức năng.
- **Màu đã chốt bổ sung: monochrome đen–xám–trắng thuần.** Concept **01 — Cockpit** là tham chiếu layout; dùng docs/prototypes/00-cockpit.png làm chuẩn màu, thay màu xanh của ảnh gốc R1 bằng grayscale. Quyết định này ưu tiên hơn bảng màu Matrix trong skill. ID ảnh prototype 00–17 là chỉ mục màn hình, không phải số phương án thiết kế.
- Reconstruct kiến trúc màn hình, không chỉ đổi theme. Bỏ sidebar sáu mục và bố cục chat lớn/cột cài đặt cố định.
- Phần chính là tốc độ/số/RPM, bốn ô dữ liệu bánh, nhiên liệu/gap/thời gian vòng. Radio cố định ở dưới, lịch sử mở drawer.
- Matrix / Minimal / Racing, tiếng Việt dễ đọc, dữ liệu khách quan. IBM Plex Sans + IBM Plex Mono + Barlow Condensed, đóng gói offline.
- Tối ưu tải UI; giữ nguyên nghiệp vụ/backend. Người dùng muốn ít token: tái dùng khảo sát/tài nguyên, không dựng lại concept hoặc viết giải thích dài.
- Ảnh A/B/C vòng đầu bị loại. 02/03 vòng sau không phải lựa chọn triển khai.

## 2. Tiếp nhận: hiện trạng code dở

Ghi chép lịch sử lúc tiếp nhận: lượt trước bắt đầu code trước khi người dùng yêu cầu dừng; các mục “chưa” bên dưới không mô tả bản tích hợp hiện tại. Khung SessionLog đã bỏ vì không thuộc tính năng baseline; sáu route hiện tại bảo toàn các workspace cũ.

| File | Hiện trạng |
| --- | --- |
| qml/Main.qml | Đã thay shell/sidebar bằng mode navigation, thêm route Cockpit/SessionLog, đổi token/control và telemetry page. Một số form cũ còn nguyên; chưa kiểm tra QML. |
| qml/UiTheme.qml | Khung token, FontLoader và định dạng số/lap/gear. Chưa chạy. Còn bộ màu xanh cũ; phải chuyển sang monochrome khi thực thi. |
| qml/DrivingData.qml | Khung ba vùng instrument, Canvas sơ đồ xe và số liệu lốp. Chưa kiểm tra render/bounds. |
| qml/Cockpit.qml | Khung masthead, instrument, radio dock, PTT và history Drawer/Loader. Chưa kiểm tra tương tác. |
| qml/SessionLog.qml | Khung ListView event log, chưa chạy. Đây là khung mới, không mặc định thuộc phạm vi chức năng; chỉ giữ nếu là cách trình bày lại chức năng đã có trong baseline. |
| CMakeLists.txt | Đã khai báo các file QML/font mới; chưa configure/build. |
| assets/fonts/ | Ba TTF và ba OFL đã tải; đã dùng fontTools kiểm tra chuỗi tiếng Việt mẫu không thiếu glyph. |
| .gitignore | Đã bỏ dòng ignore /AGENTS.md để hướng dẫn có thể được quản lý cùng repo. |
| AGENTS.md và plan này | Tài liệu bàn giao, không phải bằng chứng code hoàn tất. |

src/** chưa bị sửa. .agents/ và skills-lock.json có sẵn từ trước, không xóa hoặc thay chúng. Các font là khoảng 0,8 MB, không cần tải lại. Công cụ dev PySide6-Essentials 6.8.3 được cài tại .tooling/ui, thư mục này đang được Git ignore.

### Kiểm tra đầu tiên, trước khi viết thêm

1. Đọc git diff cho Main.qml/CMake và các file QML mới; đối chiếu với docs/prototypes/00-cockpit.png. Có thể sửa hoặc thay phần khung không phù hợp, không mặc định giữ mọi đoạn đã viết.
2. Kiểm tra ba chỗ truyền “theme: theme” ở Cockpit/SessionLog/DrivingData trong Main.qml. Có nguy cơ tự tham chiếu do property trùng tên id. Truyền qua tên/đường dẫn rõ, ví dụ root.designTheme, rồi xác nhận bằng QML runtime.
3. DrivingData.qml dùng id “data”, trùng tên property phổ biến của Item. Kiểm tra resolution của các binding; đổi id có phạm vi rõ nếu cần.
4. Kiểm tra Loader/required properties, signal handler, Canvas API, implicitHeight/Layout và vòng đời Drawer/PTT. Bản vá định sửa các điểm này đã thất bại kiểm tra ngữ cảnh, nên chưa được áp dụng.
5. Không tuyên bố khung đang chạy. Bắt đầu bằng lint/load QML và xử lý lỗi trước khi mở rộng.
6. Chốt baseline chức năng từ app trước các thay đổi UI dở dang; lập bảng từng chức năng/tùy chọn/tích hợp và đường truy cập cũ → mới. Không dùng code khung mới hoặc dữ liệu có trong backend làm lý do tự phát triển thêm chức năng ngoài baseline.

## 3. Phạm vi và nguồn dữ liệu

Được sửa QML/resources/font, đăng ký tài nguyên CMake, tài liệu và kiểm tra UI. **Không sửa src/**, training, API, schema/default settings, thuật toán, nhịp telemetry, voice/Spotter priority hoặc credential/storage.

Ràng buộc chức năng áp dụng cho toàn bộ plan: không thêm tính năng mới, không xóa hoặc làm mất tính năng đang có, kể cả dưới danh nghĩa làm gọn UI hay tối ưu hiệu năng. Navigation/drawer mới chỉ được tổ chức lại cách truy cập chức năng hiện hữu. Phần nào trong ảnh hoặc khung dở vượt baseline thì không phát triển thành chức năng mới. Giữ các điều kiện khả dụng/disabled vốn có; không bật giả hoặc loại bỏ chức năng chưa khả dụng.

Nguồn cần tra khi triển khai:

- qml/Main.qml và các file QML mới: giao diện.
- src/app/Application.h: hợp đồng facade backend.
- src/app/Application.cpp, phần toVariantMap/onStateUpdated: trường telemetry đã công bố và ước tính có sẵn.
- src/strategy/StrategyRecorder.h: các property/action record/train/sharing.
- docs/architecture/runtime-ui-storage.md: startup/setup/window/gates.
- docs/build.md và CMakeLists.txt: build Qt/Windows và resource.

| Vùng | Dữ liệu/hành động có sẵn |
| --- | --- |
| Phiên | connected, simulatorName, telemetry.track/carModel/currentLap/position. |
| Instrument | telemetry.speedKmh/rpm/gear/throttle/brake. Không có redline riêng cho từng xe; thang RPM trình bày không phải ngưỡng cảnh báo. |
| Bốn bánh | telemetry.tyreTemperatures/tyrePressures/brakeTemperatures; pressure đã là psi, lốp/phanh là °C. FL/FR/RL/RR index 0–3 theo WheelValues; thiếu trường hiển thị —, không thay 0 bằng thiếu. Nhiệt độ phanh được người dùng yêu cầu bổ sung. |
| Nhiên liệu | fuelLiters/fuelPerLapLiters/estimatedFuelLaps từ backend. Không tính lại ước tính trong QML. |
| Gap/lap | gapAheadSeconds/gapBehindSeconds, currentLapTimeSeconds/previousLapTimeSeconds/bestLapTimeSeconds. |
| Radio | latestEngineerText/latestUserText, voiceStatus/microphoneLevel, askText, beginPushToTalk/endPushToTalk. |
| Lịch sử | conversationLog chỉ có role/text; không tự tạo timestamp. |
| Sự kiện | eventLog có message/priority/timestamp; không tạo sector times, track map hoặc dữ liệu dự đoán không được công bố. |

Giữ gate startupReady, TTS benchmark/restoring, chiến thuật khả dụng, train khi disconnected, realistic setup theo phiên, consent chia sẻ, API key password và restart/tray semantics.

## 4. Layout đích

### Shell và điều hướng

- Ba nhóm: **Đua**, **Phân tích**, **Thiết lập**. Không sidebar sáu mục.
- Đua: Cockpit, truy cập history theo nhu cầu.
- Phân tích: Telemetry, Nhiên liệu & chiến thuật. Record/process/train ở nhóm dữ liệu/chiến thuật. Chỉ giữ Nhật ký phiên nếu đối chiếu xác nhận đây là trình bày lại chức năng có sẵn; không bổ sung chức năng mới.
- Thiết lập: Kỹ sư & Spotter, AI & giọng nói, Hệ thống & thiết bị. Giữ mọi control và setter cũ, đổi vị trí hợp lý.
- Header wordmark/nav/status và nút Windows. Giữ drag/resize/minimize/maximize/close/tray.

### Cockpit

1. Masthead: tên đường đua, simulator/xe, vòng và vị trí; nhãn dài elide có tooltip khi cần.
2. Instrument khoảng 30% / 40% / 30% chiều ngang: tốc độ/số/RPM/ga-phanh; bốn ô bánh 2×2; fuel ledger/gap/lap.
3. Vùng giữa là bốn ô FL/FR/RL/RR. Mỗi ô có nhiệt độ và áp suất; không còn hình xe/Canvas.
4. Dock dưới: câu kỹ sư mới nhất, composer, Gửi, Lịch sử radio, Giữ để nói. Câu dài có đường mở history; không mất nội dung.
5. History là Drawer và ListView ảo hóa. Giữ vị trí khi người dùng đọc phía trên, có cách về cuối.
6. Chưa kết nối/chưa warm-up/chưa có hội thoại phải có trạng thái thật, không đổ dữ liệu minh họa vào production.

Mục tiêu 1280×800 và 1600×1000. Ở 880×576, giữ header/dock dùng được, cho vùng instrument cuộn nếu cần thay vì thu nhỏ toàn bộ chữ. Không tràn ngang hoặc chồng nhãn/control. Kiểm tra thêm DPI 125/150% khi có môi trường.

### Typography, màu và control

- Grayscale trung tính: Canvas #111111, chrome #0B0B0B, surface #1A1A1A, raised #262626, divider #383838, control outline #777777.
- Chữ chính #F0F0F0, chữ phụ #B0B0B0, accent/focus #E0E0E0, chữ trên accent #111111. Mọi mã RGB có ba kênh bằng nhau; không xám ám xanh hoặc màu nhấn khác.
- Warning/error cũng dùng grayscale. Cảnh báo có nhãn “Cảnh báo” và biểu tượng tam giác; lỗi có nhãn “Lỗi”, biểu tượng và nét nhấn rõ. Kết nối dùng nhãn và hình dạng phân biệt connected/disconnected. Không dựa riêng vào độ sáng để truyền đạt trạng thái.
- Áp dụng xuyên suốt: bốn ô bánh, thanh RPM/ga/phanh, nav, PTT, focus/hover, nút đóng cửa sổ, icon, startup, dialog, thông báo và control Qt mặc định. Rà cả màu hard-code ngoài UiTheme.
- Body 14–16 px; nhãn phụ tối thiểu 12 px; condensed chỉ ở số/tiêu đề lớn; mono cho số liệu.
- Spacing 4/8 px, padding vùng 16–28 px, góc 2–4 px. Bề mặt phẳng, không glow/gradient trang trí/blur nặng.
- Chữ thường >=4,5:1; control/focus >=3:1. Kiểm tra lại tương phản bộ monochrome khi triển khai; kết quả bảng màu cũ không được coi là bằng chứng cho bảng màu mới.
- Control có hover/focus/pressed/disabled/loading/error phù hợp, accessible name và keyboard navigation.
- 0 hợp lệ khác missing; gear neutral/reverse theo dữ liệu normalized của backend; làm tròn lap time không tạo 60 giây sai định dạng. Nội dung radio dùng Text.PlainText.

## 5. Các bước thực hiện cho mô hình tiếp nhận

### Bước 0 — ổn định phần code dở

- Rà inventory và diff ở phần 2; xử lý binding/theme/import/required-property/layout errors.
- Lập bảng chức năng baseline → vị trí/luồng mới, dùng làm checklist bảo toàn chức năng cho các bước sau.
- Load QML bằng Qt thật; có thể dùng facade giả để độc lập khỏi voice models/Windows.
- Kết quả: khởi tạo UI không có ReferenceError/TypeError/binding loop; chưa đánh dấu nghiệp vụ hoạt động chỉ vì load thành công.

### Bước 1 — nền và shell

- Hoàn thiện UiTheme/font/resource, điều hướng ba nhóm và subnavigation.
- Chuyển token và mọi màu hard-code/control mặc định sang bộ monochrome; giữ ngữ nghĩa trạng thái bằng nhãn/biểu tượng.
- Giữ Qt Quick và facade, không thêm framework/lớp backend.
- Hoàn thiện focus, nhãn, trạng thái thật; bỏ mọi animation vô hạn.
- Kết quả: tìm được tất cả trang/chức năng cũ và không mất dữ liệu form do ẩn/thu nhỏ.

### Bước 2 — hoàn thiện concept 01 — Cockpit theo prototype 00

- Cân lại tỉ lệ instrument/sơ đồ/fuel, fonts và chiều cao dock bằng screenshot Qt thực tế.
- Số liệu 0/missing, đơn vị, trạng thái mất kết nối và tên xe/track dài hiển thị đúng.
- Hoàn thiện send/PTT/history; release/cancel/focus/unload không làm kẹt PTT hoặc gọi end nhiều lần.
- Kết quả: đối chiếu ảnh tham chiếu ở 1280×800/1600×1000, kiểm tra cửa sổ nhỏ và long text.

### Bước 3 — hoàn thiện các workspace còn lại

- Telemetry và Chiến thuật: trình bày nhất quán; khả dụng/disabled giải thích rõ; giữ train/record/share gates.
- Kỹ sư/AI/Hệ thống: sắp xếp form, switch/slider/combobox có keyboard; tránh control/label chồng nhau ở layout hẹp.
- Startup/setup/benchmark/Google voice dialog: giữ bước, retry/save, busy state và xác nhận hiện có.
- Kết quả: thao tác UI vẫn gọi đúng setter/invokable, không đổi default hoặc nghiệp vụ.

### Bước 4 — hiệu năng

- Chỉ tạo workspace đang chọn; chỉ tạo history khi mở, dùng ListView/reuse phù hợp.
- Bốn ô bánh dùng binding trực tiếp các trường đã công bố; bỏ toàn bộ Canvas/onPaint của hình xe.
- Notify signal thay timer polling. View ẩn/minimized ngừng binding live mà không làm mất state form.
- Setup content chỉ tải khi mở; bỏ hiệu ứng vô hạn/shader không cần thiết. Không giảm nhịp backend hoặc ưu tiên Spotter để đạt số đẹp.
- Ghi rõ tối ưu cấu trúc nào đã thực hiện. Nếu báo con số: đo cùng viewport/renderer/dataset và có baseline; không suy diễn thành mức giảm CPU/GPU toàn app khi chưa đo native.

### Bước 5 — nghiệm thu và bàn giao

- Chạy các kiểm tra dưới đây, rà diff giữ nguyên backend, cập nhật ảnh Qt thực tế và tiến độ.
- Đối chiếu từng mục trong bảng chức năng baseline → mới; mọi mục phải còn hoạt động và truy cập được, không có tính năng mới ngoài baseline.
- Không cài runtime AI lớn chỉ để xác nhận QML. Native build Windows/MSVC/VieNeu DLL là môi trường khác; ghi rõ nếu chưa có.
- Bàn giao ngắn, không tự commit/publish.

## 6. Checklist và môi trường kiểm tra

- [x] Người dùng chốt 01 — Cockpit; ảnh và ba font/OFL đã có.
- [x] Người dùng chốt monochrome đen–xám–trắng, thay thế màu xanh của preview.
- [x] AGENTS.md và plan bàn giao đã viết.
- [x] Bộ prototype 18 màn hình/trạng thái hoàn tất: docs/prototypes/README.md có chỉ mục/điều kiện hiển thị; index.html có gallery; manifest.json ghi kích thước, dung lượng, nguồn và SHA-256 của 18 PNG. Đã rà ảnh và đối chiếu file nguồn; đây chưa phải ảnh UI render thật.
- [x] Bảng baseline → mới: [baseline-parity.md](ui-validation/baseline-parity.md), audit property/method/call shape và facade runtime; không thêm SessionLog hoặc sửa nghiệp vụ.
- [x] Token/palette/control grayscale; trạng thái dùng nhãn/hình dạng, review ảnh Qt thực tế.
- [x] Toàn bộ 14 QML load/compile trong Qt 6.8.3; Main runtime 0 cảnh báo. CMake đăng ký QML/font/OFL và đường dẫn tồn tại; **chưa build gói qrc native**.
- [x] Sáu route và các đường truy cập chức năng cũ có kiểm tra runtime.
- [x] Cockpit theo kiến trúc mẫu; kiểm tra 0/missing/disconnected/nhãn dài.
- [x] Text/Enter, PTT chuột press/release, route cancel, focus-loss đều được kiểm tra; mỗi lượt PTT 1 begin/1 end; startup gating giữ nguyên.
- [x] History tải khi mở, ListView ảo hóa; streamed reply giữ vị trí đọc, minimize/restore giữ drawer/vị trí.
- [x] AI/voice, record/train/sharing/consent, benchmark và setup gates có kiểm tra facade; draft sạch đồng bộ, draft đang sửa được giữ.
- [x] Render và layout ở 880×576, 1280×800, 1600×1000; focus/Tab và font tiếng Việt kiểm tra trong môi trường Qt software.
- [ ] DPI native 125%/150%, trợ năng OS và bàn phím/DirectInput toàn hệ thống: cần máy Windows thực.
- [x] Hidden/minimized ngắt cập nhật live; giữ state form/history. Bốn ô bánh không còn Canvas; không animation vô hạn.
- [x] `src/**` không đổi; giới hạn native ghi rõ.
- [x] 18 ảnh route/size từ suite và gallery 50 ảnh trạng thái Qt thật, dùng fixture ghi rõ trong báo cáo.

### Bằng chứng và giới hạn

Suite cuối: `2026-10-18T12:09:29Z` (19:09:29 giờ Việt Nam), không có lỗi validation, Main QML/Qt 0 cảnh báo. Xem báo cáo JSON và README để biết trường hợp đã kiểm tra, nguồn baseline và lệnh tái chạy. Gallery dùng dữ liệu giả của harness, không phải phiên đua thật.

```bash
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software .tooling/ui/bin/python tests/ui/run_ui.py
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software .tooling/ui/bin/python tests/ui/capture_review.py
```

Tối ưu cấu trúc: workspace tạo khi truy cập lần đầu rồi giữ draft; setup tạo theo nhu cầu; history Loader/ListView tái dùng delegate và cập nhật tăng dần; binding live ngắt khi ẩn/minimize; hình xe/Canvas đã được thay bằng bốn ô dữ liệu bánh. **Chưa đo mức giảm CPU/GPU/FPS**, không đưa phần trăm cải thiện.

Cloud Linux/PySide6 chỉ kiểm chứng frontend và lời gọi facade. Chưa build Windows/MSVC/VieNeu DLL, chưa kiểm chứng simulator, microphone/loa, provider API thật, native file picker/tray/DirectInput/Ctrl+Space hoặc packaging qrc. Không coi facade giả là bằng chứng các tích hợp native hoạt động. Backend/settings/thuật toán/nhịp telemetry không bị sửa để ép chạy Linux.

## 7. Prompt ngắn để giao cho mô hình rẻ hơn

> Đọc AGENTS.md và docs/ui-redesign-plan.md. Hoàn thiện RaceEngineer theo concept 01 — Cockpit và bộ ảnh tại docs/prototypes/README.md, monochrome đen–xám–trắng thuần, dùng font local đã chuẩn bị. Bắt đầu bằng rà code dở và đối chiếu chức năng baseline. Không thêm tính năng mới; không xóa, tự vô hiệu hóa hoặc làm mất bất kỳ tính năng hiện có nào. Chỉ tổ chức lại giao diện và tối ưu thực thi, giữ nguyên backend, hành vi và các gate. Hoàn thiện navigation, radio/PTT/history và các trang cấu hình; kiểm tra QML, tương tác, dữ liệu 0/thiếu và layout 880×576/1280×800/1600×1000. Tận dụng tài nguyên có sẵn, dùng ít token, cập nhật checklist bằng bằng chứng, bàn giao ảnh render thật và giới hạn kiểm chứng Windows. Không tạo thêm concept, không tự commit/publish.
