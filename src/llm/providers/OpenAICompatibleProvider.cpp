#include "llm/providers/OpenAICompatibleProvider.h"

#include "utils/Logging.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <algorithm>

namespace raceengineer {

OpenAICompatibleProvider::OpenAICompatibleProvider(ProviderConfiguration configuration,
    QObject* const parent)
    : ILLMProvider(parent)
    , configuration_(std::move(configuration))
{
    timeout_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if (reply_) {
            timedOut_ = true;
            reply_->abort();
        }
    });
}

void OpenAICompatibleProvider::setConfiguration(const ProviderConfiguration& configuration)
{
    cancelRequest();
    configuration_ = configuration;
}

QUrl OpenAICompatibleProvider::chatEndpoint(const QUrl& baseUrl)
{
    QUrl result(baseUrl);
    QString path = result.path();
    while (path.endsWith(u'/')) {
        path.chop(1);
    }
    if (!path.endsWith(QStringLiteral("/chat/completions"))) {
        path += QStringLiteral("/chat/completions");
    }
    result.setPath(path);
    return result;
}

QUrl OpenAICompatibleProvider::modelsEndpoint(const QUrl& baseUrl)
{
    QUrl result(baseUrl);
    QString path = result.path();
    while (path.endsWith(u'/')) path.chop(1);
    if (!path.endsWith(QStringLiteral("/models"))) path += QStringLiteral("/models");
    result.setPath(path);
    return result;
}

QByteArray OpenAICompatibleProvider::authorizationHeader(const QString& apiKey)
{
    return apiKey.isEmpty() ? QByteArray{} : QByteArrayLiteral("Bearer ") + apiKey.toUtf8();
}

QString OpenAICompatibleProvider::parseErrorMessage(const QByteArray& body)
{
    const auto document = QJsonDocument::fromJson(body);
    if (!document.isObject()) {
        return QStringLiteral("Provider returned an invalid response.");
    }
    const auto error = document.object().value(QStringLiteral("error"));
    if (error.isObject()) {
        return error.toObject().value(QStringLiteral("message")).toString(
            QStringLiteral("Provider error."));
    }
    if (error.isString()) {
        return error.toString();
    }
    return document.object().value(QStringLiteral("message")).toString(
        QStringLiteral("Provider error."));
}

QJsonObject OpenAICompatibleProvider::parseResponseBody(const QByteArray& body,
    QString* const errorMessage)
{
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorMessage != nullptr) *errorMessage = QStringLiteral("Invalid JSON response from LLM server.");
        return {};
    }
    if (errorMessage != nullptr) errorMessage->clear();
    return document.object();
}

