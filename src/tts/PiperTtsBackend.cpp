#include "tts/PiperTtsBackend.h"

#include "utils/Logging.h"

#include <QAudioOutput>
#include <QAudioDevice>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QMediaDevices>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QUrl>
#include <QUuid>

#include <algorithm>

namespace raceengineer {

PiperTtsBackend::PiperTtsBackend(QString executablePath, QString modelPath,
    QObject* const parent)
    : ITtsBackend(parent)
    , executablePath_(std::move(executablePath))
    , modelPath_(std::move(modelPath))
    , process_(new QProcess(this))
    , player_(new QMediaPlayer(this))
    , output_(new QAudioOutput(this))
{
    loadCachedSpotter();
    player_->setAudioOutput(output_);
    output_->setVolume(0.85F);

    connect(process_, &QProcess::started, this, [this] {
        process_->write(normalizeBilingualText(pendingText_).toUtf8());
        process_->write("\n");
        process_->closeWriteChannel();
    });
    connect(process_, &QProcess::finished, this,
        [this](const int exitCode, const QProcess::ExitStatus status) {
            if (stopping_) return;
            if (status != QProcess::NormalExit || exitCode != 0
                || !QFileInfo::exists(temporaryAudioPath_)) {
                const QString detail = QString::fromUtf8(process_->readAllStandardError()).trimmed();
                emit errorOccurred(detail.isEmpty() ? QStringLiteral("Piper synthesis failed.") : detail);
                cleanTemporaryAudio();
                emit speakingFinished();
                return;
            }
            playbackActive_ = true;
            player_->setSource(QUrl::fromLocalFile(temporaryAudioPath_));
            player_->play();
        });
    connect(player_, &QMediaPlayer::mediaStatusChanged, this,
        [this](const QMediaPlayer::MediaStatus status) {
            if (status != QMediaPlayer::EndOfMedia || stopping_ || !playbackActive_) return;
            playbackActive_ = false;
            cleanTemporaryAudio();
            emit speakingFinished();
        });
    connect(player_, &QMediaPlayer::errorOccurred, this,
        [this](QMediaPlayer::Error, const QString& detail) {
            if (!stopping_) emit errorOccurred(detail);
        });
}

bool PiperTtsBackend::isAvailable() const
{
    return QFileInfo(executablePath_).isExecutable() && QFileInfo(modelPath_).isFile()
        && QFileInfo(modelPath_ + QStringLiteral(".json")).isFile();
}

void PiperTtsBackend::speak(const QString& text)
{
    stop();
    stopping_ = false;
    if (text.trimmed().isEmpty()) {
        emit speakingFinished();
        return;
    }
    pendingText_ = text.trimmed();
    if (playCachedSpotter(pendingText_)) return;
    if (!isAvailable()) {
        emit errorOccurred(QStringLiteral("Piper runtime or voice model is not installed."));
        emit speakingFinished();
        return;
    }
    temporaryAudioPath_ = QDir::temp().filePath(
        QStringLiteral("raceengineer-%1.wav").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    const double lengthScale = 1.0 / std::clamp(static_cast<double>(speed_), 0.5, 2.0);
    process_->setProgram(executablePath_);
    process_->setWorkingDirectory(QFileInfo(executablePath_).absolutePath());
    process_->setArguments({QStringLiteral("-m"), QStringLiteral("piper"),
        QStringLiteral("--model"), modelPath_, QStringLiteral("--output_file"),
        temporaryAudioPath_, QStringLiteral("--length_scale"), QString::number(lengthScale, 'f', 2)});
    process_->start();
    qCInfo(logTts) << "Piper 1.8 synthesis started";
    emit statusChanged(QStringLiteral("Đang tổng hợp · Piper · VAIS1000"));
    emit speakingStarted(pendingText_);
}

void PiperTtsBackend::stop()
{
    stopping_ = true;
    if (process_->state() != QProcess::NotRunning) {
        process_->kill();
        process_->waitForFinished(500);
    }
    player_->stop();
    playbackActive_ = false;
    player_->setSource({});
    cleanTemporaryAudio();
    stopping_ = false;
}

QString PiperTtsBackend::normalizeBilingualText(QString text)
{
    const auto replace = [&text](const QString& pattern, const QString& replacement) {
        text.replace(QRegularExpression(QStringLiteral("\\b(?:%1)\\b").arg(pattern),
            QRegularExpression::CaseInsensitiveOption), replacement);
    };
    replace(QStringLiteral("virtual\\s+safety\\s+car"), QStringLiteral("xe an toàn ảo"));
    replace(QStringLiteral("safety\\s+car"), QStringLiteral("xe an toàn"));
    replace(QStringLiteral("full\\s+course\\s+yellow"), QStringLiteral("cờ vàng toàn đường đua"));
    replace(QStringLiteral("box\\s+this\\s+lap"), QStringLiteral("vào pít vòng này"));
    replace(QStringLiteral("pit\\s+lane"), QStringLiteral("làn pít"));
    replace(QStringLiteral("pit\\s+stop"), QStringLiteral("lần dừng pít"));
    replace(QStringLiteral("lift\\s+and\\s+coast"), QStringLiteral("nhả ga và thả trôi"));
    replace(QStringLiteral("undercut"), QStringLiteral("pít sớm"));
    replace(QStringLiteral("overcut"), QStringLiteral("pít muộn"));
    replace(QStringLiteral("tyres?|tires?"), QStringLiteral("lốp"));
    replace(QStringLiteral("pace"), QStringLiteral("nhịp chạy"));
    replace(QStringLiteral("delta"), QStringLiteral("chênh lệch"));
    replace(QStringLiteral("stint"), QStringLiteral("lượt chạy"));
    replace(QStringLiteral("push"), QStringLiteral("tăng tốc"));
    replace(QStringLiteral("box"), QStringLiteral("vào pít"));
    replace(QStringLiteral("DRS"), QStringLiteral("đê e-rờ ét"));
    replace(QStringLiteral("ERS"), QStringLiteral("i e-rờ ét"));
    replace(QStringLiteral("ABS"), QStringLiteral("a bê ét"));
    replace(QStringLiteral("TC"), QStringLiteral("tê xê"));
    replace(QStringLiteral("F1"), QStringLiteral("ép một"));
    text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return text.trimmed();
}

void PiperTtsBackend::loadCachedSpotter()
{
    cachedSpotterDirectory_ = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("audio/spotter"));
    QFile manifest(QDir(cachedSpotterDirectory_).filePath(QStringLiteral("manifest.json")));
    if (!manifest.open(QIODevice::ReadOnly)) return;
    const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll());
    if (!document.isObject()) return;
    for (const QJsonValue& value : document.object().value(QStringLiteral("entries")).toArray()) {
        const QJsonObject entry = value.toObject();
        const QString text = entry.value(QStringLiteral("text")).toString().trimmed();
        QStringList files;
        for (const QJsonValue& file : entry.value(QStringLiteral("files")).toArray()) {
            const QString relativePath = file.toString();
            if (QFileInfo(QDir(cachedSpotterDirectory_).filePath(relativePath)).isFile()) files.append(relativePath);
        }
        if (!text.isEmpty() && !files.isEmpty()) cachedSpotterFiles_.insert(text, files);
    }
}

