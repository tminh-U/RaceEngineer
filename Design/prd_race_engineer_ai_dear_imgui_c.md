# Product Requirements Document (PRD) — Race Engineer AI (Dear ImGui C++ Edition)

---

## 1. Tổng quan Dự án (Executive Summary)
**Tên dự án:** Race Engineer AI (Kỹ sư Đua xe AI)  
**Nền tảng mục tiêu:** Windows 11 Desktop (Kiến trúc công nghệ: Pure C++20, Rendering Backend: **Dear ImGui** tích hợp DirectX 11/12 / Vulkan, GLFW/Win32 Windowing).  
**Lý do chọn Dear ImGui thay vì Qt 6 / QML:**
- **Zero Overhead & Siêu nhẹ:** Dear ImGui chạy ở chế độ Immediate Mode GUI với chi phí RAM cực thấp (< 50MB so với ~300MB+ của Qt), CPU < 0.5%, không gây sụt khung hình (drop FPS) cho game đua xe.
- **Dễ dàng biến thành In-game Overlay:** Backend DirectX/DirectInput cho phép ứng dụng vừa chạy ở chế độ Desktop Window vừa có thể dễ dàng hook hoặc render transparent overlay đè trực tiếp lên tựa game đang chơi (Assetto Corsa, iRacing).
- **Native DirectInput & WASAPI:** Tích hợp trực tiếp 100% C++ không qua binding lớp QML hay Qt event loop.

**Đối tượng sử dụng:** Sim Racers (Game thủ & vận động viên đua xe mô phỏng trên các tựa game như Assetto Corsa Competizione, Assetto Corsa, iRacing).  
**Mục tiêu sản phẩm:** Cung cấp một kỹ sư trưởng AI tương tác bằng giọng nói thời gian thực hai chiều, có khả năng đọc hiểu dữ liệu đo từ xa (telemetry) của xe, tư vấn chiến thuật lốp, nhiên liệu, phân tích đối thủ và cảnh báo buồng lái thông qua nút Push-to-Talk (PTT) gắn trực tiếp trên vô lăng DirectInput.

---

## 2. Ngôn ngữ Thiết kế & ImGui Styling (Design Language & ImGui Style)
- **Phong cách giao diện:** Dark Theme kỹ thuật cao cấp, lấy cảm hứng từ Apple macOS & Fluent kết hợp phong cách cơ học tối giản của Dear ImGui (`ImGuiStyle`).
- **Bảng màu ImGui (`ImVec4`):**
  - Background Window: `#131315` (`ImVec4(0.075f, 0.075f, 0.082f, 0.95f)`)
  - Child Bg / Panels: `#1b1b1d` (`ImVec4(0.106f, 0.106f, 0.114f, 1.0f)`)
  - Accent Active: `#0a84ff` (`ImVec4(0.039f, 0.518f, 1.0f, 1.0f)`)
  - Success / Ready: `#30d158` (`ImVec4(0.188f, 0.820f, 0.345f, 1.0f)`)
  - Warning / Danger: `#ff453a` (`ImVec4(1.0f, 0.271f, 0.227f, 1.0f)`)
- **ImGui Style Variables:**
  - `ImGuiStyle::WindowRounding = 8.0f`
  - `ImGuiStyle::FrameRounding = 6.0f`
  - `ImGuiStyle::PopupRounding = 6.0f`
  - `ImGuiStyle::ItemSpacing = ImVec2(8.0f, 6.0f)`
  - `ImGuiStyle::FramePadding = ImVec2(10.0f, 6.0f)`
- **Font:** San Francisco / Segoe UI Fluent được nạp qua `ImGuiIO::Fonts->AddFontFromFileTTF(...)` kèm icon glyph font (FontAwesome hoặc Material Icons TTF).
- **Ngôn ngữ hiển thị:** Tiếng Việt UTF-8 (`ImFontConfig::GlyphRangesVietnamese`).

---

## 3. Kiến trúc Thông tin & Điều hướng (Information Architecture)
Ứng dụng gồm **5 tab / view chính** hiển thị qua thanh điều hướng Sidebar (`ImGui::BeginChild("Sidebar", ...)`):

