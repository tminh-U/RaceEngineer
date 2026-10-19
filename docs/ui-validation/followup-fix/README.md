# Sửa 3 lỗi sau audit — 2026-10-19

## Kết quả hiện tại

- **25/25 ca edge đạt**, báo cáo `2026-10-19T14:04:22Z`: [JSON](edge-audit/edge_surface_audit.json).
- **3/3 nhóm regression đạt**, báo cáo `2026-10-19T14:04:05Z`: [JSON](focused-fix-regressions.json).
- Không QML warning/Qt log. Root xác nhận hash nguồn của cả hai báo cáo khớp file hiện tại và đã xem ảnh Qt sau sửa. `git diff --check` sạch, `git diff --name-only -- src` rỗng.
- Root sửa production; một test agent Luna max viết/chạy kiểm tra. Không thêm/xóa tính năng, đổi backend/settings/cadence, commit/push/upload. Gallery/ZIP cũ không tạo lại trong lượt sửa này; ảnh mới nằm tại đây.

## Thay đổi và bằng chứng

| Lỗi | Sửa | Kiểm chứng |
| --- | --- | --- |
| Detail API unavailable bị báo thành khả dụng | `LlmConnectionForm.qml` chỉ dịch các chuỗi thành công chuẩn khi state Connected; các nguyên nhân lỗi giữ nguyên | 3 chuỗi thành công và 2 lỗi bắt đầu Connected; [ảnh lỗi đúng](api-unavailable-model-1280x800.png), [ảnh thành công](api-connected-success-1280x800.png) |
| Startup error dài tràn cửa sổ | `StartupNotice.qml` giới hạn chiều cao theo viewport; nội dung lỗi trong ScrollView, tiêu đề/nút ngoài vùng cuộn | 880×576 đọc được đầu/cuối, title/retry/close đứng yên; click retry đúng một lần, close dismiss; short/loading gọn. [Ảnh đầu](startup-error-long-880x576.png), [ảnh cuối do root kiểm tra](root/startup-bottom-independent.png) |
| Shortcut voice mở tab kết nối AI | `Main.qml` chọn tab voice theo backend ngay sau mở workspace | Cả VieNeu/Google, lần đầu/lần đã load và giữ draft endpoint chưa lưu. [VieNeu](voice-shortcut-first-1-VieNeu-TTS.png), [Google](voice-shortcut-first-2-Google-Gemini-API.png) |

Đã chạy lại 25 ca của audit trước ở sáu kích thước; test startup được cập nhật để xác minh nội dung cuộn và controls cố định. Kết quả 22/25 trước sửa và ảnh lỗi gốc tại `tests/ui/artifacts/edge_surface_audit.json` được giữ nguyên. Hai báo cáo mới là kết quả hiện tại.

## Tái chạy

Tại thư mục repo:

```bash
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software .tooling/ui/bin/python tests/ui/focused_fix_regressions.py
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software .tooling/ui/bin/python - <<'PY'
import sys
from pathlib import Path
sys.path.insert(0, str(Path('tests/ui').resolve()))
import edge_surface_audit as audit
audit.OUT = Path('docs/ui-validation/followup-fix/edge-audit').resolve()
audit.SCREENSHOTS = audit.OUT / 'edge-failures'
print(audit.run())
PY
```

## Giới hạn

Qt 6.8.3/PySide6 offscreen/software và facade giả. Chưa xác minh Windows/DPI native, simulator/audio/provider/DirectInput thật, Ctrl+Space qua C++ event filter hay packaging qrc. Không chạy lại toàn suite/gallery, không có số đo hiệu năng native mới.
