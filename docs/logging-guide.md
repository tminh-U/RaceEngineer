# Thiết lập phiên đua để ghi log cho RaceEngineer

RaceEngineer lưu một dòng dữ liệu cho mỗi vòng hoàn thành và mỗi sự kiện vào/ra pit trong **phiên Race** của Assetto Corsa (AC) hoặc Assetto Corsa Competizione (ACC). Log gốc luôn ở trên máy. Chỉ phiên được người chơi xác nhận cài đặt thực tế trước khi đua mới được dùng khi bấm **Xử lý dữ liệu / Train model pace** hoặc tự gửi nếu đã bật chia sẻ.

## Trước khi vào phiên Race

1. Mở RaceEngineer và bật **Record** trong **Analysis & Strategy → Dữ liệu & huấn luyện**.
2. Thiết lập nhiên liệu và độ mòn lốp theo luật đua thực tế. Với AC, chọn **Fuel rate 100%** và **Tyre wear rate 100%** nếu bạn muốn log ở tốc độ hao mòn chuẩn. Với AC server tự quản, kiểm tra `FUEL_RATE=100` và `TYRE_WEAR_RATE=100` trong `server/cfg/server_cfg.ini`. Tránh mức `0` hoặc hệ số tăng tốc hao mòn khi định gộp dữ liệu với các phiên chuẩn.
3. Với ACC, dùng thiết lập Race bình thường của giải/host. Không dùng mod, chế độ hoặc quy tắc làm lốp luôn ở nhiệt độ tối ưu hay vô hiệu hóa hao lốp/nhiên liệu. Nếu không biết cấu hình server, **đừng xác nhận** phiên đó.
4. Ghi lại ngoài app các điều kiện khó suy ra từ telemetry: độ dài cuộc đua, luật bắt buộc pit/thay lốp, thời tiết, thời gian trong game, track grip, bộ lốp, hệ số hao mòn khác chuẩn và thay đổi setup giữa các stint. Giữ cùng một bộ quy tắc khi so sánh các phiên.
5. Trong app, bật **Xác nhận cài đặt đua thực tế** *trước khi phiên Race bắt đầu*. Công tắc khóa trong lúc đua và tự tắt sau phiên; cần xác nhận lại cho phiên tiếp theo. Đây là xác nhận của người chơi, app chưa đọc được trực tiếp các hệ số cài đặt của game.

## Trong và sau cuộc đua

- Đua các vòng sạch ở tốc độ bình thường; giữ RaceEngineer mở. Để có dữ liệu pit hữu ích, nên ghi trọn stint trước pit, sự kiện vào pit, phục vụ, ra pit và stint tiếp theo. Chỉ một vài vòng hoặc chỉ hotlap không đủ để đánh giá quyết định pit.
- Có thể kiểm tra nhiên liệu giảm và nhiệt độ lốp thay đổi qua các vòng. Log lưu nhiệt độ và độ mòn lốp theo từng vòng **nếu simulator cung cấp giá trị hữu hạn**. Riêng tín hiệu độ mòn ACC vẫn cần đối chiếu với stint thực tế trước khi dùng làm nhãn huấn luyện.
- Sau Race, chờ app ghi xong rồi mới đóng. Bấm **Mở thư mục dữ liệu** để xem `pit_strategy_laps.jsonl` và thư mục `recordings/`; **Xử lý dữ liệu đã ghi** để lọc các vòng hợp lệ, rồi mới **Train model pace**. Hai nút xử lý/train chạy khi ngắt kết nối simulator để dành CPU cho đua.
- Nếu quên xác nhận hoặc dùng cài đặt không thực tế, cứ để công tắc tắt: log gốc vẫn lưu để kiểm tra, nhưng không vào dữ liệu train hoặc gói tự gửi. Bản log cũ thiếu cờ xác nhận cũng không được đưa vào train.

Model pace hiện học thời gian vòng kế tiếp. Log đua quan sát được chưa tự cho biết **vòng pit tối ưu**: mô hình chọn pit cần nhãn chiến thuật hoặc phương pháp đánh giá các phương án không diễn ra trong cuộc đua. Đừng dùng kết quả pace như chỉ thị pit đã được kiểm chứng.

Tham khảo: [Assetto Corsa](https://assettocorsa.gg/assetto-corsa/) và [Assetto Corsa Competizione](https://assettocorsa.gg/assetto-corsa-competizione/) của Kunos; [mẫu cấu hình AC server](https://github.com/GameServerManagers/Game-Server-Configs/blob/main/ac/server_cfg.ini) có hai khóa hệ số nhiên liệu/lốp nói trên.
