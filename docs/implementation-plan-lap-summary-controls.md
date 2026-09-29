# Implementation plan: Tổng kết mỗi vòng và điều khiển tính năng qua LLM

Ngày lập: 29/09/2026

Trạng thái: **Đã triển khai; Release build thành công. Chưa kiểm tra trong AC/ACC thật.**

## 1. Kết quả cần đạt

- Khi bật **Tổng kết sau mỗi vòng**, kỹ sư tự thông báo ngắn về vòng vừa hoàn thành, hiển thị trên UI và đọc bằng VieNeu-TTS.
- Người dùng bật/tắt tổng kết bằng công tắc trên trang **Kỹ sư**, hoặc bằng lời nói/văn bản như “bật tổng kết mỗi vòng”, “tắt báo cáo vòng”.
- LLM hiểu lệnh bật/tắt các tính năng được hỗ trợ, ví dụ “tắt spotter”, “bật lại spotter”, “tắt tự hạ âm game”. Công tắc UI, runtime và settings phải cùng một trạng thái.

**Phương án đề xuất để duyệt:** tổng kết tự động được tạo bằng C++ từ telemetry/lịch sử vòng; LLM dùng để hiểu lệnh điều khiển. Không gọi LLM mỗi vòng. Cách này giữ chi phí thấp, chạy được khi API mất kết nối và tuân theo yêu cầu tính toán telemetry deterministic. Lời bình tự do bằng LLM ngoài các số liệu này chưa thuộc đợt triển khai.

## 2. Hiện trạng đã kiểm tra

| Thành phần | Hiện có | Cách tận dụng |
| --- | --- | --- |
| `Application::onStateUpdated()` | Nhận state, reset lịch sử theo phiên/lap rollback, cập nhật recorder, strategy và events | Nối trigger tổng kết vào luồng này, độc lập với bật/tắt pit strategy hoặc ghi log |
| `RaceHistory` | Lịch sử giới hạn 100 vòng; `LapRecord` có số vòng, thời gian và fuel đã dùng khi tính được | Dùng vòng vừa được ghi nhận, không lập một lịch sử telemetry song song |
| `qml/Main.qml` | Trang Kỹ sư có các dòng Spotter đang hiển thị “Tự động”; chưa có công tắc Spotter thật | Chuyển riêng dòng “Kích hoạt Spotter” thành công tắc, thêm dòng tổng kết |
| `SettingsManager` | Settings version 11; có setters cho ducking, strategy, PTT và recording | Thêm hai boolean; tái sử dụng setters của tính năng hiện có |
| `ToolRegistry` / `LLMManager` | Tool đọc telemetry; tối đa hai vòng tool-call; câu hỏi từ nhập text và STT | Thêm tool đọc trạng thái tính năng và tool đặt trạng thái bật/tắt |
| `MessageDispatcher` | Ưu tiên radio, ngắt lời khi có cảnh báo cao hơn, hủy theo prefix; hiện phát lại lời bị ngắt | Bổ sung nhận diện nguồn để hủy đúng Spotter/tổng kết và tránh phát lại tổng kết cũ |

Các dòng “Cập nhật nhiên liệu”, “Cảnh báo Lốp & Độ bám”, “Phân tích Delta vòng đua” hiện không có setting bật/tắt thực. Không quảng bá chúng là tính năng điều khiển được trước khi có runtime tương ứng.

## 3. Tổng kết sau mỗi vòng

### 3.1. Bật/tắt và hiển thị

- Công tắc **Tổng kết sau mỗi vòng** đặt trong card **KỸ SƯ ĐUA XE AI**.
- Mặc định **tắt**, lưu qua lần khởi động; bật thì áp dụng cho các vòng hoàn thành tiếp theo, không đọc lại lịch sử cũ.
- Subtitle phản ánh trạng thái thật: `Đã tắt`, `Chờ AC / ACC`, `Chờ hoàn thành vòng`, hoặc `Đang hoạt động`.
- Hiển thị tổng kết gần nhất cùng số vòng trên trang Kỹ sư; ghi mỗi tổng kết vào `eventLog` đã có giới hạn 100 mục.
- Tắt phải hủy tổng kết đang chờ hoặc đang đọc. Text tổng kết gần nhất vẫn có thể xem lại.
- Tổng kết không gọi `updateEngineerMessage()` và không thay thế phản hồi chat đang stream. Không đưa thông báo tự động vào lịch sử hội thoại gửi LLM.
- Contract Application dự kiến: `spotterEnabled`, `lapSummaryEnabled`, `lapSummaryStatus`, `latestLapSummary`, cùng notify signals và hai invokable `setSpotterEnabled(bool)`, `setLapSummaryEnabled(bool)`. QML chỉ gọi setter, không tự giữ boolean riêng.

