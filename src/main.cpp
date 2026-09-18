#include "app/Application.h"
#include "config/CredentialStore.h"
#include "utils/Logging.h"

#include <QGuiApplication>
#include <QCommandLineParser>
#include <QTimer>
#include <iostream>

#ifdef _WIN32
#include <windows.h>

BOOL WINAPI consoleHandler(DWORD signal)
{
    if (signal == CTRL_C_EVENT || signal == CTRL_BREAK_EVENT || signal == CTRL_CLOSE_EVENT) {
        std::cout << "\n[Race Engineer] Đang dừng ứng dụng...\n";
        QGuiApplication::quit();
        return TRUE;
    }
    return FALSE;
}
#endif

int main(int argc, char* argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleCtrlHandler(consoleHandler, TRUE);
#endif

    QGuiApplication::setApplicationName(QStringLiteral("Race Engineer"));
    QGuiApplication::setOrganizationName(QStringLiteral("RaceEngineer"));
    QGuiApplication::setApplicationVersion(QStringLiteral("0.9.0"));
    qSetMessagePattern(QStringLiteral("[%{time hh:mm:ss.zzz}] [%{category}] %{message}"));

    QGuiApplication qtApplication(argc, argv);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Race Engineer - High-performance native console companion for AC/ACC"));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption mockOption(QStringLiteral("mock"),
        QStringLiteral("Bat mock telemetry de kiem thu khong can mo game."));
    parser.addOption(mockOption);

    const QCommandLineOption mapOption(QStringLiteral("map-button"),
        QStringLiteral("Vao che do gan nut PTT tren vo lang (DirectInput)."));
    parser.addOption(mapOption);

    const QCommandLineOption setKeyOption(QStringLiteral("set-key"),
        QStringLiteral("Luu API key vao Windows Credential Manager."),
        QStringLiteral("api_key"));
    parser.addOption(setKeyOption);

    const QCommandLineOption testLlmOption(QStringLiteral("test-llm"),
        QStringLiteral("Kiem tra ket noi toi LLM server roi thoat."));
    parser.addOption(testLlmOption);

    parser.process(qtApplication);

    // Lưu API key từ dòng lệnh
    if (parser.isSet(setKeyOption)) {
        const QString key = parser.value(setKeyOption).trimmed();
        if (raceengineer::CredentialStore::writeApiKey(key)) {
            std::cout << "[Config] Đã lưu API key vào Windows Credential Manager thành công.\n";
            return 0;
        }
        std::cerr << "[Lỗi] Không thể ghi API key vào Windows Credential Manager.\n";
        return 1;
    }

    raceengineer::Application application(parser.isSet(mockOption));
    application.startDirectInput(0);

    // Kiểm tra kết nối LLM
    if (parser.isSet(testLlmOption)) {
        std::cout << "[LLM] Đang kiểm tra kết nối tới " << application.apiBaseUrl().toStdString() << " ...\n";
        QObject::connect(&application, &raceengineer::Application::apiStateChanged, [&application, &qtApplication] {
            std::cout << "[LLM] Kết quả: " << application.apiState().toStdString()
                      << " - " << application.apiDetail().toStdString() << "\n";
            qtApplication.quit();
        });
        application.testApiConnection();
        return qtApplication.exec();
    }

    // Chế độ gán nút vô lăng
    if (parser.isSet(mapOption)) {
        std::cout << "================================================================\n";
        std::cout << "         GÁN NÚT PUSH-TO-TALK (DIRECTINPUT)\n";
        std::cout << "================================================================\n";
        std::cout << "Vui lòng bấm 1 nút trên vô lăng (ví dụ: nút Radio trên Moza) để gán PTT...\n";
        std::cout << "(Nhấn Ctrl+C nếu muốn hủy)\n\n";

        QObject::connect(&application, &raceengineer::Application::pttSettingsChanged, [&application, &qtApplication] {
            std::cout << "\n-> Đã gán thành công: " << application.directInputBinding().toStdString() << "\n";
            std::cout << "Cấu hình đã được lưu vào settings.json.\n";
            qtApplication.quit();
        });
        application.beginDirectInputMapping();
        return qtApplication.exec();
    }

    // Khởi chạy bình thường (Console companion)
    std::cout << "================================================================\n"
              << "               RACE ENGINEER - CONSOLE COMPANION\n"
              << "================================================================\n"
              << " [Sim]  Trạng thái: " << application.connectionText().toStdString() << "\n"
              << " [PTT]  Nút bấm:    " << application.directInputBinding().toStdString()
              << (application.directInputPttEnabled() ? " (DirectInput Bật)" : " (DirectInput Tắt)") << "\n"
              << " [Mic]  Thu âm:     " << application.microphoneName().toStdString() << "\n"
              << " [TTS]  Giọng đọc:  " << application.ttsBackend().toStdString()
              << " (" << application.ttsStatus().toStdString() << ")\n"
              << " [LLM]  AI Server:  " << application.apiBaseUrl().toStdString()
              << " (" << application.apiModel().toStdString() << ")\n"
              << "        API Key:    " << (application.apiKey().isEmpty() ? "Chưa có (tùy chọn cho local endpoint)" : "Đã lưu") << "\n"
              << "================================================================\n"
              << "Hướng dẫn:\n"
              << "  * Giữ nút PTT trên vô lăng để nói chuyện với kỹ sư.\n"
              << "  * Nhả nút PTT để gửi giọng nói cho PhoWhisper nhận dạng.\n"
              << "  * Cảnh báo cờ vàng, hết xăng, an toàn tự động phát qua loa/tai nghe.\n"
              << "  * Nhấn Ctrl+C để thoát ứng dụng.\n"
              << "================================================================\n" << std::endl;

    // Lắng nghe sự kiện
    QObject::connect(&application, &raceengineer::Application::connectionChanged, [&application] {
        if (application.connected()) {
            std::cout << "[SIM] Đã kết nối: " << application.simulatorName().toStdString() << std::endl;
        } else {
            std::cout << "[SIM] Đã ngắt kết nối (Đang chờ AC / ACC...)" << std::endl;
        }
    });

    QObject::connect(&application, &raceengineer::Application::voiceStatusChanged, [&application] {
        const QString status = application.voiceStatus();
        if (status == QStringLiteral("Listening")) {
            std::cout << "[PTT] Đang ghi âm giọng nói (Giữ nút vô lăng)..." << std::endl;
        } else if (status == QStringLiteral("Recognizing")) {
            std::cout << "[PTT] PhoWhisper đang nhận diện giọng nói..." << std::endl;
        } else if (status == QStringLiteral("Thinking")) {
            std::cout << "[AI] Đang suy luận phản hồi..." << std::endl;
        }
    });

    QString lastUserText;
    QString lastEngineerText;
    QObject::connect(&application, &raceengineer::Application::interactionChanged, [&application, &lastUserText, &lastEngineerText] {
        const QString user = application.latestUserText();
        const QString engineer = application.latestEngineerText();
        if (!user.isEmpty() && user != lastUserText) {
            lastUserText = user;
            std::cout << "\n[BẠN]:   \"" << user.toStdString() << "\"" << std::endl;
        }
        if (!engineer.isEmpty() && engineer != lastEngineerText) {
            lastEngineerText = engineer;
            std::cout << "[KỸ SƯ]: \"" << engineer.toStdString() << "\"\n" << std::endl;
        }
    });

    QObject::connect(&application, &raceengineer::Application::latestEventChanged, [&application] {
        const QString event = application.latestEvent();
        if (!event.isEmpty()) {
            std::cout << "[SPOTTER]: " << event.toStdString() << std::endl;
        }
    });

    QObject::connect(&application, &raceengineer::Application::apiStateChanged, [&application] {
        if (application.apiState() != QStringLiteral("Connected") && !application.apiDetail().isEmpty()) {
            std::cout << "[AI Status]: " << application.apiState().toStdString()
                      << " - " << application.apiDetail().toStdString() << std::endl;
        }
    });

    return qtApplication.exec();
}
