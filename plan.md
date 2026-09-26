# Plan: UI cho AI chiến thuật và lựa chọn thiết bị chạy

Ngày lập: 23/09/2026
Ngày cập nhật: 24/09/2026
Trạng thái: GPU core, UX Commit D và Strategy core đã tích hợp; `build/RaceEngineer.exe` chính build thành công 24/09/2026 18:19. Chưa có model đã duyệt hoặc dữ liệu train thật. XGBoost Ranker chọn trực tiếp vòng pit theo mục 13; notebook ở `training/pit_strategy/train_pit_ranker.ipynb`.

## 0. Trạng thái hiện tại

UI hiện có trong `qml/Main.qml`:

- Tab `Analysis & Strategy` có thẻ fuel và công tắc thật qua SettingsManager; trạng thái phản ánh model/profile/telemetry. Model chưa có nên không hiển thị vòng pit giả.
- Telemetry phân biệt trạng thái kết nối simulator với trạng thái nguồn dữ liệu; pill chờ AC / ACC đã bỏ khỏi sidebar.
- Selector thiết bị AI local đã nối với `Application`/`SettingsManager`; cấu hình GPU được lưu trong settings version 10.
- Vulkan resolver, chọn backend cho PhoWhisper/VieNeu-TTS và fallback CPU đã tích hợp. Build `RaceEngineer` đã thành công.
- Commit D (dashboard UX và wording Spotter) đã hoàn tất; việc kế tiếp là triển khai StrategyPredictor ở Commit E.

## 1. Phạm vi của đợt đầu

Đợt đầu chỉ làm UI contract và giao diện điều khiển:

- Công tắc bật/tắt AI dự đoán đặt ở trang **Kỹ sư**.
- Lựa chọn thiết bị chạy model local đặt ở trang **Cài đặt**.
- Chuẩn hóa tên property, signal và dữ liệu mà UI cần từ `Application`.
- Không load model.
- Không chạy XGBoost.
- Không thay đổi STT, TTS, telemetry, spotter hoặc LLM runtime.

Core/runtime sẽ được tích hợp ở đợt sau theo đúng contract đã chốt trong file này.

## 2. Quyết định UX

### 2.1. Công tắc AI chiến thuật

Tận dụng row hiện có trong `qml/Main.qml`:

```text
Đề xuất chiến thuật chủ động
```

Row này nằm trong trang **Kỹ sư**, không thêm một công tắc trùng trong trang Cài đặt.

Hành vi:

- Mặc định: tắt.
- Bật: cho phép core chạy dự đoán tyre wear, pit và weather khi đã được tích hợp.
- Tắt: core không chạy predictor và không tự đưa ra đề xuất.
- Subtitle khi core chưa tích hợp: `Sẵn sàng cấu hình · Chưa kết nối bộ dự đoán`.
- Subtitle khi đã tích hợp:
  - `Đang hoạt động`
  - `Đang tắt`
  - `Fallback deterministic`
  - `Thiếu model`

UI không được gọi đây là “AI đang chạy” nếu core chưa xác nhận trạng thái ready.

### 2.2. Lựa chọn thiết bị chạy model

Đặt trong trang **Cài đặt**, tạo card riêng:

```text
AI cục bộ & thiết bị chạy
```

Các lựa chọn hiển thị:

- `Tự động`
- `CPU`
- `Vulkan — <tên GPU 1>`
- `Vulkan — <tên GPU 2>`

Nếu không có Vulkan GPU:

- Vẫn hiển thị `Tự động` và `CPU`.
- Hiển thị thông báo `Không tìm thấy thiết bị Vulkan`.
- Không hiển thị lỗi đỏ làm người dùng tưởng app hỏng.

Status bên dưới selector:

- `Đang dùng: Tự động`
- `Đang dùng: CPU`
- `Đang dùng: Vulkan — Radeon ...`
- `GPU không khả dụng · sẽ fallback CPU`
- `Thay đổi sẽ áp dụng sau khi khởi động lại`

Lựa chọn này dành cho hai model local hiện có:

- PhoWhisper-small STT.
- VieNeu-TTS.

XGBoost chiến thuật vẫn là model CPU-only, không phụ thuộc selector GPU này.

## 3. Contract UI giữa QML và Application

Các property dự kiến thêm vào `Application`:

```cpp
Q_PROPERTY(bool strategyAiEnabled
    READ strategyAiEnabled
    NOTIFY strategyAiSettingsChanged)

Q_PROPERTY(QString strategyAiStatus
    READ strategyAiStatus
    NOTIFY strategyAiStatusChanged)

Q_PROPERTY(QVariantList aiComputeDevices
    READ aiComputeDevices
    NOTIFY aiComputeDevicesChanged)

Q_PROPERTY(QString selectedAiComputeDevice
    READ selectedAiComputeDevice
    NOTIFY aiComputeSettingsChanged)

Q_PROPERTY(QString aiComputeStatus
    READ aiComputeStatus
    NOTIFY aiComputeStatusChanged)

Q_PROPERTY(bool aiComputeRestartRequired
    READ aiComputeRestartRequired
    NOTIFY aiComputeSettingsChanged)
```

Các invokable dự kiến:

```cpp
Q_INVOKABLE void setStrategyAiEnabled(bool enabled);
Q_INVOKABLE void setAiComputeDevice(const QString& deviceId);
```

Không dùng QML trực tiếp để sửa file settings. QML chỉ gọi `Application`; `SettingsManager` sẽ là nơi lưu cấu hình khi core được tích hợp.

### 3.1. Dạng dữ liệu device option

Mỗi phần tử của `aiComputeDevices` có dạng:

```json
{
  "id": "auto",
  "label": "Tự động",
  "backend": "auto",
  "available": true,
  "selected": true
}
```

Ví dụ GPU:

```json
{
  "id": "vulkan:0",
  "label": "Vulkan — AMD Radeon 680M",
  "backend": "vulkan",
  "index": 0,
  "available": true,
  "selected": false
}
```

`id` là giá trị ổn định để lưu settings. `label` chỉ dùng để hiển thị.

## 4. UI-first implementation

### Bước U1 — Chỉnh `qml/Main.qml`

Phần trang Kỹ sư:

- Thay `Repeater` static hiện tại bằng row có binding thật cho AI chiến thuật.
- Gắn:

```qml
checked: backend.strategyAiEnabled
onToggled: checked => backend.setStrategyAiEnabled(checked)
```

- Không thay đổi các row nhiên liệu, tyre, delta hoặc pit khác ở đợt UI đầu.
- Không tự suy ra trạng thái từ `backend.connected`; trạng thái AI phải đến từ property riêng.

Phần trang Cài đặt:

- Thêm card `AI cục bộ & thiết bị chạy`.
- Thêm `ComboBox` hoặc control chọn item theo pattern combo đang có trong file.
- Bind model vào `backend.aiComputeDevices`.
- Hiển thị device label và `backend.aiComputeStatus`.
- Hiển thị cảnh báo restart nếu `backend.aiComputeRestartRequired`.

### Bước U2 — Không gây nhầm với GPU renderer của Qt Quick

Row hiện tại:

```text
Tăng tốc Phần cứng (GPU)
Qt Quick renderer đang hoạt động
```

