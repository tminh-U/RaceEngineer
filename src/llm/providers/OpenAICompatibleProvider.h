#pragma once

#include "llm/ILLMProvider.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QUrl>

class QNetworkReply;

namespace raceengineer {

struct ProviderConfiguration final {
    QString name{QStringLiteral("OpenAI-compatible")};
    QUrl baseUrl;
    QString apiKey;
    QString model;
    bool streaming{true};
    int timeoutMilliseconds{30000};
};

class OpenAICompatibleProvider : public ILLMProvider {
    Q_OBJECT

public:
    explicit OpenAICompatibleProvider(ProviderConfiguration configuration, QObject* parent = nullptr);
    [[nodiscard]] bool supportsToolCalling() const noexcept override { return true; }
    [[nodiscard]] QString providerName() const override { return configuration_.name; }
    void setConfiguration(const ProviderConfiguration& configuration);

    [[nodiscard]] static QUrl chatEndpoint(const QUrl& baseUrl);
    [[nodiscard]] static QUrl modelsEndpoint(const QUrl& baseUrl);
    [[nodiscard]] static QByteArray authorizationHeader(const QString& apiKey);
    [[nodiscard]] static QString parseErrorMessage(const QByteArray& body);
    [[nodiscard]] static QJsonObject parseResponseBody(const QByteArray& body,
        QString* errorMessage = nullptr);
    [[nodiscard]] static bool modelListContains(const QByteArray& body, const QString& model);
    [[nodiscard]] static QJsonObject serializeRequest(QJsonObject request,
        const ProviderConfiguration& configuration);
    [[nodiscard]] static ApiState classifyError(int httpStatus, bool networkFailure);

public slots:
    void sendChatRequest(const QJsonObject& request) override;
    void testConnection() override;
    void cancelRequest() override;

private slots:
    void consumeStreamingData();
    void finishRequest();
    void finishConnectionTest();

private:
    struct ToolAccumulator {
        QString id;
        QString name;
        QString arguments;
    };

    void startRequest(QJsonObject request, int retryCount);
    void startConnectionTest();
    void processSseLine(const QByteArray& line);
    void fail(ApiState state, int status, const QString& message);
    void resetStreamState();

    ProviderConfiguration configuration_;
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    QTimer timeout_;
    QElapsedTimer latency_;
    QByteArray streamBuffer_;
    QByteArray rawStreamData_;
    QString streamedContent_;
    QHash<int, ToolAccumulator> streamedTools_;
    QJsonObject streamedUsage_;
    QJsonObject activeRequest_;
    qint64 firstTokenMilliseconds_{-1};
    int retryCount_{0};
    int connectionTestRetryCount_{0};
    bool cancelled_{false};
    bool testingConnection_{false};
    bool timedOut_{false};
};

} // namespace raceengineer
