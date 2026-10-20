#include "integration/SimulatorSetup.h"
#include "telemetry/common/WindowsProcess.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QStringConverter>
#include <QTemporaryFile>
#include <QUuid>
#include <QEventLoop>
#include <QTimer>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace raceengineer {
namespace {

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

bool isAc(const QString& path)
{
    return !path.isEmpty() && (QFileInfo(QDir(path).filePath("acs.exe")).isFile()
        || QFileInfo(QDir(path).filePath("acs_x86.exe")).isFile());
}

QStringList steamLibraries()
{
    QStringList roots;
#ifdef _WIN32
    QSettings steam(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat);
    roots << steam.value("SteamPath").toString();
    QSettings machine(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam"), QSettings::NativeFormat);
    roots << machine.value("InstallPath").toString();
    const QString programFiles = qEnvironmentVariable("ProgramFiles(x86)");
    if (!programFiles.isEmpty()) roots << QDir(programFiles).filePath("Steam");
#endif
    roots.removeAll(QString{});
    QStringList libraries = roots;
    const QRegularExpression paths(QStringLiteral("\"(?:path|[0-9]+)\"\\s+\"([^\"]+)\""));
    for (const auto& root : roots) {
        auto matches = paths.globalMatch(QString::fromUtf8(readFile(QDir(root).filePath("steamapps/libraryfolders.vdf"))));
        while (matches.hasNext()) {
            QString path = matches.next().captured(1).replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
            if (QDir::isAbsolutePath(path)) libraries << QDir::fromNativeSeparators(path);
        }
    }
    libraries.removeDuplicates();
    return libraries;
}

QString steamGame(const QString& appId)
{
    const QRegularExpression installDir(QStringLiteral("\"installdir\"\\s+\"([^\"]+)\""));
    for (const auto& library : steamLibraries()) {
        const auto match = installDir.match(QString::fromUtf8(readFile(
            QDir(library).filePath("steamapps/appmanifest_" + appId + ".acf"))));
        if (match.hasMatch()) {
            const QString path = QDir(library).filePath("steamapps/common/" + match.captured(1));
            if (QDir(path).exists()) return QDir(path).canonicalPath();
        }
    }
    return {};
}

bool writeWithBackup(const QString& path, const QByteArray& data, QString& error, bool* accessFailure = nullptr)
{
    const QFileInfo info(path);
    if (info.isSymLink()) { error = "Không ghi đè liên kết: " + path; return false; }
    if (info.exists()) {
        QFile original(path);
        if (!original.open(QIODevice::ReadOnly)) { if (accessFailure) *accessFailure = true; error = original.errorString() + ": " + path; return false; }
        if (original.readAll() == data) return true;
        original.close();
        const QString backup = path + ".raceengineer-backup-"
            + QDateTime::currentDateTimeUtc().toString("yyyyMMddTHHmmsszzz")
            + "-" + QUuid::createUuid().toString(QUuid::Id128).left(8);
        if (!QFile::copy(path, backup)) { if (accessFailure) *accessFailure = true; error = "Không tạo được bản sao lưu: " + path; return false; }
    }
    if (!QDir().mkpath(info.absolutePath())) { if (accessFailure) *accessFailure = true; error = "Không tạo được thư mục: " + info.absolutePath(); return false; }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        error = file.errorString() + ": " + path;
        if (accessFailure) *accessFailure = true;
        return false;
    }
    return true;
}

bool copyCompanion(const QString& source, const QString& target, QString& error, bool& accessFailure)
{
    if (source.isEmpty() || !isAc(target)) { error = "Chưa có đường dẫn AC hoặc companion đi kèm."; return false; }
    const QString root = QDir(target).canonicalPath();
    if (root.isEmpty()) { error = "Không xác định được đường dẫn AC thực tế."; return false; }
    for (const QString& relative : {QString{}, QStringLiteral("apps"), QStringLiteral("apps/python"),
             QStringLiteral("apps/python/RaceEngineer"), QStringLiteral("apps/python/RaceEngineer/RaceEngineer.py"),
             QStringLiteral("apps/python/RaceEngineer/manifest.ini"), QStringLiteral("apps/python/RaceEngineer/README.md")}) {
        const QString path = relative.isEmpty() ? root : QDir(root).filePath(relative);
        bool linked = QFileInfo(path).isSymLink();
#ifdef _WIN32
        const auto native = QDir::toNativeSeparators(path).toStdWString();
        const DWORD attributes = GetFileAttributesW(native.c_str());
        linked = linked || (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT));
#endif
        if (linked) { error = "Không chép companion qua symlink/junction: " + path; return false; }
    }
    for (const QString& name : {QStringLiteral("RaceEngineer.py"), QStringLiteral("manifest.ini"), QStringLiteral("README.md")}) {
        const QByteArray data = readFile(QDir(source).filePath(name));
        if (data.isEmpty()) { error = "Không đọc được companion: " + name; return false; }
        if (!writeWithBackup(QDir(root).filePath("apps/python/RaceEngineer/" + name), data, error, &accessFailure)) return false;
    }
    return true;
}

