#include "integration/UpdateChecker.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QVersionNumber>
#include <utility>

namespace raceengineer {

UpdateChecker::UpdateChecker(QObject* parent) : QObject(parent)
{
#ifdef RACEENGINEER_BUILD_COMMIT
    sourceCommit_ = QStringLiteral(RACEENGINEER_BUILD_COMMIT);
#endif
    const QRegularExpression commit(QStringLiteral("^[0-9a-f]{7,40}$"));
    if (!commit.match(sourceCommit_).hasMatch()) {
        sourceCommit_.clear();
        QFile file(QDir(QCoreApplication::applicationDirPath()).filePath("../release-manifest.json"));
        if (file.open(QIODevice::ReadOnly)) {
            const auto manifest = QJsonDocument::fromJson(file.readAll()).object();
            const QString value = manifest.value("commit").toString();
            if (manifest.value("version").toString() == QCoreApplication::applicationVersion()
                && commit.match(value).hasMatch()) sourceCommit_ = value;
        }
    }
}

QString UpdateChecker::currentVersion() const
{
    return QCoreApplication::applicationVersion()
        + (sourceCommit_.isEmpty() ? QString{} : QStringLiteral(" · %1").arg(sourceCommit_.left(7)));
}

void UpdateChecker::finish(const QString& status, bool available)
{
    status_ = status;
    checking_ = false;
    updateAvailable_ = available;
    emit changed();
}

void UpdateChecker::fetch(const QString& endpoint, std::function<void(const QJsonObject&)> accept)
{
    QNetworkRequest request(QUrl("https://api.github.com/repos/tminh-U/RaceEngineer/" + endpoint));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("User-Agent", "RaceEngineer-update-check");
    request.setTransferTimeout(15000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    auto* reply = network_.get(request);
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 1024 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, accept = std::move(accept)] {
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error = reply->error();
        const QByteArray bytes = reply->readAll();
        reply->deleteLater();
        if (http == 403 || http == 429) { finish("GitHub đang giới hạn lượt kiểm tra. Hãy thử lại sau."); return; }
        if (error != QNetworkReply::NoError || http != 200) {
            finish(http == 404 ? "Chưa có bản phát hành hoặc thông tin build để đối chiếu."
                               : "Không kiểm tra được cập nhật. Kiểm tra mạng rồi thử lại.");
            return;
        }
        const auto document = QJsonDocument::fromJson(bytes);
        if (!document.isObject()) { finish("Phản hồi cập nhật không hợp lệ."); return; }
        accept(document.object());
    });
}

void UpdateChecker::check()
{
    if (checking_) return;
    checking_ = true;
    updateAvailable_ = false;
    latestVersion_.clear();
    releaseTag_.clear();
    status_ = "Đang kiểm tra bản ổn định mới nhất trên GitHub…";
    emit changed();
    fetch("releases/latest", [this](const QJsonObject& release) {
        const QString tag = release.value("tag_name").toString();
        const QRegularExpression stable(QStringLiteral("^v?([0-9]+\\.[0-9]+\\.[0-9]+)$"));
        const auto match = stable.match(tag);
        if (!match.hasMatch() || release.value("draft").toBool() || release.value("prerelease").toBool()) {
            finish("Chưa có bản phát hành ổn định hợp lệ.");
            return;
        }
        releaseTag_ = tag;
        latestVersion_ = match.captured(1);
        bool hasInstaller = false;
        for (const auto& value : release.value("assets").toArray()) {
            const auto asset = value.toObject();
            hasInstaller = hasInstaller || (asset.value("name").toString() == "RaceEngineer-" + latestVersion_ + "-Setup.exe"
                && asset.value("state").toString() == "uploaded" && asset.value("size").toDouble() > 0);
        }
        if (!hasInstaller) { finish("Bản phát hành chưa có installer Windows sẵn sàng."); return; }
        const int order = QVersionNumber::compare(QVersionNumber::fromString(latestVersion_),
            QVersionNumber::fromString(QCoreApplication::applicationVersion()));
        if (order > 0) { finish("Có phiên bản mới: " + latestVersion_ + ".", true); return; }
        if (order < 0) { finish("Build hiện tại mới hơn phiên bản ổn định trên GitHub."); return; }
        if (sourceCommit_.isEmpty()) { finish("Cùng số phiên bản; build này thiếu mã nguồn để kiểm tra bản tái phát hành."); return; }
        fetch("compare/" + sourceCommit_ + "..." + releaseTag_, [this](const QJsonObject& comparison) {
            const QString relation = comparison.value("status").toString();
            if (relation == "ahead") finish("Có bản tái phát hành " + latestVersion_ + " mới hơn build đang dùng.", true);
            else if (relation == "identical" || relation == "behind") finish("Bạn đang dùng build mới nhất hoặc mới hơn bản ổn định.");
            else finish("Build khác nhánh nguồn của release. Mở bản phát hành để đối chiếu.");
        });
    });
}

void UpdateChecker::openRelease()
{
    QDesktopServices::openUrl(QUrl(releaseTag_.isEmpty()
        ? QStringLiteral("https://github.com/tminh-U/RaceEngineer/releases/latest")
        : QStringLiteral("https://github.com/tminh-U/RaceEngineer/releases/tag/") + releaseTag_));
}

} // namespace raceengineer
