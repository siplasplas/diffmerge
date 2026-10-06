#pragma once
#include <QStringList>
#include <QVector>
#include <QDateTime>
namespace diffmerge::gui {
struct DirectoryCopyItem {
    QString source, destination;
    bool directory = false;
    bool overwrites = false;
    qint64 size = 0;
    QDateTime modified;
    qint64 destinationSize = 0;
    QDateTime destinationModified;
};
struct DirectoryCopyPlan {
    QString sourceRoot, destinationRoot;
    QVector<DirectoryCopyItem> items;
    QString error;
};
// Read-only preflight. Symbolic links, type collisions and escaping paths fail.
DirectoryCopyPlan planDirectoryCopy(const QString& sourceRoot, const QString& destinationRoot,
                                    const QStringList& relativePaths);
// The host obtains confirmation for the plan before calling this mutation.
// Returns the first error; an error may follow earlier successfully copied items.
QString executeDirectoryCopy(const DirectoryCopyPlan& plan);
// No permanent-delete fallback. The host confirms the exact selected paths.
QString trashDirectoryEntries(const QString& root, const QStringList& relativePaths);
}