bool elevatedCopy(const QString& path, QString& error)
{
#ifdef _WIN32
    const std::wstring executable = QCoreApplication::applicationFilePath().toStdWString();
    QString directory = QDir::toNativeSeparators(path);
    if (directory.endsWith('\\')) directory += '\\';
    const std::wstring arguments = ("--install-ac-files \"" + directory + "\"").toStdWString();
    SHELLEXECUTEINFOW launch{};
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    launch.lpVerb = L"runas";
    launch.lpFile = executable.c_str();
    launch.lpParameters = arguments.c_str();
    launch.nShow = SW_HIDE;
    if (!ShellExecuteExW(&launch) || !launch.hProcess) {
        error = "Chưa cấp quyền chép companion vào thư mục AC. Có thể thử lại hoặc cài qua Content Manager.";
        return false;
    }
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (WaitForSingleObject(launch.hProcess, 0) != WAIT_TIMEOUT) loop.quit();
    });
    poll.start(100);
    loop.exec();
    DWORD code = STILL_ACTIVE;
    const bool finished = GetExitCodeProcess(launch.hProcess, &code) && code == 0;
    CloseHandle(launch.hProcess);
    if (!finished) error = "Bước chép companion có quyền admin chưa hoàn tất. Kiểm tra quyền ghi và thử lại.";
    return finished;
#else
    Q_UNUSED(path);
    error = "Thiết lập game chỉ hỗ trợ Windows.";
    return false;
#endif
}

QString pythonConfigPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath("Assetto Corsa/cfg/python.ini");
}

QString decodeJson(const QByteArray& bytes)
{
    if (bytes.startsWith("\xff\xfe") || (bytes.size() > 1 && bytes[1] == '\0')) {
        QStringDecoder decoder(QStringDecoder::Utf16LE);
        return decoder(bytes.startsWith("\xff\xfe") ? bytes.mid(2) : bytes);
    }
    return QString::fromUtf8(bytes);
}

int broadcastPort(const QJsonObject& config)
{
    return config.value("updListenerPort").toInt(config.value("udpListenerPort").toInt());
}

bool validBroadcast(const QJsonObject& config)
{
    const int port = broadcastPort(config);
    return port > 0 && port <= 65535 && config.value("connectionPassword").isString()
        && config.value("commandPassword").isString();
}

} // namespace

SimulatorSetup::SimulatorSetup(QObject* parent) : QObject(parent)
{
    refresh();
}

QString SimulatorSetup::companionPath() const
{
    const QDir app(QCoreApplication::applicationDirPath());
    for (const QString& relative : {QStringLiteral("../extras/AssettoCorsa/apps/python/RaceEngineer"),
             QStringLiteral("../apps/python/RaceEngineer")}) {
        const QString path = app.filePath(relative);
        if (QFileInfo(QDir(path).filePath("RaceEngineer.py")).isFile()) return QDir(path).canonicalPath();
    }
    return {};
}