1. **Bảng điều khiển (Dashboard)**:
   - *Quick Race Bar:* Vị trí (P), Vòng (Lap), Nhiên liệu (Fuel), Trạng thái kết nối ACC.
   - *Voice & Chat Log (ImGui Scrolling Log):* Khung log đàm thoại hai chiều dạng immediate-mode, tự động cuộn xuống cuối (`SetScrollHereY(1.0f)`), badge micro PTT, bộ lọc khử nhiễu RNNoise.
   - *Quick Settings & Macros:* Cụm nút bấm lệnh thoại 1 chạm (`ImGui::Button`), công tắc chế độ Ngắn gọn/Chi tiết, Audio Ducking (-12dB), thanh trượt âm lượng Radio.
2. **Kỹ sư (Engineer)**:
   - Tên gọi xưng hô của tay đua (Driver Callout Name) nhập qua `ImGui::InputText`.
   - Phong cách phản hồi của AI (Combo/Radio buttons).
   - Danh sách checkbox/toggle cảnh báo chủ động (Traffic, Lốp, Nhiên liệu, Cờ).
3. **Telemetry**:
   - Giám sát 4 bánh xe: Áp suất, nhiệt độ vỏ & lõi, độ mòn.
   - Tính toán lượng xăng chiến thuật và số vòng chạy được (L/lap, Pit window).
   - Biểu đồ thời gian thực sử dụng **ImPlot** (thư viện đồ thị trực quan cực nhanh cho ImGui).
4. **Trí tuệ nhân tạo (AI Configuration)**:
   - Cấu hình OpenAI Cloud API vs Local LLM (Ollama / llama.cpp C++ API).
   - Nhập API Key, Endpoint Base URL (`ImGui::InputTextWithHint`, chế độ ẩn password).
   - Tham số: Model (`gpt-4o-mini`), Temperature Slider (`0.0f - 1.0f`), Max Output Tokens, Timeout.
   - Nút Test Latency trả kết quả ping HTTP ms thời gian thực.
5. **Cài đặt (Settings)**:
   - *DirectInput C++ Backend:* Quét thiết bị (`IDirectInput8::EnumDevices`), gán phím PTT, Mute, Pit Request (Hỗ trợ Momentary & Toggle).
   - *WASAPI Audio:* Chọn audio capture/render endpoint ID, tự động giảm volume game (Session Audio Ducking).
   - *Tích hợp Game:* Quản lý kết nối Shared Memory / UDP (Assetto Corsa Competizione).
   - *About & Build Info:* C++20, Dear ImGui v1.9x, DirectX 11/12, Open Source repo link.

---

## 4. Pipeline Kỹ thuật C++ (Technical Implementation)
- **DirectInput Hooking:** Luồng riêng biệt polling vô lăng ở tần số 100Hz qua `IDirectInputDevice8::GetDeviceState`.
- **Speech Pipeline:** 
  - Input: PortAudio / MiniAudio ghi âm âm thanh 16kHz mono.
  - STT: Whisper C++ (`whisper.cpp`) chạy local cực nhanh hoặc gửi multipart POST đến OpenAI Audio Transcription API qua `libcurl`.
  - LLM: `libcurl` streaming SSE tới OpenAI API hoặc local llama.cpp backend.
  - TTS: Piper TTS C++ hoặc OpenAI TTS, phát lại qua WASAPI Audio buffer.
- **Telemetry Reader:** Map bộ nhớ dùng chung `OpenFileMappingW(FILE_MAP_READ, FALSE, L"Local\\acpmf_physics")` đọc struct telemetry của game ở 60 FPS.

---

## 5. Ưu điểm Kiến trúc Dear ImGui
- Dung lượng bộ nhớ RAM chạy nền: **~35MB - 50MB** (nhẹ hơn 85% so với Qt).
- CPU idle: **< 0.3%**.
- Tốc độ khởi động (Cold Start): **< 100ms**.
- Khả năng xuất bản thành single portable `.exe` không cần mang theo hàng chục DLL nặng hàng trăm MB như Qt 6.