### 3.2. Nhận diện vòng hoàn thành

- Dùng `currentLap` 1-based và `LapRecord` mới sau `raceHistory_.update(state)`: đang chạy vòng `n + 1` thì tổng kết vòng `n`.
- Chốt tối đa một tổng kết cho mỗi `(session, lapNumber)`. Không dùng riêng kích thước deque để phát hiện vòng mới vì lịch sử có giới hạn 100 vòng.
- Áp dụng cho Race, Practice, Qualifying, Hotlap và TimeAttack khi có dữ liệu vòng; không phụ thuộc `strategyEnabled`, model chiến thuật hay recorder.
- Chỉ nhận thời gian vòng hữu hạn và lớn hơn 0. Thiếu bản ghi hoặc telemetry chưa cung cấp thời gian vòng trước thì bỏ qua, không suy từ thời gian vòng đang chạy.
- Kết nối giữa vòng: vòng đầu chỉ quan sát một phần không dùng để đánh giá pace/fuel. Nếu không xác nhận được một vòng đã theo dõi đầy đủ, chờ vòng đầy đủ tiếp theo.
- Nếu bộ đếm nhảy nhiều vòng, không dựng lại những vòng đã bỏ lỡ và không so delta như thể các vòng liên tiếp. Chỉ tổng kết vòng cuối có dữ liệu xác thực.
- Reset trạng thái tổng kết theo nhánh `sessionChanged` đã có, kể cả disconnect, đổi simulator/track/car/session hoặc lap rollback; hủy audio thuộc phiên cũ.

### 3.3. Nội dung

Giữ lời đọc khoảng 1–2 câu; ưu tiên thời gian vòng và thay đổi pace, sau đó tối đa 1–2 thông tin hữu ích khác. Dùng `responseStyle` hiện có để chọn độ ngắn, không thêm setting độ dài riêng.

| Thông tin | Quy tắc |
| --- | --- |
| Số vòng + thời gian | Luôn có; UI dùng `M:SS.mmm`, lời đọc qua normalizer hiện có |
| Nhanh/chậm hơn vòng trước | C++ tính từ hai vòng hoàn thành liên tiếp cùng phiên; không lấy `currentDeltaSeconds` của vòng mới |
| Vị trí và gap | Dùng giá trị tại thời điểm qua vạch; không gọi đây là trung bình cả vòng |
| Nhiên liệu | Fuel đã dùng chỉ khi theo dõi đủ vòng, không refuel và dữ liệu hợp lệ; fuel còn đủ bao nhiêu vòng dùng kết luận deterministic hiện có |
| Lốp | Chỉ nhắc tình trạng nhiệt đáng chú ý khi có dữ liệu; dùng kết luận hiện có, không suy độ bám hoặc dự báo hao mòn từ trường ACC chưa xác minh |

Ví dụ UI: `Vòng 12: 1:48.320, nhanh hơn vòng trước 0.400 s. P5, nhiên liệu dự kiến còn 8.2 vòng.`

- Thiếu trường phụ thì bỏ trường đó, không biến giá trị thiếu thành 0 và không đọc một danh sách “không có dữ liệu”.
- Vòng có pit/caution quan sát được vẫn có thể báo thời gian, kèm ngữ cảnh ngắn; không dùng để kết luận pace tốt/xấu so với vòng thường. Theo dõi vài cờ trạng thái trong vòng, không lưu telemetry từng frame.
- `RaceState` hiện không có lap-validity đáng tin; không tuyên bố “vòng hợp lệ” hoặc “vòng sạch” theo luật game.
- Chưa phân tích sector trong tổng kết v1 vì chưa bảo đảm sector ở thời điểm qua vạch thuộc vòng vừa kết thúc. Không tự suy nguyên nhân mất thời gian, lỗi lái hoặc thời điểm pit tối ưu.

### 3.4. Radio và tài nguyên