không phải lựa chọn GPU cho STT/TTS.

Trong UI-first pass:

- Giữ row này nếu còn dùng cho Qt Quick.
- Không tái sử dụng row này cho model AI.
- Thêm selector AI riêng với subtitle rõ ràng.

Nếu sau này row Qt Quick không còn tác dụng, mới xem xét xóa trong một task riêng.

### Bước U3 — Prototype không có core

Nếu cần chạy thử QML trước khi core được tích hợp, dùng dữ liệu stub nội bộ ở mức UI:

- `strategyAiEnabled = false`.
- Device list gồm `Tự động` và `CPU`.
- Status: `Core AI chưa tích hợp`.

Stub phải được đánh dấu rõ và xóa khi `Application` expose property thật. Không để stub trở thành logic runtime lâu dài.

## 5. Cấu trúc settings hiện tại và kế hoạch tiếp theo

`SettingsManager` đã ở settings version 10 và đang lưu `local_ai`. Khi triển khai Strategy core, thêm `strategy_ai.enabled` mặc định `false` và nâng version lên 11. Cấu trúc đích:

```json
{
  "strategy_ai": {
    "enabled": false
  },
  "local_ai": {
    "compute_mode": "auto",
    "vulkan_device": ""
  }
}
```

Quy tắc migration:

- File cũ không có các key mới vẫn mở được.
- `strategy_ai.enabled` mặc định `false`.
- `local_ai.compute_mode` mặc định `auto`.
- `local_ai.vulkan_device` mặc định rỗng.
- Không reset các settings LLM, TTS, PTT hoặc audio hiện có.

## 6. Core integration sau UI

GPU runtime cho PhoWhisper/VieNeu-TTS ở mục 6.1–6.3 đã tích hợp. StrategyPredictor ở mục 6.4 vẫn chưa triển khai.

Phần 6.4 về StrategyPredictor vẫn chờ triển khai; các mục GPU phía trên đã hoàn tất.

### 6.1. GPU runtime resolver — đã tích hợp

Tạo helper dùng chung, ví dụ:

- `src/ai/LocalAiRuntime.h`
- `src/ai/LocalAiRuntime.cpp`

Nhiệm vụ:

- Liệt kê Vulkan device.
- Resolve `auto/cpu/vulkan:<id>`.
- Trả backend thực tế cho STT và TTS.
- Fallback CPU nếu:
  - build không có Vulkan;
  - runtime không có Vulkan device;
  - device đã bị rút/mất;
  - model GPU initialize thất bại.

Không để QML hoặc `main.cpp` tự đặt `GGML_VK_VISIBLE_DEVICES`.

### 6.2. PhoWhisper — đã tích hợp

Tích hợp runtime selection vào `WhisperRecognizer`:

- `use_gpu`.
- Vulkan device index.
- CPU retry sau lỗi GPU.
- Status thật sau warm-up.

### 6.3. VieNeu-TTS — đã tích hợp

Tích hợp selection vào `VieNeuTtsBackend/VieNeuWorker`:

- Chọn GPU cho native backbone.
- Fallback `VIENEU_GPU_LAYERS=0` khi GPU không dùng được.
- Fallback CPU cho codec/backend tương ứng.
- Không báo toàn bộ pipeline chạy Vulkan nếu một phần đang chạy CPU.

### 6.4. AI chiến thuật

Sau khi UI contract ổn định mới thêm:

- `StrategyPredictor`.
- Feature aggregation từ `RaceState` và `RaceHistory`.
- XGBoost Ranker chọn trực tiếp vòng pit theo hợp đồng mục 13.
- Không cần model tyre wear/weather riêng trong phiên bản đầu.
- Core deterministic kiểm tra luật/fuel, freshness và phát thông báo.
- Worker chạy theo event/lap; không chạy mặc định mỗi frame hoặc mỗi giây.

AI chiến thuật chỉ đưa recommendation. Không được điều khiển spotter safety-critical hoặc tự gửi lệnh pit.

## 7. Core status cần trả về cho UI

`strategyAiStatus` cần phân biệt:

- `Disabled`
- `Waiting for telemetry`
- `Loading model`
- `Ready`
- `Missing model`
- `Insufficient data`
- `Fallback deterministic`
- `Error`

`aiComputeStatus` cần trả về:

- requested backend;
- actual STT backend;
- actual TTS backend;
- selected GPU;
- fallback reason nếu có.

Ví dụ hiển thị:

```text
PhoWhisper: Vulkan — AMD Radeon 680M
VieNeu-TTS: Vulkan backbone · CPU codec
Strategy AI: Ready
```

## 8. Acceptance criteria cho UI-first pass

- Row `Đề xuất chiến thuật chủ động` nằm ở trang Kỹ sư.
- Không có công tắc Strategy AI trùng ở trang Cài đặt.
- Toggle mặc định tắt.
- Toggle có binding tới property contract rõ ràng.
- Trang Cài đặt có selector thiết bị AI riêng.
- Selector phân biệt `Tự động`, `CPU` và Vulkan GPU.
- Không nhầm selector AI với Qt Quick GPU renderer.
- Không có Vulkan vẫn hiển thị UI hợp lệ với `CPU`.
- Có trạng thái restart required.
- Không model nào được load hoặc inference chỉ vì UI được mở.
- QML không có warning do property/model không tồn tại trong bản có stub.
- Layout không làm hỏng các trang Kỹ sư và Cài đặt hiện có.

## 9. Acceptance criteria cho đợt core sau

- Tắt Strategy AI thì không tạo worker/model và không tăng CPU.
- Bật Strategy AI thì predictor chạy ngoài telemetry thread.
- Thiếu model/dữ liệu thì fallback deterministic.
- Không có Vulkan thì PhoWhisper và VieNeu-TTS tự chạy CPU.
- GPU được chọn đúng khi có nhiều Vulkan device.
- GPU init lỗi ở một model không làm model còn lại tắt theo.
- LLM chỉ đọc recommendation qua tool, không tự thay đổi kết luận authoritative.
- Tests offline chạy được khi không có simulator, microphone, internet hoặc GPU.

## 10. Các việc cố ý chưa làm

- Không train model trong lúc chơi.
- Không thêm Transformer, LSTM hoặc reinforcement learning.
- Không hot-switch GPU khi model đang chạy; bản đầu yêu cầu restart.
- Không dự đoán weather khi telemetry không có weather input.
- Không cho AI tự điều khiển pit/xe.
- Không thêm settings cho những hyperparameter chưa có nhu cầu người dùng.

## 11. Ưu tiên tiếp theo: Dashboard, trạng thái kết nối và wording Spotter

**Thứ tự:** làm pass UX này sau Commit C (GPU core) và trước khi bắt đầu StrategyPredictor. Đây là task UI/copy và phrase của damage event; không triển khai predictor, model dự đoán hoặc inference trong task này.

### 11.1. Thiết kế lại thẻ mô hình suy luận

