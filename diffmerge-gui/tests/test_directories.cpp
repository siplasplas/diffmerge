#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <diffmerge/DirDiffWidget.h>
#include <diffmerge/DirectoryOperations.h>
#ifdef DIFFMERGE_TEST_LAUNCH
#include "../src/LaunchOptions.h"
#endif
using namespace diffmerge::gui;

class TestDirectories : public QObject {
    Q_OBJECT
    static bool write(const QString& path, const QByteArray& bytes) {
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static QByteArray read(const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
    static const DirDiffEntry* find(const DirectoryScanResult& result, const QString& path) {
        for (const auto& entry : result.entries) if (entry.relativePath == path) return &entry;
        return nullptr;
    }
private slots:
    void swappingKeepsDirectoryAndPermissions() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const auto left = temporary.filePath("left"), right = temporary.filePath("right");
        QVERIFY(write(left + "/nested/only", "left")); QVERIFY(QDir().mkpath(right + "/nested"));
        DirDiffWidget widget; widget.setReadOnly(Side::Right, false);
        widget.setDirectories(left, right); QTRY_VERIFY(!widget.isScanning());
        QVERIFY(widget.navigateInto("nested"));
        auto* view = widget.findChild<QTableView*>("directoryTable"); QVERIFY(view);
        view->setCurrentIndex(view->model()->index(1, 0));
        widget.swapSides(); QTRY_VERIFY(!widget.isScanning());
        QCOMPARE(widget.leftPath(), right); QCOMPARE(widget.rightPath(), left);
        QCOMPARE(widget.currentRelativeDirectory(), QString("nested"));
        QVERIFY(!widget.isReadOnly(Side::Left)); QVERIFY(widget.isReadOnly(Side::Right));
        QCOMPARE(view->currentIndex().data().toString(), QString("only"));
        QCOMPARE(view->model()->index(1, 3).data().toString(), QString("only right"));
        widget.swapSides(); QTRY_VERIFY(!widget.isScanning()); QCOMPARE(widget.leftPath(), left);
    }
    void byteProgressAndCancellation() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const auto left=temporary.filePath("left"),right=temporary.filePath("right");
        QVERIFY(write(left+"/large",QByteArray(128*1024,'a')));
        QVERIFY(write(right+"/large",QByteArray(128*1024,'a')));
        DirectoryScanOptions options; options.maxComparedFileBytes=1;
        int progressCalls=0;
        const auto result=scanDirectories(left,right,options,{},[&](const QString&,qint64 done,qint64 total) {
            if(done>0) { ++progressCalls; QVERIFY(done<=total); }
        });
        QCOMPARE(result.status,DirectoryScanStatus::Ready); QVERIFY(progressCalls>0);
        QCOMPARE(find(result,"large")->status,DirEntryStatus::Same); QVERIFY(find(result,"large")->contentVerified);
        diffcore::CancellationToken token;
        const auto cancelled=scanDirectories(left,right,options,token,[&](const QString&,qint64 done,qint64) {
            if(done>0) token.requestCancellation();
        });
        QCOMPARE(cancelled.status,DirectoryScanStatus::Cancelled); QVERIFY(cancelled.entries.isEmpty());
        DirDiffWidget widget; QSignalSpy finished(&widget,&DirDiffWidget::scanFinished);
        widget.setDirectories(left,right); widget.cancelScan();
        QTRY_COMPARE(finished.size(),1); QVERIFY(!widget.isScanning());
    }
    void contentAndDirectorySummaries() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const auto left = temporary.filePath("left"), right = temporary.filePath("right");
        QVERIFY(write(left+"/changed", "abcd")); QVERIFY(write(right+"/changed", "abce"));
        QVERIFY(write(left+"/equal", "equal")); QVERIFY(write(right+"/equal", "equal"));
        QVERIFY(write(left+"/nested/file", "a")); QVERIFY(write(right+"/nested/file", "b"));
        QVERIFY(write(left+"/only/sub/file", "x"));
        QVERIFY(write(left+"/.git/ignored", "excluded")); QVERIFY(write(right+"/.hidden", "visible"));
        QVERIFY(write(left+"/Case", "upper")); QVERIFY(write(right+"/case", "lower"));
        const auto time=QDateTime::fromSecsSinceEpoch(1700000000);
        for(const auto& path : {left+"/changed",right+"/changed"}) {
            QFile file(path); QVERIFY(file.open(QIODevice::ReadWrite));
            QVERIFY(file.setFileTime(time,QFileDevice::FileModificationTime));
        }
        QFile equal(left+"/equal"); QVERIFY(equal.open(QIODevice::ReadWrite));
        QVERIFY(equal.setFileTime(time,QFileDevice::FileModificationTime)); equal.close();
        const auto result = scanDirectories(left,right);
        QCOMPARE(result.status,DirectoryScanStatus::Ready);
        QVERIFY(find(result,"changed")); QCOMPARE(find(result,"changed")->status,DirEntryStatus::Different);
        QCOMPARE(find(result,"equal")->status,DirEntryStatus::Same);
        QVERIFY(find(result,"equal")->contentVerified);
        QCOMPARE(find(result,"nested")->status,DirEntryStatus::Different);
        QCOMPARE(find(result,"only/sub/file")->status,DirEntryStatus::OnlyLeft);
        QVERIFY(!find(result,".git")); QVERIFY(find(result,".hidden"));
        QCOMPARE(find(result,"Case")->status,DirEntryStatus::OnlyLeft);
        QCOMPARE(find(result,"case")->status,DirEntryStatus::OnlyRight);
        auto limited = DirectoryScanOptions{}; limited.maxComparedFileBytes=1;
        const auto metadata = scanDirectories(left,right,limited);
        QVERIFY(find(metadata,"equal")->contentVerified);
        QCOMPARE(find(metadata,"changed")->status,DirEntryStatus::Different);
        QVERIFY(find(metadata,"changed")->contentVerified);
        QCOMPARE(scanDirectories(left,right,{.exclusions={},.maxComparedFileBytes=64,.maxEntries=1}).status,DirectoryScanStatus::ResourceLimit);
        diffcore::CancellationToken token; token.requestCancellation();
        QCOMPARE(scanDirectories(left,right,{},token).status,DirectoryScanStatus::Cancelled);
        QCOMPARE(scanDirectories(left,temporary.filePath("missing")).status,DirectoryScanStatus::Error);
    }
    // The same Polish text in cp1250 and UTF-8 (with BOM): byte-different;
    // with normalization each file is decoded in its own encoding, and the
    // encoding is a difference unless ignoreEncoding is set.
    void encodingsInDirectoryComparison() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const auto left=temporary.filePath("left"), right=temporary.filePath("right");
        const QByteArray cp1250("Za\xBF\xF3\xB3\xE6 g\xEA\x9Cl\xB9 ja\x9F\xF1, \xBF\xF3\xB3w na \x9Cwie\xBFym kwiatku\r\n"
                                "pszcz\xF3\xB3ka siedzia\xB3" "a i zbiera\xB3" "a mi\xF3" "d\r\n");
        const QByteArray utf8=QByteArray("\xEF\xBB\xBF")+QStringLiteral("Zażółć gęślą jaźń, żółw na świeżym kwiatku\n"
                                                                        "pszczółka siedziała i zbierała miód\n").toUtf8();
        QVERIFY(write(left+"/text",cp1250)); QVERIFY(write(right+"/text",utf8));
        // The same cp1250 file on both sides, one with LF: equal ignoring line endings.
        QVERIFY(write(left+"/legacy",cp1250)); QVERIFY(write(right+"/legacy",QByteArray(cp1250).replace("\r\n","\n")));
        QCOMPARE(find(scanDirectories(left,right),"text")->status,DirEntryStatus::Different);
        DirectoryScanOptions lineEndings; lineEndings.ignoreLineEndings=true;
        const auto normalized=scanDirectories(left,right,lineEndings);
        QCOMPARE(find(normalized,"text")->status,DirEntryStatus::Different);
        QCOMPARE(find(normalized,"legacy")->status,DirEntryStatus::Same);
        DirectoryScanOptions encoding; encoding.ignoreEncoding=true;
        QCOMPARE(find(scanDirectories(left,right,encoding),"text")->status,DirEntryStatus::Different); // CRLF vs LF
        encoding.ignoreLineEndings=true;
        QCOMPARE(find(scanDirectories(left,right,encoding),"text")->status,DirEntryStatus::Same);
    }
    void tableNavigationFiltersAndReadOnlyRequests() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const auto left=temporary.filePath("left"), right=temporary.filePath("right");
        QVERIFY(write(left+"/same/file","same")); QVERIFY(write(right+"/same/file","same"));
        QVERIFY(write(left+"/only/nested/file","only")); QVERIFY(write(right+"/right","right"));
        DirDiffWidget widget; QSignalSpy finished(&widget,&DirDiffWidget::scanFinished);
        QSignalSpy copy(&widget,&DirDiffWidget::copyRequested);
        widget.setDirectories(left,right); widget.show();
        QTRY_COMPARE(finished.size(),1);
        auto* view=widget.findChild<QTableView*>("directoryTable"); QVERIFY(view);
        auto* model=view->model(); QCOMPARE(model->columnCount(),6);
        QVERIFY(model->index(0,0).data().toString()!="..");
        QVERIFY(widget.navigateInto("only")); QCOMPARE(widget.currentRelativeDirectory(),QString("only"));
        QCOMPARE(model->index(0,0).data().toString(),QString(".."));
        QVERIFY(widget.navigateInto("nested")); QCOMPARE(widget.currentRelativeDirectory(),QString("only/nested"));
        QTest::keyClick(view,Qt::Key_Backspace); QCOMPARE(widget.currentRelativeDirectory(),QString("only"));
        QCOMPARE(view->currentIndex().data().toString(),QString("nested"));
        widget.navigateUp(); QCOMPARE(view->currentIndex().data().toString(),QString("only"));
        widget.setDifferencesOnly(true);
        for(int i=0;i<model->rowCount();++i) QVERIFY(model->index(i,0).data().toString()!="same");
        view->setCurrentIndex(model->index(0,0));
        QTest::keyClick(view,Qt::Key_F5); QCOMPARE(copy.size(),0);
        widget.setReadOnly(Side::Right,false); QTest::keyClick(view,Qt::Key_F5); QCOMPARE(copy.size(),1);
        widget.setExclusions({"only"}); QTRY_COMPARE(finished.size(),2);
        for(int i=0;i<model->rowCount();++i) QVERIFY(model->index(i,0).data().toString()!="only");
        widget.setExclusions({}); widget.setDirectories(left,right); QTRY_VERIFY(!widget.isScanning());
        QVERIFY(widget.navigateInto("same"));
        QTest::keyClick(view,Qt::Key_R,Qt::ControlModifier); QTRY_VERIFY(!widget.isScanning());
        QCOMPARE(widget.currentRelativeDirectory(),QString("same"));
    }
    void copyPlanIsExplicitAndSafe() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const auto left=temporary.filePath("left"),right=temporary.filePath("right");
        QVERIFY(write(left+"/folder/file","new")); QVERIFY(write(right+"/folder/file","old"));
        QVERIFY(write(left+"/folder/child","child"));
        const auto plan=planDirectoryCopy(left,right,{"folder","folder/file"});
        QVERIFY2(plan.error.isEmpty(),qPrintable(plan.error)); QCOMPARE(plan.items.size(),3);
        int overwrites=0; for(const auto& item:plan.items) overwrites+=item.overwrites;
        QCOMPARE(overwrites,1); QCOMPARE(read(right+"/folder/file"),QByteArray("old"));
        QVERIFY(executeDirectoryCopy(plan).isEmpty());
        QCOMPARE(read(right+"/folder/file"),QByteArray("new")); QCOMPARE(read(right+"/folder/child"),QByteArray("child"));
        QVERIFY(!planDirectoryCopy(left,right,{"../outside"}).error.isEmpty());
        QVERIFY(!planDirectoryCopy(left,left,{"folder"}).error.isEmpty());
        QVERIFY(write(right+"/collision","file")); QVERIFY(write(left+"/collision/child","child"));
        QVERIFY(!planDirectoryCopy(left,right,{"collision"}).error.isEmpty());
        QVERIFY(!trashDirectoryEntries(right,{"../outside"}).isEmpty());
        auto stale=planDirectoryCopy(left,right,{"folder/file"});
        QVERIFY(write(left+"/folder/file","changed length"));
        QVERIFY(!executeDirectoryCopy(stale).isEmpty()); QCOMPARE(read(right+"/folder/file"),QByteArray("new"));
        QVERIFY(QFile::link(left+"/folder",right+"/link"));
        QVERIFY(!planDirectoryCopy(left,right,{"link/child"}).error.isEmpty());
        QVERIFY(!planDirectoryCopy(right,left,{"link"}).error.isEmpty());
    }