- Xử lý tổng kết một lần khi có vòng mới; không chạy model, worker hoặc gọi API ở mỗi frame.
- Tổng kết dùng `EventPriority::Conversation`: thấp hơn callout engineer/strategy, Spotter và Critical; không ngắt câu trả lời người lái đang nghe.
- Chỉ giữ tối đa một tổng kết chờ đọc. Tổng kết vòng mới thay tổng kết cũ chưa đọc; sang phiên khác hoặc tắt tính năng thì bỏ.
- Nếu Spotter/Critical ngắt tổng kết, không phát lại tổng kết từ đầu. Các phản hồi hội thoại hiện có vẫn giữ hành vi phát lại như trước.
- Khi người lái đang PTT/STT/chờ phản hồi trực tiếp, vẫn lưu text nhưng không chen lời tổng kết tự động; bỏ phần audio của vòng đó.

## 4. Điều khiển tính năng bằng LLM

### 4.1. Danh sách tính năng được hỗ trợ trong đợt này

| Feature ID | Công tắc/tính năng | Hành vi |
| --- | --- | --- |
| `spotter` | Kích hoạt Spotter | Mặc định bật; chỉ điều khiển cảnh báo xe kế bên `CarLeft`, `CarRight`, `ThreeWide` |
| `lap_summary` | Tổng kết sau mỗi vòng | Mặc định tắt; theo mục 3 |
| `audio_ducking` | Tự động hạ âm game | Gọi `setAudioDuckingEnabled()` hiện có |
| `pit_strategy` | Tự động tính lại chiến thuật Pit | Gọi setter hiện có; bật cần `strategyAvailable()` như UI. Profile/telemetry chưa đủ thì báo đã bật nhưng đang chờ, không tuyên bố predictor ready |
| `ptt_keyboard` | PTT bàn phím | Gọi `setPushToTalkOptions()`, giữ nguyên trạng thái DirectInput |
| `ptt_directinput` | PTT DirectInput | Giữ nguyên PTT bàn phím; bật cần binding hợp lệ, không tự gán nút |
| `race_recording` | Bật/tắt ghi dữ liệu | Gọi `StrategyRecorder::setEnabled()` qua hook Application, cùng setter của `backend.strategyData.enabled`; signal hiện có lưu settings. Tắt không xóa log đã lưu |

- “Tắt spotter” không tắt cờ hiệu, hư hại, cảnh báo nhiên liệu/động cơ hay chiến thuật. Những thông báo đó do `EventEngine`/strategy sinh ra độc lập; một số có cùng priority Spotter nên không thể lọc chỉ theo priority.
- LLM chỉ đặt lựa chọn bật/tắt của người dùng. Thuật toán proximity, debounce, cooldown và ưu tiên cảnh báo vẫn hoàn toàn do C++ quyết định.
- Các toggle cửa sổ/khởi động Windows/GPU renderer và cấu hình API chưa thuộc danh sách điều khiển radio của đợt này. `setApiReasoning()` hiện tái tạo provider, nên không gọi giữa tool-round của chính request đó.
- Consent chia sẻ dữ liệu và xác nhận setup realistic vẫn phải thao tác trực tiếp theo yêu cầu hiện có; LLM không được tự cấp consent hoặc tự xác nhận thay người chơi.

### 4.2. Tool contract

Thêm hai tool vào registry hiện có:

```text
get_feature_settings()
set_feature_enabled(feature: enum, enabled: boolean)
```

Ví dụ lệnh `tắt spotter`:

```json
{
  "name": "set_feature_enabled",
  "arguments": { "feature": "spotter", "enabled": false }
}
```

Kết quả native trả về:

```json
{
  "available": true,
  "success": true,
  "feature": "spotter",
  "enabled": false,
  "changed": true,
  "message": "Đã tắt spotter."
}
```

- `get_feature_settings()` trả trạng thái hiện tại, khả năng bật và lý do nếu chưa khả dụng cho từng feature. Đọc trực tiếp từ Application, không lấy snapshot cũ của telemetry.
- Native kiểm tra enum, `enabled` đúng kiểu boolean, required fields và không có field thừa. Không chấp nhận tên property tùy ý, đường dẫn settings, mã QML hoặc câu lệnh hệ thống.
- Tool điều khiển hoạt động cả khi chưa kết nối simulator; không bị chặn bởi kiểm tra `state.connected` dành cho tool telemetry.
- Cùng một lệnh lặp lại là idempotent: trả thành công với `changed: false`; không ghi settings hoặc khởi động lại tác vụ không cần thiết.
- Native trả `success: false` và lý do nếu feature chưa khả dụng/arguments sai; không báo “đã bật” khi setter không áp dụng được. Tắt một feature đang bật luôn có thể thực hiện dù feature hiện không còn ready.

### 4.3. Luồng thực thi

