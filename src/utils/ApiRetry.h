#pragma once

#include <QDateTime>
#include <QNetworkReply>
#include <algorithm>

namespace raceengineer {
inline bool transientApiFailure(int status, QNetworkReply::NetworkError error, bool timedOut = false)
{
    if (status == 401 || status == 403 || (status >= 400 && status < 500 && status != 408 && status != 429)) return false;
    return timedOut || status == 408 || status == 429 || (status >= 500 && status < 600)
        || error == QNetworkReply::ConnectionRefusedError || error == QNetworkReply::RemoteHostClosedError
        || error == QNetworkReply::HostNotFoundError || error == QNetworkReply::TimeoutError
        || error == QNetworkReply::TemporaryNetworkFailureError || error == QNetworkReply::NetworkSessionFailedError;
}

// Do not hold a radio utterance for long quota waits or retry earlier than Retry-After.
inline int apiRetryDelay(const QByteArray& retryAfter, int attempt)
{
    int delay = 500 << std::clamp(attempt, 0, 1);
    if (retryAfter.isEmpty()) return delay;
    bool numeric = false;
    const auto seconds = retryAfter.trimmed().toLongLong(&numeric);
    qint64 requested = 0;
    if (numeric) {
        if (seconds > 10) return -1;
        requested = std::max<qint64>(0, seconds) * 1000;
    } else {
        const auto date = QDateTime::fromString(QString::fromLatin1(retryAfter), Qt::RFC2822Date);
        if (date.isValid()) requested = QDateTime::currentDateTimeUtc().msecsTo(date);
    }
    if (requested > 10000) return -1;
    return std::max(delay, static_cast<int>(std::max<qint64>(0, requested)));
}
}
