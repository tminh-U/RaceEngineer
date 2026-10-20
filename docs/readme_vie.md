<div align="center">

[![Contributors][contributors-shield]][contributors-url] [![Forks][forks-shield]][forks-url] [![Stars][stars-shield]][stars-url] [![Issues][issues-shield]][issues-url] [![MIT License][license-shield]][license-url]

</div>


<a id="readme-top"></a>

<div align="center">
  <a href="https://github.com/tminh-U/RaceEngineer">
    <img src="../assets/final_icon_256.png" alt="RaceEngineer monochrome logo" width="160">
  </a>

  <h1 align="center">RaceEngineer</h1>

  <p align="center">
    Một kỹ sư đường đua cho đua xe giả lập (Sim Racing)
    <br>
    <a href="https://github.com/tminh-U/RaceEngineer/issues">Report lỗi</a>
    ·
    <a href="https://github.com/tminh-U/RaceEngineer/issues">Yêu cầu tính năng mới</a>
  </p>
</div>



<!-- TODO: Replace the text, links, screenshots, and roadmap items below with your final project details. -->

<details>
  <summary>Mục lục</summary>
  <ol>
    <li><a href="#Thông tin về Project">Thông tin về Project</a></li>
    <li><a href="#Bắt đầu">Bắt đầu</a></li>
    <li><a href="#Tài liệu">Tài liệu</a></li>
    <li><a href="#Đóng góp">Đóng góp</a></li>
    <li><a href="#Liên hệ">Liên hệ</a></li>
  </ol>
</details>

## NOTE : Phần mềm chỉ hỗ trợ chạy AI trên CPU và AMD GPU (Không hỗ trợ Nvidia GPU trong tương lai)

## Thông tin về Project

RaceEngineer là một phần mềm kỹ sư đua xe chạy local trên máy, xử lý dữ liệu telemetry từ game, tính toán logic đua xe và đầu vào / đầu ra bằng giọng nói.

#### Tính năng
- Đọc và phân tích telemetry từ Assetto Corsa và Assetto Corsa Competizione.
- Dự đoán lượng xăng, thời gian vòng lap, khoảng cách giữa các xe,...
- Giao tiếp thời gian thực với người lái bằng tiếng Việt sử dụng `Phowhisper` và `VieNeu-TTS`.
- Các spotter quan trọng vẫn có thể sử dụng kể cả khi không có LLM hay mạng.
- Cài đặt trực quan, bật tắt các tính năng khi sử dụng.
- Nhiều giọng để lựa chọn và file dùng để train giọng tùy biến riêng cho người sử dụng.
- Tương thích với LLM dạng OpenAI.

<p align="center">
  <img src="../images/Screenshot.png" alt="Giao diện RaceEngineer Cockpit với dữ liệu minh họa">
</p>


## Được build với

- C++20
- CMake
- Qt 6 / Qt Quick
- MSVC Windows x64
- whisper.cpp và PhoWhisper-small Q5_1
- Native VieNeu-TTS v3 Turbo
- Vulkan acceleration

## Game hỗ trợ
- Assetto Corsa
- Assetto Corsa Competizione



## Bắt đầu

### Yêu cầu chạy app

- Windows x64, CPU hỗ trợ AVX2 và driver/runtime Vulkan hoạt động, kể cả khi chọn tính toán CPU.
- Assetto Corsa hoặc Assetto Corsa Competizione.
- Microphone nếu dùng giọng nói; provider/model/API key và mạng nếu hỏi AI qua API.

Installer kèm model/giọng local. Visual Studio, CMake và Qt SDK chỉ cần khi [build từ source](build.md).

### Quickstart

