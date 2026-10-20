# RaceEngineer — bàn giao triển khai

## Trạng thái và mục tiêu

- **Đã kiểm tra, commit/push và tái phát hành v1.0.6 sửa Cockpit chờ sai:** binary/tag từ `b0ecd9699c310d42dfbd525b7d29fe8ee40b6b4e`. AC/ACC đọc metadata mỗi giây bằng hai snapshot khớp nhau và thử mở static/graphics mapping đến muộn; xóa metadata cũ khi dữ liệu mới trống. Cockpit ưu tiên trạng thái kết nối, phân biệt thiếu tên track/xe với ngắt kết nối. 12/12 ca Qt ở 880×576/1280×800 đạt, 0 warning/log; root đã xem ảnh/đối chiếu hash. Windows build `39392660681` đạt 7/7 CTests, gồm `TelemetryMetadataTests` với provider thật và shared memory giả (13.36s), native/packaged setup UI, manifest/version/icon. Workflow `39395396279` thay tag/ba asset/Changes. [Bằng chứng](docs/ui-validation/release-1.0.6/metadata-reissue-2026-10-20/README.md). Chưa thử game/UDP/UAC thật hay update checker trong app; shutdown lúc model warmup chưa xác minh. `.agents/` và `skills-lock.json` không commit.
- **Phạm vi đã thực hiện — thiết lập game và cập nhật:** người dùng yêu cầu installer tự cài/bật AC Python companion, cấu hình ACC UDP, thêm trạng thái/khắc phục trong app và kiểm tra cập nhật; viết lại Quickstart. Yêu cầu này cho phép sửa `src/integration/**`, facade/CLI, kiểm tra tiến trình Windows và installer cần thiết, thay các giới hạn UI-only/không thêm tính năng trong phần lịch sử. Giữ nguyên các tính năng, thuật toán telemetry/voice/strategy hiện có.
- **Đã commit/push và tái phát hành v1.0.6 với thiết lập game/cập nhật:** binary/tag ở lượt đó từ `e57adf0c093eb06f77575886dc9997f5cc7b35aa`. `SimulatorSetup` dùng chung cho installer và mục Hệ thống & thiết bị, dò Steam/chọn AC thủ công, sao lưu và nâng quyền riêng bước chép companion; chặn ghi khi game đang chạy hoặc không kiểm tra được tiến trình. `UpdateChecker` kiểm tra stable/tái phát hành cùng version khi bấm nút, mở trang tải thủ công. [Quickstart](docs/quickstart.md); [bằng chứng](docs/ui-validation/release-1.0.6/setup-reissue-2026-10-20/README.md). Windows build `39372199687` đạt 6/6 CTests hiện có, native/packaged setup UI, manifest/version/icon; workflow `39373778949` đã thay tag/ba asset và release notes. Bộ kiểm tra hiện có không xác minh toàn bộ luồng mới: chưa thử UAC tương tác, game thật hay network flow của update checker trong app; shutdown lúc model warmup vẫn chưa xác minh. `.agents/` và `skills-lock.json` giữ nguyên, không commit.
- **Đã tái phát hành v1.0.6 ngày 2026-10-20 theo yêu cầu:** marker chưa kết nối đổi thành vòng tròn rỗng; đã kết nối giữ chấm đặc. Binary/tag ở lượt sửa marker từ `bc143b0c56ef9507e26b76fec3e70c787cb8bc3c`; Windows build `39363124808` đạt 6/6 CTests và native/packaged UI, workflow `39365222484` kiểm hash toàn bộ 1762 file rồi thay tag và ba asset trên release hiện có. Root đối chiếu checksum công khai/GitHub digests, manifest và EXE tải từ ZIP với kết quả CI; weights/preset Minh Quân giữ nguyên. Bốn ca Qt marker ở 880×576/1280×800 đạt, root đã xem ảnh. [Bằng chứng tái phát hành](docs/ui-validation/release-1.0.6/reissue-2026-10-20/README.md). Giới hạn model warmup/shutdown và game/thiết bị thật bên dưới vẫn áp dụng.
- **Phát hành gốc v1.0.6:** logo monochrome mới toàn app/tray/installer và hai README; binary gốc từ `a5a5fca8e3758b393243c93a7748cbfed07c60fe`, [release latest](https://github.com/tminh-U/RaceEngineer/releases/tag/v1.0.6). Windows CI đạt 6/6 CTests, version/manifest, icon nhúng và UI assertions trên EXE build/đóng gói; root đã xem 7 ảnh Qt Windows và đối chiếu checksum GitHub. Xem `docs/ui-validation/release-1.0.6/README.md`. `src/**` và `third_party/**` không đổi so với v1.0.5. Build release dùng AVX2; bản Vulkan cần driver/runtime Vulkan hoạt động. Test UI dừng instance sau assertions nếu model còn warmup, không chứng minh orderly shutdown lúc warmup; game/thiết bị thật vẫn chưa kiểm chứng.

- **Ủy quyền phát hành mới nhất:** người dùng yêu cầu tạo/thay logo monochrome toàn app và README, kiểm tra, commit/push GitHub, tăng phiên bản và release theo mẫu trước. Phạm vi này thay giới hạn không commit/publish ở các ghi chép lịch sử bên dưới. Chỉ phát hành binary được build/kiểm chứng từ nguồn mới; không đổi tên binary cũ thành phiên bản mới. `.agents/` và `skills-lock.json` là file cá nhân, không đưa vào commit ứng dụng.
- **Đã sửa cả 3 lỗi của audit bổ sung:** shortcut mở đúng tab voice theo backend, startup notice giới hạn viewport/cuộn nội dung và giữ tiêu đề/nút cố định, detail API lỗi không bị dịch thành thành công. Kiểm chứng mới: 3/3 nhóm regression tại 2026-10-19T14:04:05Z và 25/25 ca edge tại 2026-10-19T14:04:22Z, 0 QML warning/Qt log; root đã xem ảnh Qt và xác nhận hash nguồn của hai báo cáo khớp. Xem `docs/ui-validation/followup-fix/README.md`. Chỉ sửa Main/StartupNotice/LlmConnectionForm phía QML và kiểm tra/tài liệu; src/** không đổi, native chưa xác minh. Audit 22/25 cũ được giữ làm lịch sử trước sửa, không phải kết quả hiện tại.
- **Đã sửa các mục audit và review ảnh Qt cuối:** khôi phục tên đối thủ/fallback, binding ở System/Engineer; tách Telemetry riêng và chỉnh hierarchy/spacing. Suite 2026-10-19T04:12:02Z pass15QML/51method/0warning; audit 2026-10-19T04:13:38Z đạt19/19 kiểm tra dữ liệu/binding; gallery 2026-10-19T04:18:33Z có52ảnh và4capture grid tại 2026-10-19T04:18:56Z. Root đã đối chiếu ảnh; QML/CMake/run_ui khớp suite, toàn bộ nguồn gallery khớp. Method coverage chỉ chứng minh phạm vi UI/facade đã chạy; native Windows/simulator/thiết bị thật chưa kiểm chứng. Header giữ nguyên, Dashboard được điều chỉnh base font/layout để sửa sai lệch với preview (textScale vẫn 1), các trang ngoài giữ helper ×1,15.
- **Kết quả audit trước khi sửa:** Telemetry thiếu tên xe trước/sau (`opponentAhead/opponentBehind`); System ghi “Nút đang gán” nhưng binding chỉ còn ở Engineer. Tỷ lệ chữ/layout còn lệch prototype ngoài các thay đổi đã chốt. Xem `docs/ui-validation/design-and-feature-review-2026-10-19.md` và phần audit đầu plan; 51/51 method không chứng minh đầy đủ dữ liệu hiển thị. Lượt audit trước không thay production; yêu cầu “thế bạn sửa đi” sau đó đã cho phép sửa các mục phát hiện.

- **Cập nhật mới nhất:** nền bốn ô bánh phải trùng `theme.canvas` với Dashboard; mỗi ô hiển thị nhiệt độ lốp, áp suất và **nhiệt độ phanh** từ `telemetry.brakeTemperatures` index 0–3. Dữ liệu thiếu hiển thị —, 0 vẫn là 0°C. Đây là bổ sung trình bày được người dùng yêu cầu rõ, không đổi backend.
- **Yêu cầu mới nhất thay thế sơ đồ trong preview:** bỏ hình xe, thay vùng giữa bằng **4 ô FL/FR/RL/RR** theo bố cục 2×2. Mỗi ô giữ nhiệt độ và áp suất bánh tương ứng (indices 0–3), đơn vị rõ, phân biệt 0/thiếu. Đây là đổi cách trình bày dữ liệu hiện có; không xóa dữ liệu/chức năng. Không dựng lại hình xe/Canvas. Font ngoài dashboard vẫn +15%, header giữ font hiện tại; Cockpit chỉnh base theo preview với textScale=1; hiển thị ảnh Qt trực tiếp trong chat, kèm ZIP tải xuống khi cần; không upload/publish qua Higgsfield.
- **Yêu cầu sửa bổ sung:** Cockpit phải hiển thị đủ số liệu và radio mà không cuộn ở kích thước cửa sổ được hỗ trợ. Điều chỉnh kích thước mặc định/tối thiểu theo vùng màn hình khả dụng và bố cục; không giấu/cắt dữ liệu để né scroll. Typography phải đối chiếu preview 01 gốc: số lớn đậm, nét chữ rõ, body tiếng Việt sans regular/medium; xác minh font thực sự load/render thay vì chỉ kiểm tên file. Test agent rà thao tác động của toàn bộ control baseline; static parity không thay thế kiểm tra hành vi và native chưa chạy phải ghi rõ.
- **Phân vai mới nhất do người dùng chốt:** agent chính tự implement và review; chỉ giữ một Luna max kiểm tra (`ui_tests`). Không dùng lại hai coder `ui_core`/`ui_workspaces`. Không tự commit/publish.
- Người dùng đã chốt **01 — Cockpit**. Chuẩn monochrome: docs/prototypes/00-cockpit.png; ảnh layout ban đầu: docs/design-previews/R1-cockpit.png. Không hỏi chọn lại hoặc tạo hướng thiết kế khác.
- Đã có đủ **18 ảnh prototype** các màn hình/trạng thái. Bảng đối chiếu: **docs/prototypes/README.md**; gallery: docs/prototypes/index.html; metadata/kích thước/SHA-256: docs/prototypes/manifest.json. Đọc mục của màn hình đang làm; không cần nạp lại mọi ảnh/tài liệu ở mỗi bước. Đây là ảnh tạo sinh, không phải screenshot app chạy thật.
- **Bảng màu đã chốt: monochrome đen–xám–trắng thuần**, kể cả trạng thái/focus/control. Yêu cầu này thay thế màu xanh trong ảnh và màu nhận diện Matrix trong skill; không dùng xám ám xanh hoặc màu nhấn khác.
- Kế hoạch thực thi: docs/ui-redesign-plan.md. Đọc phần “Tiếp nhận” và “Các bước thực hiện” trước; tra phần còn lại khi cần.
- Code QML dở đã được hai coder rà và tích hợp. Trạng thái kiểm chứng hiện tại nằm trong docs/ui-validation/render-report.json và plan; không dùng phần hiện trạng tiếp nhận làm báo cáo hiện tại. Build native Windows vẫn cần môi trường phù hợp.

## Phân vai bắt buộc khi thực thi

Theo yêu cầu mới nhất, **agent chính tự sửa code production, tích hợp và review**. Chỉ dùng lại **một** test agent `ui_tests` (`gpt-6-luna`, reasoning `max`) để viết/chạy kiểm tra UI và hồi quy, facade/fixture và ảnh Qt thật. Không giao implement cho `ui_core`/`ui_workspaces`, không khởi chạy coder thay thế.

- Agent chính sở hữu QML/resource, tài liệu và review hình ảnh; tự sửa lỗi production do test phát hiện.
- Test agent sở hữu `tests/ui/**`, fixture và báo cáo/ảnh tại `docs/ui-validation/**`; không sửa production để làm test pass.
- Tránh ghi cùng file đồng thời. Test agent bàn giao lệnh, kết quả, ảnh và giới hạn native. Agent chính xem ảnh tại 880×576, 1280×800, 1600×1000 và sửa rồi kiểm tra lại nếu có lỗi.
- Giữ no-scroll Cockpit, header hiện tại, Dashboard scale=1 với base theo preview, các trang khác +15%, dữ liệu 0/thiếu, mapping bốn bánh, gates và mọi đường truy cập tính năng.

## Phạm vi được giao khi bắt đầu thực thi

- Reconstruct layout phía QML theo Cockpit: telemetry và bốn ô bánh ở trung tâm, radio dock phía dưới; lịch sử và cấu hình mở theo nhu cầu.
- **Không thêm tính năng mới; không xóa, tự vô hiệu hóa hoặc làm mất tính năng hiện có.** Giữ đầy đủ tùy chọn, tích hợp, thao tác và hành vi. Chỉ thay cách bố trí/trình bày và tối ưu thực thi, bảo toàn chức năng.
- Baseline là app trước các thay đổi UI dở dang, không phải các khung QML mới. Lập đối chiếu chức năng cũ → vị trí mới trước khi triển khai. Code khung hoặc chi tiết trong preview không tự tạo quyền thêm tính năng.
- Giữ nguyên backend src/**, STT/TTS, Spotter, LLM, thuật toán/nhịp telemetry, chiến thuật, settings schema/defaults, credential và storage.
- Chỉ sửa UI/QML, tài nguyên font/ảnh, resource CMake, tài liệu và kiểm tra frontend liên quan.
- Dùng facade trong src/app/Application.h. Không thêm API/nghiệp vụ để giống ảnh mẫu; UI chỉ định dạng dữ liệu, backend tiếp tục tính toán.
- Giữ startup gate, train/benchmark gates, realistic setup consent, chia sẻ dữ liệu, tray và thông báo restart.
- Không tự commit/push, đóng gói hoặc phát hành.

## Thực hiện với ít token

1. Kiểm tra git status và diff đúng các file đang sửa. Không reset/checkout cả repo; .agents và skills-lock.json là dữ liệu người dùng đã có.
2. Không khảo sát lại toàn repo. Dùng rg theo phạm vi, bỏ third_party/.tooling/.git; đọc đoạn cần sửa thay vì in toàn file.
3. Ponytail full đang là phong cách được yêu cầu: ít code cần thiết, hoàn tất đúng phạm vi, trao đổi ngắn. Khi cần tra skill local: .agents/skills/ponytail, redesign-existing-projects, minimalist-ui, typeui-matrix. Không đọc toàn bộ bộ skills hoặc áp quy tắc landing page lên app Qt.
4. Dùng lại font và bộ prototype đã có. Thứ tự ưu tiên: yêu cầu người dùng/phạm vi tính năng → plan/bảng trạng thái → ảnh. Chi tiết tạo sinh sai không được thành tính năng/default mới. Không tải lại model AI, không thêm framework web, không cài dependency production phục vụ việc chạy thử.
5. Làm từng bước trong plan, ghi tiến độ có bằng chứng. Agent chính tự implement/review và chỉ dùng một test agent như phân vai ở trên.
6. Không thu hẹp reconstruct thành đổi màu/font; đồng thời không mở rộng sang sửa backend.

## Ràng buộc UI/hiệu năng

- Một bộ token grayscale trung tính, mọi mã màu RGB có ba kênh bằng nhau. Không sage/xanh, vàng, đỏ hoặc màu thương hiệu ở bất kỳ control/icon/trạng thái nào. Không glow, blur nặng hay animation vô hạn.
- Phân biệt kết nối/cảnh báo/lỗi bằng nhãn rõ, biểu tượng hình học, kiểu nét/độ dày và độ sáng; không chỉ dựa vào khác biệt giữa các sắc xám. Xem token monochrome trong plan.
- Sans cho tiếng Việt, mono cho số liệu, condensed cho các số lớn; font local và OFL đã có.
- Phân biệt 0 với thiếu dữ liệu; thiếu dùng —. Hiển thị rõ “ước tính”; không chèn dữ liệu mẫu vào production.
- Bốn ô bánh dùng binding dữ liệu hiện có, không Canvas hoặc repaint hình theo telemetry. Các sơ đồ trong prototype/báo cáo cũ chỉ là lịch sử trước yêu cầu thay bằng ô.
- Notify binding thay polling. Chỉ tạo workspace đang chọn; history dùng ListView ảo hóa, chỉ tải khi mở. Dừng binding live ở view bị ẩn/thu nhỏ mà không xóa dữ liệu form.
- Không đổi nhịp telemetry hoặc ưu tiên voice/Spotter để giảm tải UI. Không đưa ra phần trăm cải thiện khi chưa đo.
- PTT phải kết thúc đúng một lần khi thả/hủy/đổi trang/mất focus. Giữ cơ chế PTT bàn phím và DirectInput hiện có. Native eventFilter trong src/app/Application.cpp yêu cầu **Ctrl+Space**; sửa nhãn Space của UI cũ cho đúng, không thêm handler QML gây kích hoạt hai lần.
- Đủ keyboard focus, accessible name, tương phản và khả năng đọc ở cửa sổ nhỏ. Dữ liệu/nhãn dài phải wrap/scroll phù hợp.

## Nghiệm thu và bàn giao

- Dùng checklist trong plan: QML, layout 880×576/1280×800/1600×1000, 0/missing, gửi lệnh/PTT/history/navigation/setup/gates.
- Qt/PySide6 + facade giả chỉ chứng minh UI; không chứng minh native audio/simulator/DirectInput. Môi trường dev .tooling/ui dùng PySide6-Essentials 6.8.3; test và ảnh render thật ở docs/ui-validation, lệnh chạy trong tests/ui.
- Build Windows/Qt/MSVC khi có môi trường phù hợp; cloud Linux không thay thế kiểm chứng app native.
- Rà diff xác nhận src/** không đổi. Chạy các kiểm tra phù hợp; không lặp suite không liên quan.
- Nghiệm thu đối chiếu đủ chức năng cũ → mới: không thêm, không bớt; tối ưu hiệu năng không được đánh đổi bằng việc bỏ tùy chọn/tích hợp hoặc làm mất hành vi.
- Kết quả cuối ngắn: đã làm gì, đã kiểm tra gì, còn giới hạn nào. Cập nhật plan; không đánh dấu hoàn tất bằng suy đoán.
