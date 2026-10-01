#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>

namespace raceengineer {
// Protocol/corpus changes invalidate previously measured results.
inline constexpr int ttsBenchmarkVersion = 1;
inline constexpr int ttsBenchmarkSamples = 23;
int chooseTtsThreads(const QJsonArray& results);
QString ttsBenchmarkFingerprint(const QString& modelDir, const QString& voice,
    const QString& device);

class TtsBenchmark final : public QObject {
    Q_OBJECT
public:
    explicit TtsBenchmark(QObject* parent = nullptr);
    ~TtsBenchmark() override;
    bool running() const { return running_; }
    double progress() const { return progress_; }
    QString status() const { return status_; }
    QJsonArray results() const { return results_; }
    void prepare(); // Reserve before the app queues context release.
    void start(const QString& executable, const QStringList& arguments, int timeoutMs = 120000);
    void cancel(const QString& reason = QStringLiteral("Đã hủy; giữ cấu hình trước benchmark."));
signals:
    void changed();
    void finished(int selectedThreads);
private:
    void launch();
    void readOutput();
    bool consume(const QJsonObject& event);
    void fail(const QString& reason);
    void finish();
    QProcess* process_{nullptr};
    QTimer timeout_;
    QString executable_, status_, failure_;
    QStringList arguments_;
    QByteArray buffer_;
    QJsonArray results_;
    QJsonObject pendingResult_;
    int level_{2}, completed_{0}, timeoutMs_{120000};
    double progress_{0};
    bool running_{false};
};
}
