#pragma once

#include <QJsonObject>
#include <QObject>

namespace raceengineer {

enum class ApiState {
    Unavailable,
    Connected,
    Requesting,
    RateLimited,
    AuthenticationError,
    NetworkError,
    ProviderError
};

class ILLMProvider : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;
    ~ILLMProvider() override = default;

    [[nodiscard]] virtual bool supportsToolCalling() const noexcept = 0;
    [[nodiscard]] virtual QString providerName() const = 0;

public slots:
    virtual void sendChatRequest(const QJsonObject& request) = 0;
    virtual void testConnection() = 0;
    virtual void cancelRequest() = 0;

signals:
    void stateChanged(raceengineer::ApiState state, const QString& detail);
    void responseChunk(const QString& text);
    void responseReceived(const QJsonObject& response, qint64 latencyMilliseconds,
        qint64 firstTokenMilliseconds);
    void requestFailed(raceengineer::ApiState state, int httpStatus, const QString& message,
        qint64 latencyMilliseconds);
    void connectionTested(bool success, const QString& detail);
};

} // namespace raceengineer

Q_DECLARE_METATYPE(raceengineer::ApiState)