- Chỉnh thẻ OpenAI Compatible API theo ảnh tham chiếu: tách bạch trạng thái endpoint với model đang chọn; trình bày nhãn “Mô hình đang chọn” và tên model thành một hàng dễ quét; phần chi tiết/trạng thái đặt riêng bên dưới.
- Thay câu “Model available” chung chung bằng trạng thái tiếng Việt có nghĩa theo kết quả kiểm tra API (`apiState`/`apiDetail`): chưa cấu hình, chưa kiểm tra, đang kết nối, đã kết nối hoặc lỗi.
- Không hiện API là trực tuyến chỉ vì đã có URL/model trong cấu hình; màu và chữ trạng thái phải theo kết nối thực tế.

### 11.2. Thay thẻ liên kết hệ thống bằng khu vực Analysis & Strategy

- Gỡ thẻ “TRẠNG THÁI LIÊN KẾT HỆ THỐNG” khỏi Dashboard và đặt thẻ “Analysis & Strategy” vào vị trí đó.
- Chuyển thẻ tính toán nhiên liệu/ma trận chiến thuật khỏi Telemetry vào tab “Analysis & Strategy”.
- Trước khi có StrategyPredictor, thẻ phải nói rõ “Chưa tích hợp”; không dùng dữ liệu mẫu và không tự tuyên bố AI sẵn sàng.
- Để phần kết quả dự đoán chỉ hiển thị khi StrategyPredictor trả trạng thái/kết quả hợp lệ ở Commit E.

### 11.3. Gọi tên rõ từng trạng thái kết nối

- Đổi badge “Chưa kết nối” thành trạng thái chỉ rõ simulator: “Simulator: Chưa kết nối AC/ACC” hoặc “Simulator: Đã kết nối {tên simulator}”.
- Badge AI API phải dựa trên `apiState`, không dựa riêng vào `apiConfigured`; phân biệt chưa cấu hình, chưa kiểm tra, đang kết nối, đã kết nối và lỗi.
- Làm rõ “Radio sẵn sàng” là trạng thái mic/PTT: dùng voice status và tên microphone hiện tại để người dùng biết radio đang chờ nói hay đang nghe.
- Không hiển thị DirectInput luôn màu xanh. Dùng `directInputStatus` ở khu vực gán nút để nêu thiết bị/controller chưa sẵn sàng, chưa map hay đã sẵn sàng.

### 11.4. Làm rõ cảnh báo hư hại và câu thoại

- Đổi “Thông báo cơ học khẩn cấp” thành “Cảnh báo hư hại”; subtitle nêu rõ đây là thông báo khi telemetry ghi nhận mức hư hại tăng và cảnh báo vẫn ưu tiên trước phản hồi AI.
- Câu Spotter mặc định khi không xác định được vị trí: “Phát hiện hư hại mới trên xe.” Áp dụng cho hư hại ở bất kỳ phía nào, không mặc định là phía trước.
- Khi telemetry có vị trí và mức độ hư hại thân xe, dùng mẫu “Phát hiện hư hại {mức độ} ở {vị trí}.” Ví dụ: “Phát hiện hư hại nặng ở phía sau.” Mức độ nhẹ/trung bình/nặng lấy từ phân loại `minor/moderate/major` hiện có; nhiều vùng thì nêu mức độ của từng vùng.
- Nếu chỉ xác định được vị trí mà chưa có phân loại mức độ đáng tin cậy (như hư hại bánh/giảm xóc), dùng “Phát hiện hư hại ở {vị trí}.”
- Kênh damage “tổng thể” là số tổng hợp, không phải một phía của xe. Nếu chỉ kênh này thay đổi, dùng câu mặc định; nếu đã có vùng cụ thể thì không đọc thêm “tổng thể” như một vị trí.
- DamageDetected hiện ghép câu “Va chạm hoặc hư hại mới”, nhưng telemetry hiện có không xác nhận va chạm. Câu mới chỉ nói về hư hại.
- Giữ nguyên trigger theo damage telemetry và EventPriority::Spotter; không đổi thành kết luận tai nạn.
- `assets/spotter/manifest.json` chưa có entry damage; Spotter ghép phrase theo telemetry và phát qua TTS động, nên không thêm WAV/cache giả cho câu mới.

### Acceptance criteria

- Người dùng nhìn thấy model đang chọn và biết API đã được kiểm tra/kết nối hay chưa; cấu hình có sẵn không bị trình bày thành kết nối thành công.
- Mỗi trạng thái dashboard/header ghi rõ đối tượng đang kết nối; DirectInput không còn trạng thái xanh cố định.
- Thẻ hệ thống được thay bằng Dự đoán AI; trạng thái trước khi có core là “Chưa tích hợp”, không có dự đoán giả.
- Câu mặc định áp dụng cho hư hại ở mọi phía; khi có vị trí thì đọc đúng vùng/bánh xe và mức độ đã phân loại nếu có. Không đọc “tổng thể” như vị trí hay khẳng định va chạm. Giữ nguyên độ ưu tiên Spotter; cache audio/manifest khớp với phrase mới nếu dùng.
- Kết thúc pass này trước Commit E; không thêm hoặc chạy StrategyPredictor trong pass UX.

## 12. Thứ tự commit đề xuất

### Commit A — UI contract/prototype (đã triển khai)

- `qml/Main.qml`
- UI state/stub tối thiểu nếu cần.
- Không model/runtime change.

### Commit B — Settings/Application contract (đã triển khai)

- `SettingsManager`.
- `Application` properties/signals/invokables.
- Settings migration.

### Commit C — GPU resolver và fallback (đã triển khai)

- Vulkan enumeration.
- STT/TTS backend selection.
- CPU fallback.

### Commit D — Dashboard UX và wording Spotter (đã triển khai)
- Thẻ mô hình suy luận hiển thị model rõ ràng cùng trạng thái API/runtime đã Việt hóa.
- Header dùng trạng thái kết nối AC/ACC thật; DirectInput không còn badge xanh giả và trạng thái cài đặt được diễn giải rõ.
- Thay thẻ liên kết hệ thống bằng khu vực “Analysis & Strategy”; trạng thái StrategyPredictor hiện “Chưa tích hợp”.
- Gỡ pill trạng thái Kỹ sư AI khỏi Dashboard; chuyển công tắc pit từ trang Kỹ sư sang tab riêng và nêu rõ đây mới là trạng thái giao diện.
- Đổi “Thông báo cơ học khẩn cấp” thành “Cảnh báo hư hại”; Spotter nêu vùng và mức độ khi telemetry có, dùng câu tổng quát khi chỉ tăng kênh damage tổng hợp.

### Commit E — Strategy core

- Feature history.
- XGBoost Ranker chọn vòng pit (chi tiết và dataset gates ở mục 13).
- Deterministic legality/freshness guards và tự động gọi pit.
- Tool/UI status integration.

### Commit F — Model assets, tests và package verification

- Model artifacts.
- Offline tests.
- Production package/runtime verification.

Commit D đã đáp ứng acceptance criteria ở mục 11; thứ tự tiếp theo là **Commit E — Strategy core**.

## 13. Kế hoạch triển khai Pit Strategy bằng XGBoost Ranker

Quyết định ngày 24/09/2026. Mục này thay thế đề xuất model tyre wear/pit predictor
chung ở mục 6.4 và cụ thể hóa Commit E/F. Chưa triển khai runtime trong đợt viết plan.
Notebook: `training/pit_strategy/train_pit_ranker.ipynb`.

### 13.1. Mục tiêu và ranh giới

