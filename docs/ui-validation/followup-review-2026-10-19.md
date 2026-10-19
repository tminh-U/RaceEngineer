# Review bổ sung — 2026-10-19

**Cập nhật sau yêu cầu sửa:** cả 3 lỗi bên dưới đã được xử lý; kiểm chứng mới 25/25 ca edge và 3/3 nhóm regression, 0 warning. Xem [kết quả/ảnh sau sửa](followup-fix/README.md). Nội dung bên dưới là bằng chứng lịch sử trước sửa.

## Kết luận

Còn **3 lỗi cần sửa**. Audit mới chỉ kiểm tra và ghi nhận; không thay production/backend, không tạo lại gallery/ZIP, không upload. Root đối chiếu nguồn và ảnh Qt, một test agent Luna max chạy các ca bổ sung.

## Các lỗi đã xác nhận

### 1. Detail API báo sai khả năng dùng mô hình

- `qml/LlmConnectionForm.qml:48` dịch mọi chuỗi bắt đầu bằng `Connected` thành “Máy chủ đã phản hồi; mô hình khả dụng.”
- Provider thật tại `src/llm/providers/OpenAICompatibleProvider.cpp:423–424` phát `ProviderError` với detail `Connected, but model 'missing-model' is unavailable`.
- Kết quả Qt: summary ghi “Lỗi API”, detail lại báo mô hình khả dụng, làm mất nguyên nhân lỗi. Root và test agent tái hiện độc lập.
- **Có từ baseline** (`HEAD:qml/Main.qml:86`), không phải lỗi mới do vòng sửa UI vừa rồi. Cần sửa mapping trình bày, không đổi provider.
- [Ảnh Qt](followup-review/missing-model-wrong-status-880x576.png).

### 2. Lỗi startup dài vượt khỏi cửa sổ

- `qml/StartupNotice.qml:17–19` lấy chiều cao toàn bộ nội dung, không giới hạn theo viewport và không có vùng cuộn.
- Ở 880×576, payload khoảng 1.000 ký tự tạo notice `[x=476,y=-97,width=390,height=659]`; tiêu đề và phần đầu thông báo nằm ngoài cửa sổ, không cuộn tới đọc được. Cùng payload vừa ở 1280×720 và 1600×1000; chuỗi dài hơn vẫn có thể tràn.
- Root tái hiện riêng bằng chuỗi dài hơn: notice y=-337, height=899. Cần giới hạn thẻ và cuộn nội dung, giữ nút thử lại/đóng truy cập được.
- [Ảnh Qt của test](../../tests/ui/artifacts/edge-failures/long_startup_error_880x576.png); [ảnh root](followup-review/startup-long-error-880x576.png).

### 3. Shortcut cấu hình giọng nói mở sai tab

- `qml/EngineerSpotterPage.qml:446` có nút “Cấu hình giọng nói”; handler tại `qml/Main.qml:354` chỉ gọi `openPage(4)`.
- Trang AI có `sectionIndex` (`qml/AiVoicePage.qml:15`), mặc định 0: Kết nối AI. Thử cả VieNeu-TTS và Google Gemini API đều mở tab 0, thay vì Giọng cục bộ (1) / Google TTS (2).
- Các control giọng nói vẫn còn và có thể mở thủ công. Đây là sai đích điều hướng, không kết luận chức năng voice bị xóa.
- [Ảnh Qt](followup-review/voice-shortcut-destination-880x576.png).

## Các ca đã đạt và giới hạn

- Báo cáo [edge_surface_audit.json](../../tests/ui/artifacts/edge_surface_audit.json), thời điểm **2026-10-19T12:47:26Z**: **25 ca, 22 đạt / 3 lỗi, 0 QML warning / Qt log**.
- Sáu kích thước: 880×576, 1100×576, 1280×576, 1280×720, 1280×800, 1600×1000.
- Nhãn xe/track/đối thủ/event dài, speed 500, lap 999, fuel 999,9 L, gap 123,456 s, bốn giá trị bánh/phanh phân biệt; nội dung còn truy cập được, Cockpit không cuộn. Elide/scroll có chủ đích không tính là mất dữ liệu khi vẫn đọc được đầy đủ theo luồng kiểm tra.
- Binding DirectInput dài và thao tác remap; detail API khoảng 1.285 ký tự và khả năng cuộn tới endpoint; keyboard Enter/Tab/Space, PTT release/route cancel; ngắt live khi inactive/minimized và refresh dữ liệu mới khi trở lại đều đạt.
- Quan sát phụ: detail API dùng Text.AutoText nên markup như `<b>` được render thành định dạng thay vì ký tự nguyên văn; baseline cũng có hành vi này. Không tính thành hồi quy mới hoặc suy diễn lỗi bảo mật. [Ảnh](followup-review/api-detail-markup-880x576.png).
- Chưa phát hiện thêm tính năng bị xóa trong phạm vi audit mới. Không kết luận toàn bộ app không còn lỗi; không chạy lại toàn suite/gallery đã hoàn tất ở vòng trước.
- Qt 6.8.3/PySide6, offscreen/software, facade giả. Chưa kiểm chứng Windows/DPI native, Ctrl+Space qua C++ event filter, simulator, audio, provider thật, DirectInput thật, tray/file picker/qrc packaging. Không có số đo hiệu năng native mới.

Lệnh tái chạy:

```bash
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software .tooling/ui/bin/python tests/ui/edge_surface_audit.py
```
