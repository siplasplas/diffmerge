#pragma once

#include <QString>

namespace diffmerge::corpus {

// Iterates all .info files in corpusDir, downloads the corresponding slider
// file pairs into outputDir (one sub-directory per repo), deduplicates by
// SHA-256 digest of the two URLs, and writes a CSV index per repo.
//
// Output layout:
//   outputDir/<repo>/          — downloaded file pairs: <digest>_0.ext, <digest>_1.ext
//   outputDir/<repo>.csv       — index: digest,sign blockBegin delta,...
//
// where <repo> = user&reponame (slash replaced by ampersand).
//
// Files that cannot be downloaded (deleted repos, HTTP errors) are silently
// skipped; the CSV entry is still written if at least one of the two files
// succeeded, otherwise the entry is omitted.
class CorpusProcessor {
public:
    explicit CorpusProcessor(const QString& corpusDir,
                             const QString& outputDir);
    void run();

private:
    void processRepo(const QString& infoPath, int idx, int total);

    QString m_corpusDir;
    QString m_outputDir;
};

}  // namespace diffmerge::corpus
