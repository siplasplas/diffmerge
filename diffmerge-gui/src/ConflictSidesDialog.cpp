#include "ConflictSidesDialog.h"
#include "MergeResultSave.h"
#include <diffmerge/ConflictMarkers.h>
#include <diffmerge/FileDiffWidget.h>
#include <QDialog>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QTextStream>
#include <QVBoxLayout>

namespace diffmerge::gui::desktop {
namespace {
QString decodedProjection(const QByteArray& bytes) {
    const auto text=bytes.startsWith(QByteArray::fromHex("efbbbf")) ? bytes.mid(3) : bytes;
    return QString::fromUtf8(text);
}
int fail(const QString& message) {
    QTextStream(stderr)<<message<<'\n';
    return 2;
}
}
int runConflictSidesDialog(const QString& path, int markerSize) {
    const auto loaded=readMergeResultFile(path);
    if (loaded.status!=MergeSaveStatus::Saved) return fail(loaded.message);
    MergeSessionInputs inputs;
    MergeResultSeed seed;
    seed.file.availability=MergeAvailability::Present;
    seed.file.bytes=loaded.bytes;
    seed.file.fileName=QFileInfo(path).fileName();
    seed.file.label=path;
    inputs.resultSeed=std::move(seed);
    const auto prepared=prepareMergeSession(inputs);
    if (prepared.status!=MergeSessionStatus::Ready || !prepared.session) return fail(prepared.message);
    MarkerImportOptions markers;
    markers.markerSize=markerSize;
    markers.allowUnconfirmedMarkers=true;
    const auto imported=importConflictMarkers(*prepared.session,markers);
    if (imported.status!=MergeSessionStatus::Ready) return fail(imported.message);
    if (imported.conflicts.isEmpty()) return fail(QStringLiteral("No conflict markers found in: %1").arg(path));

    QByteArray left, right;
    qint64 cursor=0;
    for (const auto& conflict:imported.conflicts) {
        const auto start=conflict.resultBytes.start;
        const auto shared=loaded.bytes.mid(cursor,start-cursor);
        left+=shared;
        right+=shared;
        left+=conflict.ours;
        right+=conflict.theirs;
        cursor=start+conflict.resultBytes.length;
    }
    left+=loaded.bytes.mid(cursor);
    right+=loaded.bytes.mid(cursor);
    const auto name=QFileInfo(path).fileName();
    const auto ours=TextSnapshot::fromText(decodedProjection(left),QStringLiteral("RESULT (HEAD)"),name);
    const auto theirs=TextSnapshot::fromText(decodedProjection(right),QStringLiteral("Incoming"),name);
    ComparisonOptions options;
    options.splitReplacementsRightFirst=true;
    const auto comparison=prepareComparison(ours,theirs,options);
    if (comparison.status!=PreparationStatus::Ready || !comparison.comparison) return fail(comparison.message);

    QDialog dialog;
    dialog.setWindowTitle(QStringLiteral("Conflict sides — %1").arg(name));
    dialog.resize(1200,700);
    auto* layout=new QVBoxLayout(&dialog);
    auto* headings=new QHBoxLayout;
    headings->addWidget(new QLabel(QStringLiteral("RESULT (HEAD)"),&dialog),1);
    headings->addWidget(new QLabel(QStringLiteral("Incoming"),&dialog),1);
    layout->addLayout(headings);
    auto* view=new FileDiffWidget(&dialog);
    view->setPathBarVisible(false);
    view->setEditable(Side::Left,false);
    view->setEditable(Side::Right,false);
    view->setComparison(comparison.comparison);
    layout->addWidget(view,1);
    QStringList ancestorSections;
    for (int i=0;i<imported.conflicts.size();++i) {
        const auto& conflict=imported.conflicts[i];
        if (!conflict.base) continue;
        const auto heading=QStringLiteral("Conflict %1 — %2").arg(i+1).arg(
            conflict.baseLabel.isEmpty() ? QStringLiteral("ancestor") : conflict.baseLabel);
        ancestorSections.append(heading+QLatin1Char('\n')+QString::fromUtf8(*conflict.base));
    }
    if (!ancestorSections.isEmpty()) {
        auto* showAncestor=new QCheckBox(QStringLiteral("Show ancestor fragments"),&dialog);
        showAncestor->setToolTip(QStringLiteral("Fragments from ||||||| markers; this is not a complete BASE file"));
        layout->addWidget(showAncestor);
        auto* ancestor=new QPlainTextEdit(&dialog);
        ancestor->setReadOnly(true);
        ancestor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        ancestor->setPlainText(ancestorSections.join(QStringLiteral("\n\n")));
        ancestor->hide();
        layout->addWidget(ancestor,1);
        QObject::connect(showAncestor,&QCheckBox::toggled,ancestor,&QWidget::setVisible);
    }
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
    layout->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    auto* next=new QShortcut(Qt::Key_F7,&dialog);
    QObject::connect(next,&QShortcut::activated,view,&FileDiffWidget::navigateToNext);
    auto* previous=new QShortcut(Qt::SHIFT | Qt::Key_F7,&dialog);
    QObject::connect(previous,&QShortcut::activated,view,&FileDiffWidget::navigateToPrev);
    if (view->changeCount()>0) view->navigateToChange(0);
    dialog.exec();
    return 0;
}
} // namespace diffmerge::gui::desktop
