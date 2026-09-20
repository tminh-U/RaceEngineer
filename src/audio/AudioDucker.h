#pragma once

#include <QObject>
#include <map>

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
    void performDuck();
    void performUnduck();

    bool enabled_{true};
    bool ducked_{false};
    float duckFactor_{0.25f}; // -12dB (reduces game audio to 25%)

    std::map<unsigned long, float> savedVolumes_;
};

} // namespace raceengineer