#ifdef DIFFMERGE_TEST_LAUNCH
    void launchPathValidation() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const auto file=temporary.filePath("file"); QVERIFY(write(file,"content"));
        const auto second=temporary.filePath("second"); QVERIFY(write(second,"content"));
        QCOMPARE(validateLaunchPaths({}).kind,LaunchKind::Empty);
        QCOMPARE(validateLaunchPaths({file}).kind,LaunchKind::PrefillFile);
        QCOMPARE(validateLaunchPaths({temporary.path()}).kind,LaunchKind::PrefillDirectory);
        QCOMPARE(validateLaunchPaths({file,second},{"a.cpp","b.cpp"}).kind,LaunchKind::Files);
        QCOMPARE(validateLaunchPaths({temporary.path(),temporary.path()}).kind,LaunchKind::Directories);
        const auto mixed=validateLaunchPaths({file,temporary.path()}); QCOMPARE(mixed.kind,LaunchKind::Error);
        QVERIFY(mixed.error.contains(file)); QVERIFY(mixed.error.contains("directory"));
        QCOMPARE(validateLaunchPaths({file,temporary.filePath("missing")}).kind,LaunchKind::Error);
        QCOMPARE(validateLaunchPaths({file,second,file}).kind,LaunchKind::Error);
    }
#endif
};
QTEST_MAIN(TestDirectories)
#include "test_directories.moc"
