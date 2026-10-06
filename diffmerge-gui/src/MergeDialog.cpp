#include "MergeDialog.h"
#include "MergeResultSave.h"
#include <diffmerge/MergeWidget.h>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPromise>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTextStream>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
namespace diffmerge::gui::desktop {
namespace {
struct MergePreviewLoadResult {
    PrepareMergeSessionResult prepared;
    std::optional<MergeSaveStamp> stamp;
};
class MergeEditDialog : public QDialog {
public:
    using QDialog::QDialog;
    MergeWidget* merge = nullptr;
    bool saving = false, closeAfterSave = false, resolvedSaved = false;
    std::optional<MergeSaveStamp> savedStamp;
    diffcore::CancellationToken saveCancellation;
    std::function<bool(bool)> saveAndMaybeClose;
    void reject() override {
        if (saving) { saveCancellation.requestCancellation(); return; }
        if (merge && merge->isModified()) {
            const auto answer = QMessageBox::question(this,QStringLiteral("Unsaved merge result"),
                QStringLiteral("RESULT has unsaved changes and %1 unresolved conflicts. Save before closing?").arg(merge->unresolvedCount()),
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,QMessageBox::Cancel);
            if (answer == QMessageBox::Cancel) return;
            if (answer == QMessageBox::Save) { if (saveAndMaybeClose) saveAndMaybeClose(true); return; }
        }
        QDialog::reject();
    }
};
}
int runMergeDialog(const MergeLaunchOptions& launch, QWidget* parent) {
    const auto path=launch.resultPath;
    MergeEditDialog dialog(parent); dialog.setWindowTitle(QStringLiteral("Conflict editor")); dialog.resize(1200,700);
    auto* layout = new QVBoxLayout(&dialog);
    auto* status = new QLabel(QStringLiteral("Loading RESULT…"), &dialog);
    status->setTextFormat(Qt::PlainText); status->setWordWrap(true); layout->addWidget(status);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close,&dialog); layout->addWidget(close);
    QObject::connect(close,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    diffcore::CancellationToken cancellation;
    auto* watcher = new QFutureWatcher<MergePreviewLoadResult>(&dialog);
    QObject::connect(watcher,&QFutureWatcher<MergePreviewLoadResult>::finished,&dialog,[&dialog,layout,status,watcher,launch] {
        const auto loaded = watcher->result(); const auto& prepared = loaded.prepared;
        if (prepared.status != MergeSessionStatus::Ready || !loaded.stamp) { status->setText(prepared.message); return; }
        auto* preview = new MergeWidget(&dialog);
        preview->setViewMode(MergeViewMode::Conflicts); dialog.merge = preview;
        QObject::connect(preview,&MergePreviewWidget::operationFailed,status,[status](const QString& message) { status->setText(message); status->show(); });
        layout->insertWidget(1,preview,1);
        auto* controls = new QWidget(&dialog); auto* row = new QHBoxLayout(controls);
        auto* markerSize = new QSpinBox(controls); markerSize->setRange(1,200000); markerSize->setValue(launch.markerSize);
        auto* spacing = new QSlider(Qt::Horizontal,controls); spacing->setRange(8,160); spacing->setValue(24);
        auto* editable = new QCheckBox(QStringLiteral("Edit RESULT"),controls); row->addWidget(editable);
        QObject::connect(editable,&QCheckBox::toggled,preview,&MergeWidget::setEditable);
        QObject::connect(preview,&MergeWidget::editableChanged,editable,[editable](bool enabled) { QSignalBlocker blocker(editable); editable->setChecked(enabled); });
        QObject::connect(preview,&MergeWidget::modifiedChanged,markerSize,[markerSize,preview](bool modified) {
            markerSize->setEnabled(!modified && preview->resultBytes() == std::optional<QByteArray>{preview->session()->inputs().resultSeed->file.bytes});
        });
        row->addWidget(new QLabel(QStringLiteral("Marker length"),controls)); row->addWidget(markerSize);
        row->addWidget(new QLabel(QStringLiteral("Panel spacing"),controls)); row->addWidget(spacing,1);
        layout->insertWidget(2,controls);
        auto* saveControls = new QWidget(&dialog); auto* saveRow = new QHBoxLayout(saveControls);
        auto* saveDraft = new QPushButton(QStringLiteral("Save draft"),saveControls);
        auto* saveResolved = new QPushButton(QStringLiteral("Save resolved"),saveControls);
        auto* finish = new QPushButton(QStringLiteral("Save resolved and close"),saveControls);
        auto* progress = new QProgressBar(saveControls); progress->hide();
        auto* cancelSave = new QPushButton(QStringLiteral("Cancel save"),saveControls); cancelSave->hide();
        saveRow->addWidget(saveDraft); saveRow->addWidget(saveResolved); saveRow->addWidget(finish);
        saveRow->addWidget(progress,1); saveRow->addWidget(cancelSave); layout->insertWidget(3,saveControls);
        const auto import = [preview,status,session=prepared.session](int size) {
            MarkerImportOptions options; options.markerSize=size; options.allowUnconfirmedMarkers=true;
            if (preview->setSession(session,options)) status->hide();
        };
        QObject::connect(markerSize,&QSpinBox::valueChanged,preview,import);
        QObject::connect(spacing,&QSlider::valueChanged,preview,&MergePreviewWidget::setPanelSpacing); import(launch.markerSize);
        if (!launch.localPath.isEmpty()) preview->setEditable(!launch.readOnly);
        if (launch.readOnly) editable->setEnabled(false);
        const auto expected = std::make_shared<MergeSaveStamp>(*loaded.stamp);
        const auto updateButtons = [&dialog,preview,saveDraft,saveResolved,finish] {
            const bool writable = preview->isEditable() && !dialog.saving;
            saveDraft->setEnabled(writable); saveResolved->setEnabled(writable && preview->unresolvedCount()==0);
            finish->setEnabled(saveResolved->isEnabled());
        };
        QObject::connect(preview,&MergeWidget::editableChanged,&dialog,[updateButtons](bool) { updateButtons(); });
        QObject::connect(preview,&MergeWidget::conflictStatesChanged,&dialog,updateButtons); updateButtons();
        QObject::connect(cancelSave,&QPushButton::clicked,&dialog,[&dialog] { dialog.saveCancellation.requestCancellation(); });
        const auto startSave = [&dialog,preview,controls,status,progress,cancelSave,expected,updateButtons](MergeExportInput captured,MergeExportOptions options,bool closeAfter) {
            if (dialog.saving) return;
            dialog.resolvedSaved=false; dialog.closeAfterSave=false; dialog.saving=true; dialog.saveCancellation=diffcore::CancellationToken{};
            preview->setEnabled(false); controls->setEnabled(false); updateButtons();
            progress->setRange(0,0); progress->show(); cancelSave->show();
            status->setText(QStringLiteral("Validating and saving RESULT…")); status->show();
            auto* saving = new QFutureWatcher<MergeFileSaveResult>(&dialog);
            QObject::connect(saving,&QFutureWatcher<MergeFileSaveResult>::progressRangeChanged,progress,&QProgressBar::setRange);
            QObject::connect(saving,&QFutureWatcher<MergeFileSaveResult>::progressValueChanged,progress,&QProgressBar::setValue);
            QObject::connect(saving,&QFutureWatcher<MergeFileSaveResult>::finished,&dialog,[&dialog,preview,controls,status,progress,cancelSave,expected,updateButtons,saving,captured,closeAfter] {
                const auto result=saving->result(); saving->deleteLater(); dialog.saving=false;
                preview->setEnabled(true); controls->setEnabled(true); progress->hide(); cancelSave->hide();
                if (result.status != MergeSaveStatus::Saved || !result.stamp || !result.outcome) {
                    status->setText(result.message); updateButtons(); return;
                }
                *expected=*result.stamp;
                const bool acknowledged=preview->acknowledgeSaved(captured);
                dialog.savedStamp=result.stamp;
                dialog.resolvedSaved=acknowledged && result.outcome->disposition==MergeExportDisposition::Resolved && result.outcome->explicitlyCompleted;
                status->setText(result.outcome->disposition==MergeExportDisposition::Resolved
                    ? QStringLiteral("Resolved RESULT saved. Repository state was not changed.")
                    : QStringLiteral("Draft saved. Conflicts remain unresolved."));
                updateButtons();
                if (closeAfter && acknowledged && !preview->isModified()) dialog.accept();
            });
            saving->setFuture(QtConcurrent::run([captured=std::move(captured),options=std::move(options),stamp=*expected,token=dialog.saveCancellation](QPromise<MergeFileSaveResult>& promise) {
                const auto progressCallback=[&promise](qint64 value,qint64 total) {
                    promise.setProgressRange(0,int(total)); promise.setProgressValue(int(value));
                };
                promise.addResult(saveMergeResultFile(captured,options,stamp,{},token,progressCallback));
            }));
        };
        QObject::connect(preview,&MergeWidget::saveRequested,&dialog,[&dialog,startSave](MergeExportInput input,MergeExportOptions options) { startSave(std::move(input),std::move(options),dialog.closeAfterSave); });
        QObject::connect(preview,&MergeWidget::finishRequested,&dialog,[startSave](MergeExportInput input,MergeExportOptions options) { startSave(std::move(input),std::move(options),true); });
        const auto requestSave = [&dialog,preview](MergeExportDisposition disposition,bool closeAfter) {
            if (dialog.saving) return false;
            if (!preview->isEditable()) {
                QMessageBox::information(&dialog,QStringLiteral("Read-only RESULT"),QStringLiteral("Enable Edit RESULT before saving.")); return false;
            }
            MergeExportOptions options; options.disposition=disposition;
            if (disposition==MergeExportDisposition::Draft && preview->unresolvedCount()>0) {
                if (QMessageBox::question(&dialog,QStringLiteral("Save unresolved draft?"),
                    QStringLiteral("Save %1 unresolved conflicts with markers? This does not finish the merge.").arg(preview->unresolvedCount()),
                    QMessageBox::Save | QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Save) return false;
            }
            if (disposition==MergeExportDisposition::Resolved && !preview->session()->inputs().hostConflicts) {
                if (QMessageBox::question(&dialog,QStringLiteral("Confirm resolved result"),
                    QStringLiteral("No authoritative host conflict list is available. Have you reviewed RESULT and explicitly confirmed it as resolved?"),
                    QMessageBox::Yes | QMessageBox::Cancel,QMessageBox::Cancel)!=QMessageBox::Yes) return false;
                options.confirmUnknownConflictState=true;
            }
            if (closeAfter && disposition==MergeExportDisposition::Resolved) return preview->requestFinish(options);
            return preview->requestSave(options);
        };
        QObject::connect(saveDraft,&QPushButton::clicked,&dialog,[requestSave] { requestSave(MergeExportDisposition::Draft,false); });
        QObject::connect(saveResolved,&QPushButton::clicked,&dialog,[requestSave] { requestSave(MergeExportDisposition::Resolved,false); });
        QObject::connect(finish,&QPushButton::clicked,&dialog,[requestSave] { requestSave(MergeExportDisposition::Resolved,true); });
        dialog.saveAndMaybeClose=[&dialog,preview,requestSave](bool closeAfter) {
            const auto disposition=preview->unresolvedCount()>0 ? MergeExportDisposition::Draft : MergeExportDisposition::Resolved;
            dialog.closeAfterSave=closeAfter;
            const bool requested=requestSave(disposition,closeAfter);
            if (!requested) dialog.closeAfterSave=false;
            return requested;
        };
        auto* saveShortcut=new QShortcut(QKeySequence::Save,&dialog);
        QObject::connect(saveShortcut,&QShortcut::activated,&dialog,[&dialog] { dialog.saveAndMaybeClose(false); });
        auto* next=new QShortcut(Qt::Key_F7,&dialog); QObject::connect(next,&QShortcut::activated,preview,&MergePreviewWidget::navigateToNextConflict);
        auto* previous=new QShortcut(Qt::SHIFT | Qt::Key_F7,&dialog); QObject::connect(previous,&QShortcut::activated,preview,&MergePreviewWidget::navigateToPreviousConflict);
    });
    watcher->setFuture(QtConcurrent::run([path,launch,cancellation] {
        MergePreviewLoadResult result;
        const auto loaded=readMergeResultFile(path,{},cancellation);
        if (loaded.status!=MergeSaveStatus::Saved || !loaded.stamp) {
            result.prepared.status=loaded.status==MergeSaveStatus::Cancelled ? MergeSessionStatus::Cancelled : MergeSessionStatus::Error;
            result.prepared.message=loaded.message; return result;
        }
        MergeSessionInputs inputs; MergeResultSeed seed;
        seed.origin=ResultSeedOrigin::WorkingFile; seed.file.availability=MergeAvailability::Present;
        seed.file.bytes=loaded.bytes; seed.file.rawPath=QFile::encodeName(loaded.stamp->path);
        seed.file.label=loaded.stamp->path; seed.file.fileName=QFileInfo(path).fileName(); seed.fingerprint=loaded.stamp->fingerprint;
        if (launch.labels.size()>3) seed.file.label=launch.labels[3];
        inputs.resultSeed=std::move(seed);
        quint64 total=loaded.bytes.size();
        if (!launch.localPath.isEmpty()) {
            for (int i=0; i<3; ++i) {
                auto& source=i==0 ? inputs.base : i==1 ? inputs.ours : inputs.theirs;
                if (i==0 && launch.baseAbsent) { source.availability=MergeAvailability::Absent; source.label=QStringLiteral("absent BASE"); continue; }
                const auto sourcePath=i==0 ? launch.basePath : i==1 ? launch.localPath : launch.remotePath;
                MergeSessionLimits remaining; remaining.maxInputBytes-=total;
                const auto file=readMergeResultFile(sourcePath,remaining,cancellation);
                if (file.status!=MergeSaveStatus::Saved || !file.stamp) {
                    result.prepared.status=file.status==MergeSaveStatus::Cancelled ? MergeSessionStatus::Cancelled : MergeSessionStatus::Error;
                    result.prepared.message=file.message; return result;
                }
                total+=file.bytes.size(); source.availability=MergeAvailability::Present; source.bytes=file.bytes;
                source.rawPath=QFile::encodeName(file.stamp->path); source.sourceId=file.stamp->fingerprint;
                source.fileName=QFileInfo(sourcePath).fileName();
                const auto role=i==0 ? QStringLiteral("BASE") : i==1 ? QStringLiteral("LOCAL") : QStringLiteral("REMOTE");
                source.label=launch.labels.size()>i ? launch.labels[i] : role+QStringLiteral(" — ")+source.fileName;
            }
        }
        if (launch.localPath.isEmpty()) inputs.base.fileName=inputs.ours.fileName=inputs.theirs.fileName=QFileInfo(path).fileName();
        result.prepared=prepareMergeSession(inputs,{},cancellation); result.stamp=loaded.stamp; return result;
    }));
    dialog.exec(); cancellation.requestCancellation(); dialog.saveCancellation.requestCancellation();
    if (launch.readOnly || !dialog.resolvedSaved || !dialog.savedStamp || !dialog.merge || dialog.merge->isModified()) return 1;
    const auto verified=readMergeResultFile(dialog.savedStamp->path);
    if (verified.status!=MergeSaveStatus::Saved || !verified.stamp || !sameMergeSaveStamp(*verified.stamp,*dialog.savedStamp)) {
        QTextStream(stderr)<<"RESULT changed after the resolved save; merge completion refused\n"; return 1;
    }
    return 0;
}

} // namespace diffmerge::gui::desktop