- Model **chọn trực tiếp vòng pit** bằng ranking: một query là một trạng thái đua,
  một candidate là một vòng pit khả thi. Chọn candidate có score cao nhất.
- Không cần LAYA, LLM hoặc model dự đoán phụ để vận hành phiên bản đầu.
- Core C++ sở hữu candidate legality, thiếu dữ liệu, tuổi kết quả và thông báo;
  không thay lựa chọn model bằng một bộ chấm điểm chiến thuật khác khi model hợp lệ.
- Score ranking không phải xác suất thành công. UI không hiển thị confidence % tự suy ra.
- Người lái được tự động báo chuẩn bị pit và “Vào pit cuối vòng này” trước điểm rẽ.
  Không gửi lệnh điều khiển xe hoặc tự đặt dịch vụ pit.
- Ưu tiên CPU thấp: một model nhỏ, một luồng inference, tính theo sự kiện; giám sát
  thời điểm gọi pit là phép kiểm tra nhẹ trên telemetry hiện có.
- **Phạm vi model v1:** race tính vòng, điều kiện khô, còn đúng một stop bắt buộc,
  một cấu hình dịch vụ pit cố định, profile track/car/rules đã có dữ liệu kiểm chứng.
  Phải hiển thị unsupported cho race tính giờ/no-stop/multi-stop/wet ngoài phạm vi.
  Đây là mốc đầu có thể kiểm chứng, không phải tuyên bố hoàn tất mọi loại race AC/ACC.

### 13.2. Luồng tích hợp vào code hiện có

```text
Application::onStateUpdated → RaceHistory (giữ nguyên tính toán hiện có)
  → snapshot feature + luật race → danh sách vòng pit hợp lệ
  → StrategyPredictor worker: XGBoost Ranker → candidate đứng đầu
  → kiểm tra session/revision/freshness/legality → StrategyRecommendation
  → theo dõi vòng/vị trí → RaceEvent → MessageDispatcher → radio/TTS hiện có
```

- Đọc normalized `RaceState`, `RaceHistory`, không đọc trực tiếp struct AC/ACC trong model.
- `Application` quản lý vòng đời worker và nối property/signal vào `qml/Main.qml`.
- Thêm source strategy tối thiểu trong `src/strategy/`; không dựng framework plugin/model router.
- Dùng XGBoost native C API trên CPU; pin phiên bản dependency khi tích hợp, quản lý
  model bằng RAII, không nhúng Python hoặc cài Python trong app.
- Thêm CMake/runtime DLL và production manifest ở Commit F; không đổi GPU STT/TTS.
- Đọc JSON model đã cắt tại best iteration; schema float32/order/NaN giống notebook.
- Bật/tắt thật qua `SettingsManager` (migration kế tiếp version 10, dự kiến 11);
  thay công tắc prototype. Không đụng cấu hình local_ai đang hoạt động.

### 13.3. Dataset: ưu tiên và nguồn