```text
Lời nói -> STT ----+
                  +-> LLMManager -> tool-call đã kiểm tra
Nhập văn bản -----+                     |
                                        v
                             Application: setter dùng chung
                                        |
                         SettingsManager + runtime + notify QML
                                        |
                                kết quả tool + radio xác nhận
```

- Thêm hook callback nhỏ cho các tool điều khiển, được Application cấp qua LLMManager/ToolRegistry. Không tạo một service/controller mới hoặc kéo dependency QML vào registry.
- Tool được thực thi đồng bộ trên thread sở hữu Application như luồng hiện tại; không blocking-call sang telemetry/audio worker.
- Setter đọc lại trạng thái sau khi áp dụng rồi mới trả kết quả tool. Signal QML cập nhật công tắc ngay; cùng setter cũng được gọi khi bấm UI.
- Hai boolean mới: `spotter_enabled=true`, `lap_summary_enabled=false`. Nâng settings schema từ 11 lên 12, giữ toàn bộ giá trị đã lưu; file cũ thiếu khóa dùng defaults trên.
- Đổi trạng thái Spotter cần reset trạng thái debounce để khi bật lại có thể cảnh báo xe đang kề bên; tắt hủy đúng audio proximity đang chờ/đọc, không hủy damage/cờ/hội thoại.
- Kết quả lệnh và lý do từ chối đi vào `toolLog` hiện có, không tạo log riêng.

### 4.4. Hành vi của LLM và xử lý lỗi

- Prompt yêu cầu gọi tool khi người lái ra lệnh bật/tắt; không chỉ trả lời “đã tắt” bằng văn bản. Chỉ điều khiển tính năng được người lái yêu cầu trong lượt hiện tại.
- Hỏi trạng thái như “spotter đang bật không?” chỉ gọi tool đọc; “đừng tắt spotter” không được hiểu thành lệnh tắt.
- Lệnh mơ hồ như “tắt cảnh báo” phải hỏi lại tên tính năng, không tự chọn Spotter hoặc tắt tất cả cảnh báo.
- Lệnh nhiều tính năng rõ ràng, ví dụ “tắt spotter và bật tổng kết vòng”, được xử lý tuần tự qua tool có sẵn; xác nhận riêng kết quả thành công/thất bại. Giữ giới hạn hai vòng tool-call hiện tại.
- Chỉ dùng tool ghi trong request trực tiếp từ text/STT; tổng kết tự động không sinh request LLM và không có quyền gọi tool.
- Khi người dùng thay đổi UI trong lúc LLM đang chờ, không để kết quả cũ ghi đè lựa chọn mới: giữ revision settings tại đầu request và từ chối write đã lỗi thời. Các write đã chấp nhận trong cùng request cập nhật revision để lệnh nhiều tính năng vẫn chạy được.
- Request đã bị hủy/thay thế không được áp dụng tool trả về muộn. Retry không đảo trạng thái vì dùng giá trị boolean đích, không dùng hành động `toggle`.
- Xác nhận trạng thái điều khiển dùng kết quả native. Sau tool ghi thành công, lời xác nhận phải phản ánh đúng trạng thái đã áp dụng; nếu API lỗi ở lượt trả lời sau, vẫn hiển thị/đọc xác nhận native, không rollback tùy tiện.
- API mất mạng, timeout hoặc provider không hỗ trợ tool-calling: báo không thực hiện được lệnh bằng AI; công tắc UI và tổng kết tự động vẫn hoạt động. Không thêm bộ nhận diện khẩu lệnh riêng trong đợt này.

## 5. Thứ tự triển khai và file dự kiến

| Bước | Công việc | File chính | Điều kiện hoàn thành |
| --- | --- | --- | --- |
| P1 | Settings và setter chung; công tắc Spotter thật; định nghĩa rõ phạm vi proximity | `src/config/SettingsManager.{h,cpp}`, `src/app/Application.{h,cpp}`, `qml/Main.qml` | UI bật/tắt đúng runtime; lưu/khôi phục defaults và trạng thái |
| P2 | Tạo tổng kết từ vòng hoàn thành; thêm công tắc và text gần nhất | Helper nhỏ `src/race/LapSummary.{h,cpp}`, Application, QML | Mỗi vòng đúng một text; không tổng kết sai phiên/vòng thiếu dữ liệu |
| P3 | Hủy audio theo nguồn, không phát lại tổng kết bị ngắt | `src/audio/MessageDispatcher.{h,cpp}`, Application | Tắt không còn audio của feature; cảnh báo/hội thoại khác giữ nguyên |
| P4 | Tool đọc/ghi feature; hook tới setters; prompt, validation, revision và xác nhận | `src/llm/tools/ToolRegistry.{h,cpp}`, `src/llm/LLMManager.{h,cpp}`, Application | Text/STT điều khiển đúng các feature trong danh sách; UI đồng bộ |
| P5 | Kiểm tra offline và kiểm tra trực tiếp UI/radio | Tests hiện có, `CMakeLists.txt`; cập nhật `REQUIREMENTS.md` khi hoàn tất | Đạt tiêu chí mục 6; báo rõ phần cần AC/ACC thật |