bool PiperTtsBackend::playCachedSpotter(const QString& text)
{
    const auto iterator = cachedSpotterFiles_.constFind(text.trimmed());
    if (iterator == cachedSpotterFiles_.cend() || iterator->isEmpty()) return false;
    const QStringList& variants = iterator.value();
    int index = QRandomGenerator::global()->bounded(variants.size());
    const int previous = lastCachedSpotterVariant_.value(text, -1);
    if (variants.size() > 1 && index == previous) index = (index + 1) % variants.size();
    lastCachedSpotterVariant_.insert(text, index);
    playbackActive_ = true;
    emit speakingStarted(text);
    emit statusChanged(QStringLiteral("Đang phát spotter Piper đã lưu"));
    player_->setSource(QUrl::fromLocalFile(
        QDir(cachedSpotterDirectory_).filePath(variants.at(index))));
    player_->play();
    return true;
}

void PiperTtsBackend::setVolume(const float volume)
{
    output_->setVolume(std::clamp(volume, 0.0F, 1.0F));
}

void PiperTtsBackend::setAudioOutputDevice(const QString& description)
{
    for (const QAudioDevice& device : QMediaDevices::audioOutputs()) {
        if (device.description() == description) {
            output_->setDevice(device);
            return;
        }
    }
    output_->setDevice(QMediaDevices::defaultAudioOutput());
}

void PiperTtsBackend::setSpeed(const float speed)
{
    speed_ = std::clamp(speed, 0.5F, 2.0F);
}

void PiperTtsBackend::cleanTemporaryAudio()
{
    if (!temporaryAudioPath_.isEmpty()) {
        const QString path = temporaryAudioPath_;
        temporaryAudioPath_.clear();
        player_->setSource({});
        QFile::remove(path);
    }
}

} // namespace raceengineer