void SimulatorSetup::refresh()
{
    QSettings settings("RaceEngineer", "SimulatorSetup");
    acPath_ = settings.value("acPath").toString();
    if (!isAc(acPath_)) acPath_ = steamGame("244210");
    if (!isAc(acPath_)) acPath_.clear();
    const QString source = companionPath();
    bool installed = !source.isEmpty() && !acPath_.isEmpty();
    for (const QString& name : {QStringLiteral("RaceEngineer.py"), QStringLiteral("manifest.ini")}) {
        const QByteArray bytes = readFile(QDir(source).filePath(name));
        installed = installed && !bytes.isEmpty()
            && bytes == readFile(QDir(acPath_).filePath("apps/python/RaceEngineer/" + name));
    }
    QSettings python(pythonConfigPath(), QSettings::IniFormat);
    const bool enabled = python.value("__FEATURES/ENABLED").toInt() == 1
        && python.value("RaceEngineer/ACTIVE").toInt() == 1 && python.status() == QSettings::NoError;
    const QString acStatus = acPath_.isEmpty() ? "Chưa tìm thấy AC. Chọn thư mục chứa acs.exe."
        : source.isEmpty() ? "Thiếu companion đi kèm RaceEngineer."
        : !installed ? "Companion chưa cài hoặc khác bản đi kèm."
        : !enabled ? "Đã có companion; Python app chưa được bật."
        : "Đã cài đúng bản và bật Python app. Chờ kiểm chứng dữ liệu khi vào track.";
    accPath_ = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
        .filePath("Assetto Corsa Competizione/Config/broadcasting.json");
    const bool accDetected = QDir(QFileInfo(accPath_).absolutePath()).exists() || !steamGame("805550").isEmpty();
    const auto config = QJsonDocument::fromJson(decodeJson(readFile(accPath_)).toUtf8());
    const bool accReady = config.isObject() && validBroadcast(config.object());
    const auto processState = inspectRunningSimulator();
    status_ = {{"ac", QVariantMap{{"path", acPath_}, {"installed", installed}, {"enabled", enabled},
                   {"ready", installed && enabled}, {"status", acStatus}}},
        {"acc", QVariantMap{{"path", accPath_}, {"detected", accDetected}, {"ready", accReady},
                    {"status", accReady ? QString("Đã cấu hình UDP cổng %1. Chờ ACC kết nối.").arg(broadcastPort(config.object()))
                        : accDetected ? QString("Chưa cấu hình UDP hợp lệ; có thể thiết lập khi đã thoát game.")
                                      : QString("Chưa tìm thấy ACC. Hãy cài hoặc mở game một lần.")}}},
        {"processCheckAvailable", processState.has_value()},
        {"running", processState && *processState != RunningSimulator::None}};
    emit changed();
}

bool SimulatorSetup::mayConfigure()
{
    if (busy_) { message_ = "Đang cài companion; chờ tác vụ hoàn tất."; emit changed(); return false; }
#ifndef _WIN32
    message_ = "Thiết lập game chỉ hỗ trợ Windows.";
    emit changed();
    return false;
#else
    const auto processState = inspectRunningSimulator();
    if (!processState) {
        message_ = "Không kiểm tra được tiến trình game; chưa thay đổi cấu hình.";
        refresh();
        return false;
    }
    if (*processState != RunningSimulator::None) {
        message_ = "Thoát AC / ACC trước khi thay cấu hình game.";
        refresh();
        return false;
    }
    return true;
#endif
}

void SimulatorSetup::chooseAcFolder()
{
    if (busy_) return;
    const QString path = QFileDialog::getExistingDirectory(nullptr, "Chọn thư mục Assetto Corsa chứa acs.exe", acPath_);
    if (path.isEmpty()) return;
    if (!isAc(path)) { message_ = "Thư mục đã chọn không có acs.exe hoặc acs_x86.exe."; emit changed(); return; }
    QSettings settings("RaceEngineer", "SimulatorSetup");
    settings.setValue("acPath", QDir(path).canonicalPath());
    settings.sync();
    message_ = settings.status() == QSettings::NoError ? "Đã chọn đường dẫn AC." : "Không lưu được đường dẫn AC.";
    refresh();
}