`LapSummary` chỉ là helper nhỏ để kiểm tra logic vòng/nội dung ngoài UI; không thêm interface, model hay pipeline phân tích mới. Giữ `RaceHistory` và công thức đang dùng bởi strategy/tools; chỉ thay chúng nếu kiểm tra cho thấy trigger tổng kết cần sửa đúng lỗi chung. Nếu `production/config/settings.example.json` vẫn được dùng khi đóng gói, cập nhật hai khóa mới cùng schema lúc triển khai settings.

## 6. Tiêu chí nghiệm thu

1. Bật tổng kết từ UI hoặc câu “bật tổng kết mỗi vòng” cho cùng trạng thái lưu; tắt bằng một trong hai cách thì cả UI và runtime tắt ngay.
2. Qua vạch một lần tạo đúng một tổng kết vòng vừa chạy; nhiều telemetry update không lặp. Sau 100 vòng vẫn phát hiện được vòng mới.
3. Bật giữa phiên không đọc lại vòng cũ; kết nối giữa vòng, lap rollback, reconnect, nhảy counter và thiếu/NaN/Inf thời gian không phát tổng kết sai.
4. Delta được tính từ hai vòng hoàn thành phù hợp; thiếu fuel/tyre/position thì bỏ đúng trường; vòng pit/caution không dẫn đến kết luận pace sai hoặc lời khuyên pit giả.
5. Tắt tổng kết/Spotter khi audio đang đọc hoặc chờ chỉ hủy nguồn tương ứng. Spotter/Critical ngắt tổng kết; tổng kết bị ngắt không phát lại sau cảnh báo.
6. “Tắt spotter” dừng proximity, “bật lại spotter” khôi phục cảnh báo; cờ, hư hại và fuel/engine critical vẫn hoạt động. Không đánh đồng priority Spotter với nguồn proximity.
7. Lệnh rõ ràng, lệnh nhiều feature, câu phủ định, lệnh mơ hồ, sai arguments, feature lạ, gọi lặp và API lỗi được xử lý theo mục 4. Không có xác nhận thành công giả.
8. Pit strategy thiếu model và DirectInput thiếu binding trả lý do chưa bật được; strategy đã bật nhưng thiếu profile/telemetry phải báo đang chờ, không báo ready. Tắt không bị availability gate cản. LLM không đổi consent hoặc realism confirmation.
9. UI thay đổi trong lúc chờ LLM hoặc gửi request mới không bị response cũ ghi đè. Khởi động lại khôi phục settings; settings cũ giữ hành vi Spotter đang bật và summary mặc định tắt.
10. API offline: tổng kết, Spotter và UI vẫn chạy. Không tăng số API request do qua vạch, không tải model/dependency mới.

Tái sử dụng `tests/RaceStateTests.cpp` cho nội dung tổng kết, settings và contract tool; dùng callback giả/native result giả cho kiểm tra tool để không gọi mạng. Thêm một kiểm tra nhỏ với TTS giả nếu cần chứng minh cancellation/preemption của dispatcher; không khởi tạo model âm thanh trong test. Sau triển khai chạy target liên quan và `ctest --test-dir build --output-on-failure` trong môi trường build hiện có. UI kiểm tra thủ công, sau đó xác nhận trigger qua vạch trên AC/ACC thật; không coi mock là bằng chứng đã kiểm tra simulator thật.

## 7. Phạm vi duyệt

Phạm vi đã duyệt: **tổng kết deterministic mỗi vòng + công tắc UI + LLM điều khiển bảy feature ở mục 4.1**. Không triển khai lời bình LLM tự động, điều khiển mọi property UI, công tắc riêng cho từng loại cảnh báo hoặc thay đổi thuật toán chiến thuật. Không train model, thay telemetry provider, đổi STT/TTS hay chỉnh các phần UI ngoài phạm vi trên.
