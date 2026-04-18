#pragma once

#include <QNetworkAccessManager>
#include <QString>

namespace diffmerge::corpus {

// Synchronous-style HTTP GET downloader backed by QNetworkAccessManager.
// Skips files that return HTTP 4xx/5xx or network errors (deleted repos etc.)
// and logs a warning — callers should treat a false return as "skip this entry".
class Downloader {
public:
    Downloader();

    // Download url to outputPath.  Returns true on success, false on any
    // error (network failure, HTTP error, write failure).
    bool download(const QString& url, const QString& outputPath);

private:
    QNetworkAccessManager m_manager;
};

}  // namespace diffmerge::corpus
