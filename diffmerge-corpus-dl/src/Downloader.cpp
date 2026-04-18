#include "Downloader.h"

#include <QEventLoop>
#include <QFile>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTextStream>

namespace diffmerge::corpus {

Downloader::Downloader() = default;

bool Downloader::download(const QString& url, const QString& outputPath) {
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = m_manager.get(request);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const auto netError   = reply->error();
    const int  httpStatus = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (netError != QNetworkReply::NoError) {
        QTextStream(stderr) << "skip (network): " << url
                            << " — " << reply->errorString() << '\n';
        reply->deleteLater();
        return false;
    }
    if (httpStatus >= 400) {
        QTextStream(stderr) << "skip (HTTP " << httpStatus << "): " << url << '\n';
        reply->deleteLater();
        return false;
    }

    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly)) {
        QTextStream(stderr) << "skip (write error): " << outputPath << '\n';
        reply->deleteLater();
        return false;
    }
    file.write(reply->readAll());
    reply->deleteLater();
    return true;
}

}  // namespace diffmerge::corpus