bool OpenAICompatibleProvider::modelListContains(const QByteArray& body, const QString& model)
{
    const auto normalizedModelId = [](QString id) {
        id = id.trimmed();
        constexpr auto resourcePrefix = "models/";
        if (id.startsWith(QLatin1StringView(resourcePrefix), Qt::CaseInsensitive)) {
            id.remove(0, 7);
        }
        return id;
    };
    const QString requestedModel = normalizedModelId(model);
    const auto root = parseResponseBody(body);
    for (const auto& value : root.value(QStringLiteral("data")).toArray()) {
        const QString listedModel = normalizedModelId(
            value.toObject().value(QStringLiteral("id")).toString());
        if (listedModel.compare(requestedModel, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

QJsonObject OpenAICompatibleProvider::serializeRequest(QJsonObject request,
    const ProviderConfiguration& configuration)
{
    request.insert(QStringLiteral("model"), configuration.model);
    request.insert(QStringLiteral("stream"), configuration.streaming);
    return request;
}

ApiState OpenAICompatibleProvider::classifyError(const int httpStatus, const bool networkFailure)
{
    if (httpStatus == 401 || httpStatus == 403) return ApiState::AuthenticationError;
    if (httpStatus == 429) return ApiState::RateLimited;
    return networkFailure ? ApiState::NetworkError : ApiState::ProviderError;
}

void OpenAICompatibleProvider::sendChatRequest(const QJsonObject& request)
{
    cancelRequest();
    cancelled_ = false;
    startRequest(request, 0);
}

void OpenAICompatibleProvider::testConnection()
{
    cancelRequest();
    cancelled_ = false;
    connectionTestRetryCount_ = 0;
    startConnectionTest();
}

void OpenAICompatibleProvider::startConnectionTest()
{
    testingConnection_ = true;
    timedOut_ = false;
    QNetworkRequest request(modelsEndpoint(configuration_.baseUrl));
    request.setRawHeader("Accept", "application/json");
    const QByteArray authorization = authorizationHeader(configuration_.apiKey);
    if (!authorization.isEmpty()) request.setRawHeader("Authorization", authorization);
    request.setTransferTimeout(configuration_.timeoutMilliseconds);
    latency_.restart();
    emit stateChanged(ApiState::Requesting, QStringLiteral("Checking LLM server"));
    reply_ = network_.get(request);
    connect(reply_, &QNetworkReply::finished, this, &OpenAICompatibleProvider::finishConnectionTest);
    timeout_.start(configuration_.timeoutMilliseconds);
}

void OpenAICompatibleProvider::cancelRequest()
{
    cancelled_ = true;
    timeout_.stop();
    if (reply_) {
        reply_->abort();
        reply_->deleteLater();
        reply_.clear();
    }
    resetStreamState();
    testingConnection_ = false;
}

void OpenAICompatibleProvider::startRequest(QJsonObject request, const int retryCount)
{
    testingConnection_ = false;
    timedOut_ = false;
    retryCount_ = retryCount;
    activeRequest_ = request;
    resetStreamState();
    request = serializeRequest(std::move(request), configuration_);

    QNetworkRequest networkRequest(chatEndpoint(configuration_.baseUrl));
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    const QByteArray authorization = authorizationHeader(configuration_.apiKey);
    if (!authorization.isEmpty()) networkRequest.setRawHeader("Authorization", authorization);
    networkRequest.setRawHeader("Accept", configuration_.streaming ? "text/event-stream" : "application/json");
    networkRequest.setTransferTimeout(configuration_.timeoutMilliseconds);

    latency_.restart();
    emit stateChanged(ApiState::Requesting, QStringLiteral("Requesting"));
    reply_ = network_.post(networkRequest, QJsonDocument(request).toJson(QJsonDocument::Compact));
    if (configuration_.streaming) {
        connect(reply_, &QNetworkReply::readyRead, this, &OpenAICompatibleProvider::consumeStreamingData);
    }
    connect(reply_, &QNetworkReply::finished, this, &OpenAICompatibleProvider::finishRequest);
    timeout_.start(configuration_.timeoutMilliseconds);
    qCInfo(logApi) << "Chat request started for" << configuration_.name << configuration_.model;
}

void OpenAICompatibleProvider::consumeStreamingData()
{
    if (!reply_) {
        return;
    }
    const QByteArray chunk = reply_->readAll();
    // The explicit timer guards connection/first-byte latency. Once streaming
    // begins, QNetworkRequest's transfer timeout handles stalled connections.
    if (!chunk.isEmpty()) timeout_.stop();
    rawStreamData_.append(chunk);
    streamBuffer_.append(chunk);
    qsizetype newline = -1;
    while ((newline = streamBuffer_.indexOf('\n')) >= 0) {
        QByteArray line = streamBuffer_.left(newline).trimmed();
        streamBuffer_.remove(0, newline + 1);
        processSseLine(line);
    }
}

void OpenAICompatibleProvider::processSseLine(const QByteArray& line)
{
    if (!line.startsWith("data:")) {
        return;
    }
    const QByteArray data = line.mid(5).trimmed();
    if (data == "[DONE]") {
        return;
    }
    const auto document = QJsonDocument::fromJson(data);
    if (!document.isObject()) {
        return;
    }
    const auto root = document.object();
    if (root.value(QStringLiteral("usage")).isObject()) {
        streamedUsage_ = root.value(QStringLiteral("usage")).toObject();
    }
    const auto choices = root.value(QStringLiteral("choices")).toArray();
    if (choices.isEmpty()) {
        return;
    }
    const auto delta = choices.first().toObject().value(QStringLiteral("delta")).toObject();
    const QString content = delta.value(QStringLiteral("content")).toString();
    if (!content.isEmpty()) {
        if (firstTokenMilliseconds_ < 0) firstTokenMilliseconds_ = latency_.elapsed();
        streamedContent_ += content;
        emit responseChunk(content);
    }
    for (const auto& value : delta.value(QStringLiteral("tool_calls")).toArray()) {
        const auto call = value.toObject();
        const int index = call.value(QStringLiteral("index")).toInt();
        auto& accumulator = streamedTools_[index];
        if (!call.value(QStringLiteral("id")).toString().isEmpty()) {
            accumulator.id = call.value(QStringLiteral("id")).toString();
        }
        const auto function = call.value(QStringLiteral("function")).toObject();
        accumulator.name += function.value(QStringLiteral("name")).toString();
        const QJsonValue arguments = function.value(QStringLiteral("arguments"));
        if (arguments.isString()) {
            accumulator.arguments += arguments.toString();
        } else if (arguments.isObject()) {
            accumulator.arguments += QString::fromUtf8(
                QJsonDocument(arguments.toObject()).toJson(QJsonDocument::Compact));
        }
        if (firstTokenMilliseconds_ < 0) firstTokenMilliseconds_ = latency_.elapsed();
    }
}

void OpenAICompatibleProvider::finishRequest()
{
    if (!reply_) {
        return;
    }
    timeout_.stop();
    QNetworkReply* const finished = reply_;
    const int status = finished->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto networkError = finished->error();
    const QString networkMessage = finished->errorString();
    QByteArray body;
    if (configuration_.streaming) {
        // Drain the final bytes before clearing the guarded reply pointer. Some
        // providers place the last content/tool delta in the finished packet.
        consumeStreamingData();
        if (!streamBuffer_.trimmed().isEmpty()) {
            processSseLine(streamBuffer_.trimmed());
            streamBuffer_.clear();
        }
    } else {
        body = finished->readAll();
    }
    reply_.clear();
    finished->deleteLater();

    if (cancelled_) {
        return;
    }
    if (networkError != QNetworkReply::NoError || status >= 400) {
        if (body.isEmpty() && configuration_.streaming) body = rawStreamData_;
        const bool temporary = timedOut_ || networkError == QNetworkReply::TimeoutError
            || networkError == QNetworkReply::TemporaryNetworkFailureError || status >= 500;
        if (temporary && retryCount_ < 1 && streamedContent_.isEmpty() && streamedTools_.isEmpty()) {
            QTimer::singleShot(250, this, [this] { startRequest(activeRequest_, retryCount_ + 1); });
            return;
        }
        const ApiState state = classifyError(status, networkError != QNetworkReply::NoError);
        const QString detail = timedOut_ ? QStringLiteral("LLM request timed out after %1 ms")
                                               .arg(configuration_.timeoutMilliseconds)
                                         : (body.isEmpty() ? networkMessage : parseErrorMessage(body));
        fail(state, status, detail);
        return;
    }

    QJsonObject response;
    if (configuration_.streaming) {
        QJsonObject message{{QStringLiteral("role"), QStringLiteral("assistant")},
            {QStringLiteral("content"), streamedContent_}};
        QJsonArray calls;
        QList<int> indexes = streamedTools_.keys();
        std::sort(indexes.begin(), indexes.end());
        for (const int index : indexes) {
            const auto& tool = streamedTools_[index];
            calls.append(QJsonObject{{QStringLiteral("id"), tool.id},
                {QStringLiteral("type"), QStringLiteral("function")},
                {QStringLiteral("function"), QJsonObject{{QStringLiteral("name"), tool.name},
                    {QStringLiteral("arguments"), tool.arguments}}}});
        }
        if (!calls.isEmpty()) message.insert(QStringLiteral("tool_calls"), calls);
        response.insert(QStringLiteral("choices"), QJsonArray{QJsonObject{
            {QStringLiteral("message"), message}, {QStringLiteral("finish_reason"),
                calls.isEmpty() ? QStringLiteral("stop") : QStringLiteral("tool_calls")}}});
        response.insert(QStringLiteral("usage"), streamedUsage_);
    } else {
        QString parseError;
        response = parseResponseBody(body, &parseError);
        if (!parseError.isEmpty()) {
            fail(ApiState::ProviderError, status, parseError);
            return;
        }
    }
    emit stateChanged(ApiState::Connected, QStringLiteral("Connected"));
    emit responseReceived(response, latency_.elapsed(), firstTokenMilliseconds_);
}

void OpenAICompatibleProvider::finishConnectionTest()
{
    if (!reply_ || !testingConnection_) return;
    timeout_.stop();
    QNetworkReply* const finished = reply_;
    reply_.clear();
    testingConnection_ = false;
    const QByteArray body = finished->readAll();
    const int status = finished->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto networkError = finished->error();
    const QString networkMessage = finished->errorString();
    finished->deleteLater();
    if (cancelled_) return;

    // Some lightweight HTTP servers close the socket without a clean keep-alive
    // shutdown. A complete HTTP 200 model list is still a valid success.
    if (status == 200 && modelListContains(body, configuration_.model)) {
        emit stateChanged(ApiState::Connected, QStringLiteral("Model available"));
        emit connectionTested(true, QStringLiteral("Connected — model available"));
        return;
    }

    if (networkError != QNetworkReply::NoError || status != 200) {
        if (connectionTestRetryCount_ < 1) {
            ++connectionTestRetryCount_;
            QTimer::singleShot(250, this, &OpenAICompatibleProvider::startConnectionTest);
            return;
        }
        const ApiState state = classifyError(status, networkError != QNetworkReply::NoError);
        const QString endpoint = modelsEndpoint(configuration_.baseUrl).toString();
        const QString detail = timedOut_
            ? QStringLiteral("Timeout after %1 ms: %2").arg(configuration_.timeoutMilliseconds).arg(endpoint)
            : networkError != QNetworkReply::NoError
                ? QStringLiteral("%1: %2").arg(networkMessage, endpoint)
                : QStringLiteral("HTTP %1: %2").arg(status).arg(
                      body.isEmpty() ? networkMessage : parseErrorMessage(body));
        emit stateChanged(state, detail);
        emit connectionTested(false, detail);
        return;
    }
    if (!modelListContains(body, configuration_.model)) {
        const QString detail = QStringLiteral("Connected, but model '%1' is unavailable").arg(configuration_.model);
        emit stateChanged(ApiState::ProviderError, detail);
        emit connectionTested(false, detail);
        return;
    }
}

void OpenAICompatibleProvider::fail(const ApiState state, const int status, const QString& message)
{
    emit stateChanged(state, message);
    emit requestFailed(state, status, message, latency_.isValid() ? latency_.elapsed() : 0);
}

void OpenAICompatibleProvider::resetStreamState()
{
    streamBuffer_.clear();
    rawStreamData_.clear();
    streamedContent_.clear();
    streamedTools_.clear();
    streamedUsage_ = {};
    firstTokenMilliseconds_ = -1;
}

} // namespace raceengineer
