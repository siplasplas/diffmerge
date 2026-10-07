#include "ResolveDialog.h"
#include "MergeResultSave.h"
#include <diffmerge/ConflictResolverWidget.h>
#include <diffmerge/MergeWidget.h>
#include <qxfiledialog.h>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QShortcut>
#include <QTextStream>
#include <QVBoxLayout>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif
namespace diffmerge::gui::desktop {
namespace {
class ResolveDialog : public QDialog {
public:
    using QDialog::QDialog;
    ConflictResolverWidget* resolver=nullptr;
    void reject() override {
        if(resolver && resolver->isModified() && QMessageBox::question(this,"Unsaved resolution",
            "Discard unsaved conflict decisions and RESULT edits?",QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Discard) return;
        if(resolver) resolver->cancelAnalysis();
        QDialog::reject();
    }
};
bool alias(const QString& a,const QString& b) {
    if(QFileInfo(a).absoluteFilePath()==QFileInfo(b).absoluteFilePath()) return true;
#ifdef Q_OS_UNIX
    struct stat x{},y{};
    if(::stat(QFile::encodeName(a).constData(),&x)==0 && ::stat(QFile::encodeName(b).constData(),&y)==0) return x.st_dev==y.st_dev && x.st_ino==y.st_ino;
#endif
    return QFileInfo(a).exists() && QFileInfo(b).exists() && QFileInfo(a).canonicalFilePath()==QFileInfo(b).canonicalFilePath();
}
bool publish(const QString& path,const QByteArray& bytes) {
    QSaveFile file(path); file.setDirectWriteFallback(false);
    return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size() && file.commit();
}
}
int runResolveDialog(const QString& path,int markerSize,QWidget* parent) {
    const auto loaded=readMergeResultFile(path);
    if(loaded.status!=MergeSaveStatus::Saved) { QTextStream(stderr)<<loaded.message<<'\n'; return 2; }
    ResolveDialog dialog(parent); dialog.setWindowTitle("Automatic conflict resolution — "+QFileInfo(path).fileName()); dialog.resize(1300,900);
    auto* layout=new QVBoxLayout(&dialog);
    auto* status=new QLabel(&dialog); status->setTextFormat(Qt::PlainText); status->setWordWrap(true); layout->addWidget(status);
    auto* resolver=new ConflictResolverWidget(&dialog); dialog.resolver=resolver; layout->addWidget(resolver,1);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close,&dialog);
    auto* save=buttons->addButton("Save result as...",QDialogButtonBox::ActionRole); save->setEnabled(false); layout->addWidget(buttons);
    QObject::connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    QObject::connect(resolver,&ConflictResolverWidget::operationFailed,status,&QLabel::setText);
    QObject::connect(resolver,&ConflictResolverWidget::analysisFinished,&dialog,[save,status,resolver](bool ready) {
        save->setEnabled(ready);
        if(ready) status->setText(resolver->pendingDecisionCount() ? "Review the remaining conflicts. Original evidence remains available after marker removal." : "All recognized text conflicts are resolved; automatic policy decisions can still be overridden.");
    });
    QObject::connect(save,&QPushButton::clicked,&dialog,[resolver,&dialog,path,status] {
        const auto bytes=resolver->resultBytes(); if(!bytes) { status->setText("RESULT cannot be serialized"); return; }
        if(resolver->pendingDecisionCount() && QMessageBox::question(&dialog,"Save working draft",
            "Some decisions remain pending. Save this working draft and its review report?",QMessageBox::Save|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Save) return;
        const auto output=QxFileDialog::getSaveFileName(&dialog,"Save resolved text or working draft",{});
        if(output.isEmpty()) return;
        const auto reportPath=output+".resolution.json";
        if(QFileInfo(output).isSymLink() || QFileInfo(reportPath).isSymLink() || alias(path,output) || alias(path,reportPath) || alias(output,reportPath)) {
            status->setText("Choose distinct regular output and report paths; the input is preserved"); return;
        }
        if(QFileInfo(reportPath).exists() && QMessageBox::question(&dialog,"Replace report","Replace the existing resolution report?",QMessageBox::Yes|QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes) return;
        auto report=resolver->report(); auto publication=report["publication"].toObject(); publication["contentPublished"]=true; report["publication"]=publication;
        if(!publish(output,*bytes)) { status->setText("Could not save RESULT"); return; }
        if(!publish(reportPath,QJsonDocument(report).toJson())) { status->setText("RESULT saved, but its review report could not be saved"); return; }
        if(auto captured=resolver->mergeEditor()->captureExportInput()) resolver->mergeEditor()->acknowledgeSaved(*captured);
        status->setText(resolver->pendingDecisionCount() ? "Working draft and report saved; decisions remain pending." : "Resolved text and report saved. Repository state was not changed.");
    });
    auto* next=new QShortcut(Qt::Key_F7,&dialog); QObject::connect(next,&QShortcut::activated,resolver,&ConflictResolverWidget::navigateToNextPending);
    auto* previous=new QShortcut(Qt::SHIFT|Qt::Key_F7,&dialog); QObject::connect(previous,&QShortcut::activated,resolver,&ConflictResolverWidget::navigateToPreviousPending);
    resolver->setInput(loaded.bytes,QFileInfo(path).fileName(),{markerSize,{}});
    dialog.exec(); return 0;
}
} // namespace diffmerge::gui::desktop
