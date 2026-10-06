#include <diffmerge/FileDiffWidget.h>
#include "FileEditingState.h"
#include "DiffConnectorSplitter.h"
#include <diffmerge/DiffEditor.h>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPromise>
#include <QProgressBar>
#include <QtConcurrent/QtConcurrentRun>
#include <array>

namespace diffmerge::gui {
namespace {
struct ByteResult {
    ByteComparisonStatus status = ByteComparisonStatus::Error;
    qint64 leftSize = 0, rightSize = 0;
    QString message;
};
void compareBytes(QPromise<ByteResult>& promise, QString left, QString right,
                  diffcore::CancellationToken cancellation) {
    ByteResult result;
    promise.setProgressRange(0,1000);
    const auto stopped = [&] { return cancellation.isCancellationRequested() || promise.isCanceled(); };
    const auto finish = [&](ByteComparisonStatus status, const QString& message = {}) {
        result.status=status; result.message=message; promise.addResult(result);
    };
    if(stopped()) { finish(ByteComparisonStatus::Cancelled); return; }
    QFile a(left), b(right);
    std::array<QFileInfo,2> initial{QFileInfo(left),QFileInfo(right)};
    std::array<QString,2> canonical;
    std::array<QDateTime,2> modified;
    std::array<qint64,2> sizes{};
    for(int i=0;i<2;++i) {
        auto& file=i==0 ? a : b;
        if(file.fileName().isEmpty()) continue;
        if(!initial[i].isFile() || !file.open(QIODevice::ReadOnly)) {
            finish(ByteComparisonStatus::Error,QStringLiteral("Cannot read compared file: %1").arg(file.fileName())); return;
        }
        sizes[i]=initial[i].size(); canonical[i]=initial[i].canonicalFilePath(); modified[i]=initial[i].lastModified();
    }
    result.leftSize=sizes[0]; result.rightSize=sizes[1];
    bool identical=sizes[0]==sizes[1];
    qint64 processed=0;
    while(identical && processed<sizes[0]) {
        if(stopped()) { finish(ByteComparisonStatus::Cancelled); return; }
        const auto count=std::min<qint64>(64*1024,sizes[0]-processed);
        const auto x=a.read(count), y=b.read(count);
        if(x.size()!=count || y.size()!=count || a.error()!=QFileDevice::NoError || b.error()!=QFileDevice::NoError) {
            finish(ByteComparisonStatus::Error,QStringLiteral("File changed or read failed during byte comparison")); return;
        }
        identical=x==y; processed+=count;
        const int progress=int(1000.0L*processed/sizes[0]);
        promise.setProgressValueAndText(progress,QStringLiteral("Comparing bytes: %1 / %2 bytes per file").arg(processed).arg(sizes[0]));
    }
    if(stopped()) { finish(ByteComparisonStatus::Cancelled); return; }
    for(int i=0;i<2;++i) {
        const auto& file=i==0 ? a : b;
        if(file.fileName().isEmpty()) continue;
        const QFileInfo current(file.fileName());
        if(current.size()!=sizes[i] || current.lastModified()!=modified[i] || current.canonicalFilePath()!=canonical[i]) {
            finish(ByteComparisonStatus::Error,QStringLiteral("File changed during byte comparison; refresh again")); return;
        }
    }
    promise.setProgressValue(1000);
    finish(identical ? ByteComparisonStatus::Identical : ByteComparisonStatus::Different);
}
}
void FileDiffWidget::cancelByteComparison() {
    if(m_byteStatus==ByteComparisonStatus::Comparing) m_editing->cancellation.requestCancellation();
}
bool FileDiffWidget::loadByteComparison(const QString& leftPath, const QString& rightPath) {
    const auto prepared=prepareComparison({}, {}, m_options);
    if(prepared.status!=PreparationStatus::Ready) { emit loadFailed(prepared.message); return false; }
    setComparison(prepared.comparison);
    m_binaryInput=true; m_byteStatus=ByteComparisonStatus::Comparing;
    setPaths(leftPath,rightPath);
    setSaveTarget(Side::Left,leftPath); setSaveTarget(Side::Right,rightPath);
    m_editing->unsafe={true,true}; updateEditability();
    m_binaryNotice->setText(QStringLiteral("Comparing bytes…")); m_binaryNotice->show();
    m_binaryProgress->setValue(0); m_binaryProgress->show(); m_binaryCancel->show();
    m_splitter->hide(); m_unifiedEditor->hide();
    m_editing->pending=true; m_editing->cancellation={};
    const auto generation=m_editing->generation;
    auto* watcher=new QFutureWatcher<ByteResult>(this);
    connect(watcher,&QFutureWatcher<ByteResult>::progressValueChanged,this,[this,watcher,generation](int value) {
        if(generation!=m_editing->generation) return;
        m_binaryProgress->setValue(value);
        const auto progress=watcher->future().progressText();
        if(!progress.isEmpty()) m_binaryNotice->setText(progress);
        emit byteComparisonProgress(value,progress);
    });
    connect(watcher,&QFutureWatcher<ByteResult>::finished,this,[this,watcher,generation] {
        const auto result=watcher->result(); watcher->deleteLater();
        if(generation!=m_editing->generation) return;
        m_editing->pending=false; m_byteStatus=result.status;
        m_binaryProgress->hide(); m_binaryCancel->hide();
        if(result.status==ByteComparisonStatus::Identical || result.status==ByteComparisonStatus::Different)
            m_binaryNotice->setText(QStringLiteral("Byte comparison: files %1 — left %2 bytes, right %3 bytes")
                .arg(result.status==ByteComparisonStatus::Identical ? QStringLiteral("are identical") : QStringLiteral("differ"))
                .arg(result.leftSize).arg(result.rightSize));
        else if(result.status==ByteComparisonStatus::Cancelled)
            m_binaryNotice->setText(QStringLiteral("Byte comparison cancelled — result unknown"));
        else m_binaryNotice->setText(result.message);
        emit byteComparisonFinished(result.status);
    });
    watcher->setFuture(QtConcurrent::run(compareBytes,leftPath,rightPath,m_editing->cancellation));
    emit editableChanged(Side::Left,false); emit editableChanged(Side::Right,false);
    emit pathsChanged(leftPath,rightPath);
    return true;
}
}