1. Thoát AC, ACC và RaceEngineer; tải và chạy installer từ [Releases](https://github.com/tminh-U/RaceEngineer/releases/latest).
2. Installer tự tìm thư viện Steam, cài/bật Python companion của AC và cấu hình UDP cho ACC được phát hiện. Giữ cổng/password hợp lệ và cấu hình khác; sao lưu file trước khi thay đổi.
3. Mở app, hoàn tất thiết lập giọng/AI; chọn micro và loa ở **Thiết lập → Hệ thống & thiết bị**. Kết nối LLM và cấu hình TTS là hai phần riêng.
4. Vào **Hệ thống & thiết bị → Kết nối game · AC / ACC → Kiểm tra lại**. AC chưa được tìm thấy thì chọn thư mục chứa `acs.exe`, bấm **Cài / cập nhật companion**. Với ACC, bấm **Thiết lập ACC UDP** khi game đã thoát; mở game một lần nếu chưa có thư mục người dùng.
5. Khởi động lại game/Content Manager sau thiết lập, vào track và kiểm tra Cockpit có kết nối/dữ liệu. File đúng chưa chứng minh telemetry đang hoạt động. AC Spotter cần companion hiện tại; UDP ACC chưa cung cấp hình học bánh xe cho proximity Spotter.
6. Giữ **Ctrl+Space** để nói, thả để gửi; hoặc gán nút vô lăng DirectInput.
7. Mở **Hệ thống & thiết bị → Phiên bản & cập nhật** để kiểm tra bản stable và bản tái phát hành cùng số phiên bản; tải installer trên GitHub và chạy khi đã thoát app/game.

Bản portable cần giải nén đầy đủ, mở `bin/RaceEngineer.exe` rồi dùng nút thiết lập trong app. Khi thiếu quyền ghi, game đang chạy hoặc JSON hỏng, xem trạng thái lỗi và sửa lại. Khóa cổng ACC chính thức là `updListenerPort`; app vẫn đọc alias `udpListenerPort` cũ và giữ nguyên cổng đang hợp lệ.

Xem [Quickstart đầy đủ và cách xử lý lỗi](quickstart.md), [ghi log/train](logging-guide.md) và [build từ source](build.md).

## Tài liệu

- [Kiến trúc ứng dụng](architect.md): tổng quan và các chương theo hệ thống.
- [Runtime và UI](architecture/runtime-ui-storage.md): vòng đời, threads, settings và đóng gói.
- [Telemetry và Spotter](architecture/telemetry-radar-spotter.md): nguồn AC/ACC và hình học radar.
- [STT và TTS](architecture/voice-stt-tts.md): nhận diện giọng nói, tổng hợp tiếng nói và radio.
- [Prompt và tools LLM](architecture/llm-prompts-tools.md): hội thoại và điều khiển tính năng.
- [XGBoost và chiến thuật](architecture/strategy-xgboost.md): điều kiện duyệt model, ghi log và train nghiên cứu.
- [Hướng dẫn build](build.md): build bản development và release.
- [Hướng dẫn train giọng](training.md): train giọng VieNeu-TTS tuỳ chỉnh.
- [Hướng dẫn ghi log](logging-guide.md): chuẩn bị phiên Race đủ điều kiện cho train local.
- [README tiếng Anh](../README.md)

## Đóng góp

Contributions are welcome.

1. Fork the project.
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Make and test your changes.
4. Commit your changes: `git commit -m "Add my feature"`
5. Push the branch and open a pull request.

Please keep simulator-specific telemetry inside its provider and preserve deterministic behavior for safety-critical spotter messages.


## Liên hệ

Le Dinh Tue Minh - [@tminh-U](https://github.com/tminh-U)

## Acknowledgments

- [VieNeu-TTS](https://github.com/pnnbao97/VieNeu-TTS)
- [Phowhisper](https://github.com/VinAIResearch/PhoWhisper)
- [whisper.cpp](https://github.com/ggerganov/whisper.cpp)
- [Qt](https://www.qt.io/)
- Assetto Corsa and Assetto Corsa Competizione shared-memory telemetry

<!-- MARKDOWN LINKS & IMAGES -->

[license-shield]: https://img.shields.io/github/license/tminh-U/RaceEngineer.svg?style=for-the-badge
[license-url]: https://github.com/tminh-U/RaceEngineer/blob/main/LICENSE
[windows-shield]: https://img.shields.io/badge/platform-Windows%20x64-0078D4?style=for-the-badge&logo=windows
[windows-url]: https://github.com/tminh-U/RaceEngineer
[contributors-shield]: https://img.shields.io/github/contributors/tminh-U/RaceEngineer.svg?style=for-the-badge
[contributors-url]: https://github.com/tminh-U/RaceEngineer/graphs/contributors
[forks-shield]: https://img.shields.io/github/forks/tminh-U/RaceEngineer.svg?style=for-the-badge
[forks-url]: https://github.com/tminh-U/RaceEngineer/network/members
[stars-shield]: https://img.shields.io/github/stars/tminh-U/RaceEngineer.svg?style=for-the-badge
[stars-url]: https://github.com/tminh-U/RaceEngineer/stargazers
[issues-shield]: https://img.shields.io/github/issues/tminh-U/RaceEngineer.svg?style=for-the-badge
[issues-url]: https://github.com/tminh-U/RaceEngineer/issues
