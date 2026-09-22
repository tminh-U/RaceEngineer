<div align="center">

[![Contributors][contributors-shield]][contributors-url] [![Forks][forks-shield]][forks-url] [![Stars][stars-shield]][stars-url] [![Issues][issues-shield]][issues-url] [![MIT License][license-shield]][license-url]

</div>


<a id="readme-top"></a>

<div align="center">
  <a href="https://github.com/tminh-U/RaceEngineer">
    <img src="assets/final_icon_256.png" alt="RaceEngineer logo" width="160">
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

## Thông tin về Project

RaceEngineer là một phần mềm kỹ sư đua xe chạy local trên máy, xử lý dữ liệu telemetry từ game, tính toán logic đua xe và đầu vào / đầu ra bằng giọng nói.

#### Tính năng
- Đọc và phân tích telemetry từ Assetto Corsa và Assetto Corsa Competizone.
- Dự đoán lượng xăng, thời gian vòng lap, khoảng cách giữa các xe,...
- Giao tiếp thời gian thực với người lái bằng tiếng Việt sử dụng `Phowhisper` và `VieNeu-TTS`.
- Các spotter quan trọng vẫn có thể sử dụng kể cả khi không có LLM hay mạng.
- Cài đặt trực quan, bật tắt các tính năng khi sử dụng.
- Nhiều giọng để lựa chọn và file dùng để train giọng tùy biến riêng cho người sử dụng.
- Tương thích với LLM dạng OpenAI.

<!-- TODO: Add a screenshot or a short demo GIF here. -->

<!--
<p align="center">
  <img src="path/to/screenshot.png" alt="RaceEngineer screenshot">
</p>
-->


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
- Assetto Corsa Competizone



## Bắt đầu

### Yêu cầu

- Windows x64
- Visual Studio 2022 with an x64 MSVC toolchain
- CMake 3.24 or newer
- Ninja
- Qt 6.5 or newer
- Assetto Corsa and/or Assetto Corsa Competizione
- Runtime models and voice assets
- Vulkan SDK (optional; recommended for GPU acceleration)

### Cài đặt

1. Tải `RaceEngineer-X.X.X-Setup.exe` từ [Release](https://github.com/tminh-U/RaceEngineer/releases).
2. Chạy `RaceEngineer-X.X.X-Setup.exe`.
3. Khởi động app `RaceEngineer` và bắt đầu sử dụng.


### AC Python App

Đây là addon tùy chọn để lấy thêm dữ liệu từ Assetto Corsa. Installer đã kèm sẵn addon tại:

```text
<RaceEngineer install>\extras\AssettoCorsa\apps\python\RaceEngineer
```

Copy thư mục `RaceEngineer` vào thư mục `apps\python` của Assetto Corsa, hoặc kéo nó vào Content Manager.


### ACC Broadcasting

Để sử dụng được thêm các dữ liệu từ vị trí đổi thủ hoặc bảng xếp hạng, hãy config ACC broadcasting listener ở vị trí :

```text
Documents\Assetto Corsa Competizione\Config\broadcasting.json
```

Sau đó set với một cổng không được app nào sử dụng (ví dụ):
```json
{
  "udpListenerPort": 9000
}
```

## Tài liệu

- [Kiến trúc ứng dụng](architect.md) — các thành phần và luồng dữ liệu trong app.
- [Hướng dẫn build](build.md) — hướng dẫn build bản development và release.
- [Hướng dẫn train giọng](training.md) — train giọng VieNeu-TTS tuỳ chỉnh.
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

Le Dinh Tue Minh — [@tminh-U](https://github.com/tminh-U)

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
