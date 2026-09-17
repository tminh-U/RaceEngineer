#include "tts/PiperTtsBackend.h"

#include "utils/Logging.h"

#include <QAudioOutput>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QProcess>
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
    player_->setAudioOutput(output_);
    output_->setVolume(0.85F);

    connect(process_, &QProcess::started, this, [this] {
        process_->write(pendingText_.toUtf8());
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
            player_->setSource(QUrl::fromLocalFile(temporaryAudioPath_));
            player_->play();
        });
    connect(player_, &QMediaPlayer::playbackStateChanged, this,
        [this](const QMediaPlayer::PlaybackState state) {
            if (state != QMediaPlayer::StoppedState || stopping_ || temporaryAudioPath_.isEmpty()) return;
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
    if (!isAvailable()) {
        emit errorOccurred(QStringLiteral("Piper runtime or voice model is not installed."));
        emit speakingFinished();
        return;
    }
    pendingText_ = text.trimmed();
    temporaryAudioPath_ = QDir::temp().filePath(
        QStringLiteral("raceengineer-%1.wav").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    const double lengthScale = 1.0 / std::clamp(static_cast<double>(speed_), 0.5, 2.0);
    process_->setProgram(executablePath_);
    process_->setWorkingDirectory(QFileInfo(executablePath_).absolutePath());
    process_->setArguments({QStringLiteral("--model"), modelPath_, QStringLiteral("--output_file"),
        temporaryAudioPath_, QStringLiteral("--length_scale"), QString::number(lengthScale, 'f', 2)});
    process_->start();
    qCInfo(logTts) << "Piper synthesis started";
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
    player_->setSource({});
    cleanTemporaryAudio();
    stopping_ = false;
}

void PiperTtsBackend::setVolume(const float volume)
{
    output_->setVolume(std::clamp(volume, 0.0F, 1.0F));
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