bool SimulatorSetup::installAc()
{
    refresh();
    if (!mayConfigure()) return false;
    busy_ = true;
    emit changed();
    QString error;
    const QString source = companionPath();
    bool accessFailure = false;
    if (!copyCompanion(source, acPath_, error, accessFailure) && accessFailure) {
        message_ = "Cần quyền ghi thư mục AC; chỉ bước chép companion sẽ yêu cầu quyền admin.";
        emit changed();
        if (elevatedCopy(acPath_, error)) error.clear();
    }
    const auto processState = inspectRunningSimulator();
    if (error.isEmpty() && (!processState || *processState != RunningSimulator::None))
        error = "Game vừa khởi động hoặc chưa kiểm tra được tiến trình; thử lại để bật Python app.";
    if (error.isEmpty()) {
        QTemporaryFile temporary;
        if (!temporary.open()) error = "Không tạo được cấu hình Python tạm.";
        else {
            const QString path = temporary.fileName();
            temporary.close();
            const QString target = pythonConfigPath();
            QFile original(target);
            if (original.exists() && !original.open(QIODevice::ReadOnly)) error = "Không đọc được " + target;
            else {
                QFile staging(path);
                const QByteArray originalBytes = original.isOpen() ? original.readAll() : QByteArray{};
                if ((original.isOpen() && original.error() != QFileDevice::NoError)
                    || !staging.open(QIODevice::WriteOnly) || staging.write(originalBytes) != originalBytes.size())
                    error = "Không chuẩn bị được cấu hình Python.";
                staging.close();
                if (error.isEmpty()) {
                    QSettings python(path, QSettings::IniFormat);
                    python.allKeys();
                    if (python.status() != QSettings::NoError) error = "python.ini không hợp lệ; giữ nguyên file cũ.";
                    else {
                        python.setValue("__FEATURES/ENABLED", 1);
                        python.setValue("RaceEngineer/ACTIVE", 1);
                        python.sync();
                        if (python.status() != QSettings::NoError) error = "Không ghi được cấu hình Python tạm.";
                    }
                }
                original.close();
                if (error.isEmpty()) writeWithBackup(target, readFile(path), error);
            }
        }
    }
    busy_ = false;
    message_ = error.isEmpty() ? "Đã cài và bật AC companion. Khởi động lại AC / Content Manager để nạp app."
        : "Cài AC chưa hoàn tất: " + error;
    refresh();
    return error.isEmpty();
}

bool SimulatorSetup::installAcFiles(const QString& path)
{
    if (!mayConfigure()) return false;
    bool accessFailure = false;
    QString error;
    const bool success = copyCompanion(companionPath(), path, error, accessFailure);
    message_ = success ? "Đã chép companion AC." : error;
    emit changed();
    return success;
}

bool SimulatorSetup::configureAcc()
{
    refresh();
    if (!mayConfigure()) return false;
    if (!status_.value("acc").toMap().value("detected").toBool()) {
        message_ = "Chưa tìm thấy ACC. Hãy cài hoặc mở game một lần rồi kiểm tra lại.";
        emit changed();
        return false;
    }
    QFile file(accPath_);
    QByteArray original;
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly)) { message_ = "Không đọc được " + accPath_; emit changed(); return false; }
        original = file.readAll();
        file.close();
    }
    const auto document = QJsonDocument::fromJson(decodeJson(original).toUtf8());
    if (!original.isEmpty() && !document.isObject()) {
        message_ = "broadcasting.json không hợp lệ; giữ nguyên file. Sửa JSON rồi thử lại.";
        emit changed();
        return false;
    }
    QJsonObject config = document.object();
    int port = broadcastPort(config);
    if (port <= 0 || port > 65535) port = 9000;
    config.insert("updListenerPort", port); // ACC's official key contains this spelling.
    if (config.contains("udpListenerPort")) config.insert("udpListenerPort", port);
    if (!config.contains("connectionPassword")) config.insert("connectionPassword", QUuid::createUuid().toString(QUuid::Id128));
    if (!config.contains("commandPassword")) config.insert("commandPassword", QString{});
    if (!validBroadcast(config)) { message_ = "Kiểu dữ liệu password ACC không hợp lệ; giữ nguyên file."; emit changed(); return false; }
    QByteArray data = QJsonDocument(config).toJson();
    if (original.isEmpty() || original.startsWith("\xff\xfe") || (original.size() > 1 && original[1] == '\0')) {
        QStringEncoder encoder(QStringEncoder::Utf16LE);
        const QByteArray encoded = encoder(QString::fromUtf8(data));
        data = QByteArray("\xff\xfe", 2) + encoded;
    }
    QString error;
    writeWithBackup(accPath_, data, error);
    message_ = error.isEmpty() ? "Đã thiết lập ACC UDP. Khởi động lại ACC để game đọc cấu hình."
        : "Thiết lập ACC chưa hoàn tất: " + error;
    refresh();
    return error.isEmpty();
}

bool SimulatorSetup::configureDetectedGames()
{
    refresh();
    if (!mayConfigure()) return false;
    bool success = true;
    QStringList results;
    if (!acPath_.isEmpty()) { success = installAc() && success; results << message_; }
    if (status_.value("acc").toMap().value("detected").toBool()) { success = configureAcc() && success; results << message_; }
    message_ = results.isEmpty() ? "Chưa phát hiện game. Mở Hệ thống & thiết bị để chọn đường dẫn và thiết lập sau." : results.join('\n');
    emit changed();
    return success;
}

} // namespace raceengineer