1. **Sim racing:** tự ghi AC/ACC, log người dùng/league được phép sử dụng; tách
   simulator, game version, track/layout, car/class, race profile. Recorder chưa tồn tại.
   [AiM ACC telemetry guide](https://www.aimsportsystems.com.au/download/doc/eng/simracing/AssettoCorsaCompetizione_100_eng.pdf)
   là nguồn hướng dẫn ghi telemetry bổ sung; không mặc định app đọc được binary AiM/MoTeC.
2. **Đua thật:** GT3 từ [SRO results](https://www.gt-world-challenge-europe.com/results),
   WEC từ [Al Kamel](https://fiawec.alkamelsystems.com/) (ưu tiên LMGT3, tách lớp xe khác),
   F1 từ [FastF1](https://github.com/theOehrly/Fast-F1). Timing không đồng nghĩa full telemetry.
3. **Các hạng thấp hơn:** GT4/touring/one-make khi có nguồn chính thức và mapping phù hợp.

- Không có dataset pit tối ưu đã xác nhận trong repo. `dataset/` hiện dùng cho voice TTS.
- Lưu provenance/URL/ngày lấy/quyền sử dụng; Al Kamel hạn chế phân phối dữ liệu.
  Không tự tải hàng loạt hoặc đưa raw data/model vào Git. Xác nhận quyền train/phát hành.
- Ưu tiên không có nghĩa cộng mọi nguồn thành một dataset lớn: mặc định chỉ sim đúng profile.
  Dữ liệu thật dùng phân tích/calibration trước; thử transfer riêng và chỉ giữ nếu cải thiện
  trên test sim cố định. Báo metrics theo domain; không để số lượng F1 lấn át sim.
- Thiếu fuel/wear/rules phải đánh dấu thiếu; không bịa giá trị hoặc lấy tương lai điền ngược.

### 13.4. Recorder và nhãn quyết định

- Ghi bất đồng bộ theo lap/event; không serialize toàn bộ telemetry 30 Hz.
  Nếu cần mẫu trong vòng để định vị pit, lấy field cần thiết với tần suất giới hạn riêng.
- Ghi ID event/session/driver, UTC start/end, simulator/version, track/car/profile,
  completed laps, fuel delta, tuổi bộ lốp có nguồn xác nhận, pit service, flags/invalid laps,
  traffic và snapshot trước quyết định. Không reset tyre age khi pit mà không thay lốp.
- Bounded queue, flush theo đợt, giới hạn dung lượng/retention rõ; lỗi ghi không block telemetry.
  Recorder tùy chọn, thông báo mất mẫu; không âm thầm dùng log thiếu làm training chuẩn.
- Timing log đã quan sát không có counterfactual: pit vòng 19 không chứng minh 19 tối ưu.
- Tạo nhãn từ mô phỏng đã calibration bằng TRAIN telemetry; chạy các candidate trên cùng
  tập future scenarios/seeds, luật và dịch vụ. Xếp grade 0..3 theo expected remaining race
  time/regret; giữ cost/scenario provenance làm label/evaluation, tuyệt đối không làm feature.
- Có thể dùng expert ranking có provenance; nhãn expert không có cost thì không báo regret giây.
- Tách mô phỏng toy của notebook khỏi calibrated simulator thật. Simulator/gán nhãn thật
  là hạng mục cần triển khai/hiệu chỉnh offline, không coi notebook demo đã giải quyết nó.
- Không fine-tune bằng race test hoặc dùng dữ liệu vòng chưa hoàn thành trong feature.

### 13.5. Hợp đồng model v1

Notebook là schema v1 tham chiếu; không thêm feature chỉ có offline mà runtime không có.

| Feature | Ý nghĩa |
|---|---|
| laps_remaining | Gồm vòng hiện tại; race tính vòng |
| fuel_laps_remaining, reserve_laps | Fuel range từ tính toán hiện có + reserve profile |
| stint_laps | Tuổi bộ lốp đã xác nhận |
| pace_mean_s, pace_trend_s | Từ các vòng hoàn thành trước decision; trend dương = chậm đi |
| gap_ahead_s, gap_behind_s | NaN khi không có; không lấy private telemetry đối thủ |
| pit_loss_s | Ước tính có trước decision, đo/cấu hình theo profile |
| window_open_offset, window_close_offset | Giới hạn luật pit tính bằng offset vòng |
| candidate_offset | candidate_lap - current_lap |

- `candidate_lap=k` nghĩa pit cuối vòng k; current_lap 1-based là vòng đang chạy.
- Liệt kê đủ ứng viên nguyên trong [max(0, window_open_offset),
  min(window_close_offset, laps_remaining-1, floor(fuel_laps_remaining-reserve_laps-1))].
  Fuel filter v1 bảo thủ cho một vòng trọn từ vị trí hiện tại; thiếu candidate thì báo
  tình trạng nhiên liệu/không có phương án hợp lệ, không ép ranker chọn một vòng bất khả thi.
- Race profile xác nhận dịch vụ sau stop đủ hoàn thành race, sức chứa fuel và pit rules.
  Nếu profile không bảo đảm điều đó, không chạy model one-stop.
- Cần audit field thật: normalized state chưa có đầy đủ tyre-set/service/rules/car metadata.
  Bổ sung từ telemetry đáng tin hoặc race profile nhập tay; không suy ra tùy tiện.
- Race có thể hoàn thành không pit cần action `NO_STOP` ở schema sau, không ép vào pit.
- Tie score chọn vòng sớm hơn. Không tự suy pit window từ khoảng score chưa calibration.
  UI v1 hiển thị vòng model chọn và cửa sổ luật riêng, tránh gọi cả hai là cùng một thứ.

### 13.6. Train, đánh giá và artifact

- Notebook có demo end-to-end, import raw tùy chọn, real candidate CSV validation,
  chronological event split, train `XGBRanker(rank:pairwise)`, đánh giá, export.
- Cấu hình khởi đầu: CPU histogram, n_jobs=1, tối đa 96 cây depth 3, early stopping;
  không khẳng định cấu hình tối ưu hoặc mức CPU cụ thể khi chưa đo.
- Split toàn event (mọi xe/decision cùng race ở cùng phần), theo thời gian; purge ranh giới
  chồng session. Dữ liệu derived/synthetic từ một event phải giữ chung source group.
- Metrics: NDCG@1, top-grade hit (chấp nhận ties), simulation regret so với best candidate,
  so baseline đơn giản; đánh giá theo domain/profile. Accuracy pit đã quan sát không đủ.
- Regret trong simulator là bằng chứng trong simulator; replay không chứng minh phương án
  chưa chạy ngoài đời. Phải có held-out sim sessions + shadow mode/controlled runs.
- Xuất `pit_ranker.json`, `feature_schema.json`, `manifest.json`, `parity_vectors.json`,
  split IDs, metrics, label provenance. Có model/data hash, versions, scope, feature order.
- Manifest luôn `deployment_ready=false` trong notebook. Demo tuyệt đối không được promote.
  Promotion model thật cần ghi bằng chứng các gate; runtime từ chối artifact chưa duyệt,
  schema/hash/profile sai, missing required fields hoặc data ngoài phạm vi.

### 13.7. CPU và cơ chế gọi pit

- Load model một lần khi bật; một worker CPU, XGBoost nthread=1, batch candidate.
- Không chạy model mỗi telemetry frame/giây mặc định. Trigger theo lap hoàn thành,
  thay đổi fuel bất thường/rules/traffic đáng kể/hư hại/đối thủ pit hoặc yêu cầu người lái.
  Chỉ dùng trigger có nguồn dữ liệu thật; damage/wet ngoài scope v1 thì invalidate.
- Gộp trigger, một request chạy + một snapshot mới nhất chờ; không queue vô hạn.
  Kết quả kèm session ID, state revision, profile/model version và thời điểm hết hạn;
  bỏ kết quả cũ hoặc candidate đã qua điểm rẽ. Không apply kết quả sau disable/disconnect.
- Dùng telemetry có sẵn theo dõi thời điểm phát: chuẩn bị pit → pit vòng này → hoàn thành.
  Mỗi giai đoạn một lần; reset khi đổi session, pit xong hoặc đổi plan có ý nghĩa.
- Gọi trước pit entry với thời gian dự phòng nghe/phản ứng. Chưa có vị trí entry tin cậy
  thì báo từ đầu vòng; recommendation đến quá muộn không đọc lệnh rẽ gấp.
- Chỉ đổi recommendation khi đủ bằng chứng theo hysteresis được hiệu chỉnh trên validation;
  đổi bắt buộc nếu phương án cũ không hợp lệ. Không biến score gap thành confidence %.
- Dùng `RaceEvent`/`MessageDispatcher` hiện có; strategy không chặn spotter/fuel critical,
  invalidate thông báo đang xếp hàng nếu plan đã bị hủy. Kiểm tra freshness cả lúc phát.
- Đo CPU ms/call, p50/p95 latency, calls/min, RAM, queue depth và game frametime off/on;
  bao gồm feature aggregation/dispatch và tách chi phí TTS. Không chỉ báo latency model.

### 13.8. Các bước thực hiện còn lại (Commit E/F)

- [ ] E1 — Recorder, profile rules và feature availability audit; xuất schema CSV.
- [ ] E2 — Thu sim logs, calibrated candidate labeler offline, dataset có provenance;
      dùng notebook train/đánh giá. Không dùng toy model để vượt qua E2.
- [ ] E3 — Candidate builder, native ranker worker, validation artifact, state lifecycle.
- [ ] E4 — Pit callout state machine + cancel queued stale messages + settings thật;
      nối Dashboard/Analysis & Strategy với disabled/waiting/unsupported/missing model/
      insufficient data/ready/error và vòng pit được chọn. Giữ fuel tools đang hoạt động.
- [ ] E5 — Shadow mode, đo CPU/frametime, kiểm tra hành vi radio và chọn model đạt gate.
- [ ] F — Đóng gói đúng app RaceEngineer: XGBoost DLL/license, model đã duyệt,
      feature schema/hash/runtime dependencies; build và package verification.

Kiểm tra khi triển khai (theo yêu cầu kiểm chứng từng mốc): Python/C++ parity; lap off-by-one;
fuel/window bounds; NaN/thiếu field; ties; late worker; pause/replay/session reset; disconnect;
disable/re-enable; pit service không đổi lốp; expired queued audio; unsupported race profile;
thông báo trước pit entry và không lặp. Offline fixtures, không cần AC/ACC hoặc mạng.

### 13.9. Mở rộng sau v1

Tiến độ triển khai 24/09/2026: native Ranker adapter, guard manifest/schema/hash,
profile race, UI/status/settings, callout tự động và recorder lap/pit đã được thêm.
Mã đã biên dịch và liên kết thành công vào `build/RaceEngineer.exe` chính sau khi đóng app.
Chưa có dataset pit được gán nhãn
hoặc model đã duyệt; E2/E5/F model artifact vẫn chờ dữ liệu và đánh giá thật.

- Race tính giờ: action theo pit lap nhưng constraints theo ETA/giây và quy tắc kết thúc
  sau timer; dự phòng thêm vòng, không làm tròn thời gian còn lại một cách tuyệt đối.
- No-stop/multi-stop: thêm action/schema và continuation policy để đánh giá toàn race;
  giữ luật mandatory stop, refuel, thay lốp/driver và thời gian stint theo series.
- Traffic sau pit, wet/compound transitions và nhiều track/car: bổ sung feature chỉ khi
  runtime cung cấp được, train model/schema mới và đánh giá lại. Không đổi schema v1 ngầm.
- Nguồn GT3/WEC/F1/hạng thấp hơn chỉ được đưa vào model phát hành sau ablation chứng minh
  lợi ích trên sim. Hoàn thiện module rộng hơn cần các gate này, không chỉ train thành công.

## 14. Đánh giá material ACC và kế hoạch train pit Ranker (24/09/2026)

Tài liệu tham chiếu: [ACC Machine Learning Datasets](https://docs.google.com/document/d/1eWLVpeg6cCJNfluJWzQIlSHEY8l3JWRsm7mIvMl3Wlk/edit). **Kết luận: đủ để thiết kế recorder và thử nghiệm phương pháp; chưa đủ để train model chọn vòng pit có bằng chứng hiệu quả.** Giữ mục tiêu ở §13: chọn trực tiếp vòng pit bằng XGBoost Ranker, CPU một luồng. Hồi quy hao mòn lốp chỉ là nghiên cứu phụ sau khi xác minh được nhãn, không là điều kiện bắt buộc cho v1.

### 14.1. Kiểm kê nguồn và khoảng trống

| Material trong tài liệu | Dùng được | Chưa chứng minh được |
|---|---|---|
| [Lero ACC KPI, 174 tay đua/1.327 vòng tại Brands Hatch](https://github.com/ESRLAccount/SimRacing_KPITelemetryDataAnalysis) | Lap pace, driving style, thử pipeline/kiểm tra feature | CSV công bố không có fuel, pit service, luật pit hoặc cost của các vòng pit khác nhau; không là nhãn chiến thuật. |
| [ACC Server Admin Handbook, mục VIII.1](https://cdn.g-portal.com/ServerAdminHandbook_v8.pdf) | Kết quả, lap/split, penalty và trạng thái mandatory stop để đối chiếu recorder | Ví dụ JSON chính thức không có chuỗi pit-in/out, thời gian pit box, refuel hay thay lốp. Không suy ra trực tiếp vòng pit tối ưu. |
| [ac-data-eng-project](https://github.com/robin-ede/ac-data-eng-project), [acr_telemetry](https://github.com/decnet100/acr_telemetry), [PyAccSharedMemory](https://github.com/rrennoir/PyAccSharedMemory) | Tham khảo cách thu telemetry ACC, schema và sanity check | Là công cụ/pipeline, không phải bộ race đã gán nhãn. RaceEngineer đã có recorder JSONL; không thêm Kafka/PostgreSQL/recorder thứ hai chỉ để train. |
| [Kaggle F1 PitNextLap](https://www.kaggle.com/datasets/aadigupta1601/f1-strategy-dataset-pit-stop-prediction/discussion) và dữ liệu F1 thật | Thử import, đặc trưng và benchmark ngoài miền | Nhãn PitNextLap là lựa chọn đã xảy ra, không chứng minh lựa chọn đó tối ưu; ROC-AUC cao không đo lợi ích chiến thuật. Không trộn F1 vào ACC/AC mặc định. |
| ACC `tyreWear[4]` trong struct và đề xuất wear regressor | Kiểm tra khả năng đọc trong nhiều stint và sau thay lốp | Tên field không bảo đảm giá trị ACC thay đổi hoặc phản ánh mòn thực; [triển khai telemetry khác báo ACC không xuất live tyre wear](https://github.com/Rgosh/ac-pro-engineer). Chưa có nhãn wear đáng tin để train hồi quy. |

### 14.2. Thứ tự thực hiện

Đã bắt đầu bước 1: recorder ghi riêng `pit_enter`/`pit_exit` và
`pit_box_enter`/`pit_box_exit`; `training/pit_strategy/audit_sim_logs.py`
kiểm kê stop, log lỗi và độ phủ telemetry theo session. Chưa có race log AC/ACC
đủ pit service và nhãn cost để mở bước train model thật.

1. **Audit dữ liệu, chưa train:** lấy race ACC có pit hoàn chỉnh và race AC riêng; đối chiếu JSONL hiện có với kết quả server, game version, track/car, race rules. Xác minh lap/pit enter/exit, fuel trước-sau, tyre service, pit loss, cờ/invalid lap; kiểm tra `tyreWear` thay đổi hợp lý hay là giá trị không khả dụng. Ghi tỷ lệ thiếu và lỗi theo simulator/profile. Nếu thiếu field cốt lõi, bổ sung đúng field đó vào recorder hiện có ở tần suất lap/event, không ghi physics mỗi frame.
2. **Chốt phạm vi đầu tiên:** một race tính vòng, khô, một mandatory stop, track/car/class và luật pit xác nhận, đúng phạm vi runtime §13. AC và ACC là hai miền riêng; timed race, nhiều stop, mưa, thay driver/refuel rule phức tạp cần profile và bộ đánh giá riêng. Chỉ tạo candidate hợp lệ ở thời điểm quyết định, với feature đúng thứ tự/schema C++ hiện có và chỉ dùng thông tin đã biết trước quyết định.
3. **Tạo nhãn ranking có giá trị chiến thuật:** một query = một snapshot trước quyết định, một row = một vòng pit hợp lệ. Lập cost của từng candidate bằng replay/simulator offline hiệu chỉnh từ *race độc lập* (pace trước/sau pit, fuel, thời gian qua pit/box, traffic tối thiểu); lưu scenario, phiên bản mô phỏng và cost chỉ ở cột nhãn. Vòng pit quan sát được hoặc nhãn `PitNextLap` chỉ dùng để mô tả hành vi, không được coi là optimum. Nếu mô phỏng chưa kiểm tra được với các pit thật ở nhiều vòng khác nhau, dừng ở baseline luật/fuel, chưa xuất model để app dùng.
4. **Train bản nhỏ:** dùng notebook `training/pit_strategy/train_pit_ranker.ipynb`, `rank:pairwise`, CPU `hist`, một luồng; nhãn relevance tính từ chênh lệch cost so với candidate tốt nhất. Chia train/validation/test theo *race/session và thời gian*, giữ mọi snapshot và scenario sinh từ cùng race trong cùng split. Khóa test trước khi chọn feature/hyperparameter. F1/GT3/WEC thực chỉ thử transfer riêng và giữ lại khi test sim cùng profile tốt hơn.
5. **Gate offline và thực địa:** so với earliest legal, latest legal, midpoint, fuel-threshold và chính sách pit đã quan sát. Báo NDCG@1, tỷ lệ chọn candidate đồng tối ưu, regret trung bình/p90 và số giây race hoàn thành, bootstrap theo race (không theo candidate). Mục tiêu đề xuất trước khi xem test: giảm ít nhất 10% regret trung bình so với baseline mạnh nhất, CI 95% của cải thiện không cắt 0, p90 không xấu hơn, 100% lựa chọn hợp luật. Nếu không đủ race độc lập cho CI đáng tin, chỉ báo kết quả thăm dò. Sau gate offline, chạy shadow mode rồi các race đối chứng ghép điều kiện để xem thời gian hoàn thành thực có tốt hơn; không suy lợi ích thực từ simulated regret.
6. **Gate vận hành:** Python/C++ parity, missing feature, pit đã đóng, pit service, disconnect và radio không lặp; đo p95 thời gian feature+inference, calls/race, CPU và game frametime trên máy chạy game. Chỉ promote artifact khi không có lỗi hợp luật/unsafe callout và không gây suy giảm frametime đo được. Nếu model không hơn baseline, không phát hành model và tiếp tục thu dữ liệu; không dùng F1 pilot hiện tại (0,210 s regret so với 0,201 s earliest legal) làm model phát hành.

**Điểm quyết định tiếp theo:** có được race log ACC/AC đầy đủ, quyền sử dụng, pit service và counterfactual đã hiệu chỉnh hay không. Số dòng lap lớn không thay cho số race độc lập có pit và nhãn cost đáng tin.

## 15. Mô hình hao mòn và nhiệt độ lốp từ log RaceEngineer (26/09/2026)

Mục tiêu: nút Train riêng trong Analysis & Strategy tạo **hai XGBoost regressor CPU** nghiên cứu: thay đổi tyre wear và thay đổi nhiệt độ lõi lốp ở lần lấy mẫu qua vạch tiếp theo. Fuel hiện có là feature; mức tiêu thụ/laps remaining vẫn tính bằng công thức từ telemetry, không train thêm mô hình nhiên liệu. Hai regressor không tự chọn vòng pit, không được nạp vào Ranker đang bị khóa.

1. **Process:** tái sử dụng `local_training.py` và bộ lọc vòng sạch hiện có. Snapshot v3 giữ wear/nhiệt độ/fuel tại vòng hiện tại và vòng sau cho bốn bánh; chỉ lấy số hữu hạn, `realism_confirmed=true`, hai vòng liên tiếp cùng phiên/xe/track, bỏ vòng đầu, pit/caution và vòng kề pit. Không bù giá trị thiếu hay suy từ tên trường ACC. Báo độ phủ, phiên nào wear không đổi hoặc nhảy bất thường. Nhiệt độ hiện chỉ là mẫu qua vạch, không quảng bá là trung bình cả vòng.
2. **Train offline:** `--action train-tyres` train riêng hai target thay đổi một vòng, theo simulator/category; bánh là một feature của cùng model để giới hạn số artifact. Chia train/validation theo *session_id* nguyên phiên, yêu cầu ít nhất 3 phiên và 60 cặp vòng có nhãn mỗi target/nhóm; so MAE với baseline giữ nguyên giá trị hiện tại. ACC wear chỉ train khi log thật cho thấy tín hiệu biến thiên hợp lệ; thiếu dữ liệu thì ghi `insufficient_data`/`signal_unverified` và không tạo model giả. XGBoost CPU một luồng, phiên bản/feature order/metrics/hash/nguồn snapshot nằm trong run tự lưu; `deployment_ready=false`.
3. **App:** thêm nút `Train model lốp` gọi cùng worker Python, chỉ chạy khi ngắt simulator; giữ Process, Train pace, Cancel và status sẵn có. Thông báo kết quả chỉ nói mô hình nghiên cứu, không gợi ý pit. Log local và chia sẻ hiện tại giữ nguyên.
4. **Chốt:** build Release và rà diff. Chưa có log AC/ACC đủ điều kiện thì chỉ giao pipeline và báo thiếu dữ liệu; không khẳng định hiệu quả model hay bật quyết định pit. Mốc tiếp theo là thu stint thật, đối chiếu wear ACC và kiểm tra held-out MAE trước khi cân nhắc dùng dự báo cho chiến thuật.

**Trạng thái 26/09/2026:** Process schema v3, hai nhánh train lốp, UI và build Release đã triển khai. Chưa tìm thấy log RaceEngineer đủ điều kiện trên máy; chưa có model lốp được train/duyệt. Thu ít nhất 3 phiên và 60 cặp vòng sạch có tín hiệu tương ứng cho mỗi simulator/category rồi chạy Process → Train model lốp; giữ các artifact nghiên cứu ngoài runtime.

## 16. Tính vòng pit hợp lệ và phương án có thời gian dự kiến tốt nhất (26/09/2026)

Quyết định mới cho bước tiếp theo, thay mục tiêu **Ranker chọn trực tiếp** ở §13–14. Giữ nguyên core Ranker hiện có dưới cổng duyệt model; chưa thay runtime trong bước viết plan. Ranker chỉ quay lại khi có nhãn so sánh các vòng pit đáng tin và chứng minh hơn bộ tính trực tiếp. Không dùng LLM/LAYA để quyết định pit.

### 16.1. Phạm vi và điều kiện bắt đầu

- V1 chỉ xét race AC/ACC **tính vòng, khô, còn đúng một stop bắt buộc**, cùng track/layout, xe, luật và dịch vụ pit đã xác nhận. Quyết định là `pit cuối vòng k`; `currentLap` là vòng đang chạy (1-based). Không suy từ tên xe rằng có mandatory stop hoặc cho phép refuel/thay lốp.
- Tận dụng `StrategyPredictor::candidates()` và `stillLegal()`, `RaceHistory`, recorder, worker một luồng và state machine radio trong `Application`. Tách phần kiểm tra hợp lệ dùng chung khi triển khai để danh sách ứng viên và kiểm tra trước phát radio không lệch nhau. Không thêm engine mô phỏng cả cuộc đua hay chạy Python khi đua.
- Nếu thiếu profile, không xác nhận được dịch vụ pit, nguồn fuel/pace bất thường, race đổi điều kiện hoặc dữ liệu vượt phạm vi, chỉ hiển thị lý do và cửa sổ luật nếu biết; **không chọn vòng hoặc tự gọi pit**. Các model pace/lốp tự train hiện là artifact nghiên cứu, không tự động trở thành tham số chiến thuật.

### 16.2. Ứng viên hợp lệ trước khi so thời gian

1. Liệt kê các vòng nguyên `k` từ vòng hiện tại đến vòng cuối, giao với cửa sổ pit đã xác nhận của profile. Bỏ vòng đã qua điểm báo an toàn, vòng pit đóng, vòng không đủ nhiên liệu đi tới pit với reserve, hoặc vòng không còn đủ thời gian hoàn tất dịch vụ theo luật. V1 không có action `NO_STOP` hay nhiều stop.
2. Với từng `k`, kiểm tra cả **hai chặng**: nhiên liệu hiện tại tới pit; nhiên liệu được phép nạp/sẵn có sau pit đủ về đích với reserve và không vượt dung tích bình. Xác nhận loại dịch vụ, thời gian tối thiểu, mandatory stop và điều kiện hợp lệ từ profile/telemetry đáng tin; thiếu một điều kiện bắt buộc thì loại ứng viên, không coi giá trị thiếu là 0.
3. Profile mẫu hiện chỉ có `pit_loss_s`, cửa sổ và reserve; mở rộng đúng các trường luật/dịch vụ còn thiếu sau audit log và game setup. `pit_enter`/`pit_box_enter` chỉ chứng minh xe đi qua pit/box, không chứng minh thay lốp hoặc nạp nhiên liệu. Pit xong hoặc không xác minh được dịch vụ thì hủy khuyến nghị one-stop như hiện tại.

### 16.3. Ước tính kết quả cho từng ứng viên

- Offline dùng các **vòng sạch trước và sau một stop hoàn chỉnh** để hiệu chỉnh riêng theo simulator + track/layout + xe/nhóm xe + luật. Bắt đầu bằng median/trend pace theo tuổi stint và fuel, mức tiêu thụ nhiên liệu/vòng, pit loss đo từ pit-in/out và box; chỉ dùng XGBoost hồi quy nếu thắng baseline đơn giản trên race giữ lại. Tyre wear/temperature chỉ đưa vào khi tín hiệu AC/ACC đã xác minh; không bịa dữ liệu cho ACC.
- Tại đầu vòng `n`, tính cùng một snapshot cho mọi `k` hợp lệ: `T(k) = tổng lap dự kiến từ n..k trên stint hiện tại + pit loss + tổng lap dự kiến từ k+1..N trên stint mới`. Pit loss là **phần thời gian tăng thêm so với vòng sạch**, gồm ảnh hưởng pit entry/exit và dịch vụ theo định nghĩa profile; không cộng lại lap pit nếu phần đó đã nằm trong pit loss. Giữ fuel load và tuổi lốp theo hai stint, không dùng telemetry tương lai làm feature.
- Nếu chỉ có pit loss hằng số mà không đo được khác biệt pace trước/sau pit, các ứng viên chưa phân biệt được về hiệu quả; không gọi một vòng là “tối ưu”. Traffic, safety car, thời tiết và đối thủ sau pit chưa có dự báo đáng tin trong `RaceState` thì không tự cộng một penalty tùy ý; V1 ghi rõ những rủi ro chưa tính và ngừng tự gọi khi điều kiện ngoài profile.
- Ước lượng sai số từ residual trên **race độc lập** và dùng cùng các kịch bản pace/pit loss khi so các `k`; lưu `expected_remaining_s`, dải sai số, số mẫu và phiên bản hiệu chỉnh. Đây là ước tính có điều kiện, không phải thời gian đích chắc chắn hoặc phần trăm confidence của Ranker.

### 16.4. Chọn vòng, ngưỡng gọi và CPU

- Trong số ứng viên hợp lệ, chọn `argmin T(k)`; nếu chênh lệch với phương án khác nhỏ hơn sai số đã hiệu chỉnh, hiển thị cửa sổ ứng viên tương đương thay vì khẳng định một vòng thắng rõ. Ở vòng `n`, chỉ phát “Vào pit cuối vòng này” khi `k=n` và `min(T(k>n)) - T(n)` vượt ngưỡng cải thiện chốt trên validation, hoặc `n` là vòng hợp lệ cuối cùng (nói rõ đây là giới hạn luật/fuel). Điều kiện này chỉ hoạt động sau cổng phát hành và phải kiểm tra lại luật/fuel ngay trước khi phát; nếu phương án tốt nhất ở vòng sau thì chỉ giữ kế hoạch, không gọi pit sớm.
- Giữ recommendation cũ nếu kết quả mới chưa vượt ngưỡng đổi kế hoạch đã hiệu chỉnh; đổi ngay khi kế hoạch cũ hết hợp lệ. Dùng revision/session/freshness và hủy thông báo radio cũ như luồng hiện tại. Báo chuẩn bị từ vòng trước; “Vào pit cuối vòng này” chỉ phát ở đầu vòng đủ sớm, không phát lệnh gấp khi quá muộn.
- Tính lại khi sang vòng hoặc sự kiện làm thay đổi hợp lệ/chi phí (fuel bất thường, pit window, service, cờ); gộp trigger và giới hạn một job đang chạy. Dùng C++ worker CPU một luồng, không predict mỗi frame/giây. Đo thời gian tính gồm feature, liệt kê ứng viên và dispatch, cùng CPU/game frametime khi bật/tắt.

### 16.5. Dữ liệu, đánh giá và cổng phát hành

1. **Audit và hiệu chỉnh:** tái dùng `audit_sim_logs.py` và recorder; xác nhận đủ pit-in/out, box-in/out, fuel trước/sau, dịch vụ thật, vòng sạch, setup realistic. Tách train/validation/test theo **race/session** và thời gian. Nhiều vòng trong cùng một race không phải nhiều race độc lập. Thiếu các stop ở nhiều thời điểm thì tiếp tục thu log, không tạo nhãn tối ưu giả.
2. **Kiểm tra phần có thể quan sát:** trên race giữ lại, so dự báo lap pace, fuel, thời gian qua pit và tổng thời gian của **phương án đã chạy thật** với số đo; báo MAE/bias, độ phủ sai số và breakdown theo profile. So với giữ nguyên pace, median ba vòng, fuel trung bình và pit loss cố định. Không dùng cost do chính bộ tính sinh ra làm bằng chứng rằng bộ tính chọn pit tốt.
3. **Kiểm tra quyết định:** replay các trạng thái trước pit để chứng minh legality, tính nhất quán, không dùng thông tin tương lai và radio không lặp. Replay không có kết quả thật của các vòng pit chưa chọn; muốn chứng minh lợi ích chiến thuật cần shadow mode rồi race đối chứng ở cùng profile với nhiều thời điểm pit khác nhau. Đặt ngưỡng cải thiện và kế hoạch phân tích **trước** khi xem tập test; báo kết quả theo race, không theo hàng candidate.
4. **Promote:** yêu cầu ít nhất 10 race độc lập giữ lại cho profile, sai số các thành phần đạt baseline, 100% phương án hợp luật, không thiếu trường bắt buộc, parity offline/C++, không suy giảm game frametime đáng kể và lợi ích so baseline có bằng chứng ở race đối chứng. Nếu chưa đạt, UI báo đang hiệu chỉnh; recorder/training vẫn hoạt động nhưng không tự gọi pit. Cổng Ranker cũ không được dùng để duyệt artifact của bộ tính mới.

### 16.6. Thứ tự triển khai

- [ ] P1: audit race log thật; chốt field profile/service và định nghĩa pit loss; bổ sung recorder chỉ cho field thiếu thực sự.
- [ ] P2: viết hiệu chỉnh offline nhỏ cho pace trước/sau stop, fuel, pit loss và residual; lưu artifact có schema, hash, nguồn race, phạm vi và số mẫu. Không phát hành model lốp nghiên cứu hiện tại như thành phần đã duyệt.
- [ ] P3: tái dùng candidate builder/worker của `StrategyPredictor` để tính `T(k)`; tách availability của planner khỏi `pit_ranker.json` mà vẫn giữ nguyên guard của Ranker. Nối vào status và radio hiện có; không thêm tab mới.
- [ ] P4: kiểm tra fixture offline (đầu/cuối vòng, cửa sổ, thiếu fuel/service, pit xong, race reset, kết quả muộn), shadow mode, đo CPU/frametime và race đối chứng trước khi mở gọi tự động.

Tiến độ 26/09/2026: Đã bổ sung field recorder/audit, script hiệu chỉnh nghiên cứu và bộ tính `T(k)` trong worker hiện có, nối status/radio và giữ cổng Ranker riêng. Chưa có race log đủ điều kiện để hiệu chỉnh/đánh giá; artifact do script tạo luôn `deployment_ready=false`. P1 cần log thật; P2 cần hiệu chỉnh uncertainty và margin từ race độc lập; P4 và duyệt tự động vẫn chờ bằng chứng thực nghiệm.
