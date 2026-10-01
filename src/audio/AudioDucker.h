#pragma once

#include <QObject>
#include <QThread>
#include <memory>

namespace raceengineer {

class AudioDucker final : public QObject {
    Q_OBJECT

public:
    explicit AudioDucker(QObject* parent = nullptr);
    ~AudioDucker() override;

    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }
    void setEnabled(bool enabled);

    [[nodiscard]] float duckFactor() const noexcept { return duckFactor_; }
    void setDuckFactor(float factor);

    [[nodiscard]] bool isDucked() const noexcept { return ducked_; }
    void setDucked(bool ducked);

signals:
    void duckedChanged(bool ducked);
    void enabledChanged(bool enabled);

private:
    struct WorkerState;

    bool enabled_{true};
    bool ducked_{false};
    float duckFactor_{0.25f}; // -12dB (reduces game audio to 25%)

    QThread workerThread_;
    QObject* workerContext_{nullptr};
    std::shared_ptr<WorkerState> workerState_;
};

} // namespace raceengineer
