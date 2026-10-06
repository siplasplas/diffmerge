#include <QApplication>
#include <QImage>
#include <QScrollBar>
#include <QSplitterHandle>
#include <QTest>
#include <QVBoxLayout>
#include <QPalette>
#include <QShortcut>
#include <QSignalSpy>
#include <QSignalBlocker>
#include <QTemporaryDir>
#include <QFile>
#include <QUndoStack>
#ifdef DIFFMERGE_TEST_VIEW_MENU
#include "../src/MainWindow.h"
#include <QMenuBar>
#include <QMenu>
#include <QSettings>
#include <QSlider>
#include <QMessageBox>
#include <QTimer>
#include <QPushButton>
#endif
#include <QDir>
#include <qce/kate/KatePaths.h>
#include "../src/editor/SyntaxLoader.h"
#include <future>
#include <limits>
#include <qce/ExtraSelection.h>

#include <diffcore/DiffEngine.h>
#include <qce/CodeEditArea.h>

#include <diffmerge/DiffEditor.h>
#include "../src/fileview/DiffConnectorSplitter.h"
#include <diffmerge/FileDiffWidget.h>
#include <diffmerge/DirDiffWidget.h>

using namespace diffmerge::gui;
using diffcore::ChangeType;

class TestDiffEditor : public QObject {
    Q_OBJECT

    static QColor pixelAt(DiffEditor& editor, int y) {
        const QImage image = editor.edit()->area()->viewport()->grab().toImage();
        const qreal scale = image.devicePixelRatio();
        return image.pixelColor(qRound((editor.edit()->area()->viewport()->width() - 20) * scale),
                                qRound(y * scale));
    }

    static void showEditor(DiffEditor& editor, const AlignedLineModel& model,
                           const ColorScheme& scheme) {
        editor.resize(400, 200);
        editor.setColorScheme(scheme);
        editor.setAlignedModel(&model);
        editor.show();
        QApplication::processEvents();
    }

    static int colorHeight(const QImage& image, int x, const QColor& color) {
        int count = 0;
        for (int y = 0; y < image.height(); ++y)
            if (image.pixelColor(x, y) == color) ++count;
        return count;
    }

private slots:
    void swappingPreservesDocumentsUndoAndTargets() {
        FileDiffWidget widget; widget.setEditable(Side::Right, true);
        widget.setComparison(prepareComparison(TextSnapshot::fromText("left\n", "Left label", "left.cpp"),
            TextSnapshot::fromText("right\n", "Right label", "right.py")).comparison);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto leftPath = directory.filePath("left"), rightPath = directory.filePath("right");
        widget.setSaveTarget(Side::Left, leftPath); widget.setSaveTarget(Side::Right, rightPath);
        auto* editor = widget.rightEditor(); auto* area = editor->edit()->area();
        auto* document = area->document();
        area->setCursorPosition({0, 5}); QTest::keyClicks(area, " changed");
        const auto edited = widget.text(Side::Right); const auto undoCount = area->undoStack()->count();
        QVERIFY(widget.isRecomputing()); QVERIFY(widget.swapSides());
        QCOMPARE(widget.leftEditor(), editor); QCOMPARE(area->document(), document);
        QCOMPARE(area->undoStack()->count(), undoCount); QCOMPARE(editor->side(), Side::Left);
        QCOMPARE(widget.text(Side::Left), edited); QVERIFY(widget.isModified(Side::Left));
        QVERIFY(!widget.isModified(Side::Right)); QVERIFY(widget.isEditable(Side::Left));
        QVERIFY(!widget.isEditable(Side::Right)); QCOMPARE(widget.saveTarget(Side::Left), rightPath);
        QCOMPARE(widget.comparison()->snapshot(Side::Left).label, QString("Right label"));
        QCOMPARE(editor->syntaxFileName(), QString("right.py"));
        area->undo(); QTRY_VERIFY(!widget.isRecomputing()); QVERIFY(!widget.isModified(Side::Left));
        QCOMPARE(widget.text(Side::Left), QString("right\n"));
        QVERIFY(widget.copyChange(0, Side::Right)); QTRY_VERIFY(!widget.isRecomputing());
        QCOMPARE(widget.text(Side::Left), QString("left\n"));
        QVERIFY(widget.save(Side::Left)); QFile saved(rightPath); QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), QByteArray("left\n")); QVERIFY(!QFile::exists(leftPath));
        QVERIFY(widget.swapSides()); QCOMPARE(widget.rightEditor(), editor);
    }
    void swappingProjectedAndBinaryViews() {
        FileDiffWidget widget; widget.setContent({"old"}, {"new"});
        widget.setViewMode(ViewMode::Unified); QVERIFY(widget.swapSides());
        QCOMPARE(widget.comparison()->snapshot(Side::Left).lines, QStringList{"new"});
        widget.setViewMode(ViewMode::SideBySide); widget.setUnchangedLinesSkipped(true);
        QVERIFY(widget.swapSides()); QCOMPARE(widget.text(Side::Left), QString("old"));
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto left = directory.filePath("left"), right = directory.filePath("right");
        for (const auto& path : {left, right}) {
            QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(QByteArray("a\0b", 3)), qint64(3));
        }
        QVERIFY(widget.loadFromPaths(left, right)); QVERIFY(widget.swapSides());
        QCOMPARE(widget.saveTarget(Side::Left), right); QCOMPARE(widget.saveTarget(Side::Right), left);
        QTRY_COMPARE(widget.byteComparisonStatus(), ByteComparisonStatus::Identical);
    }
    void highlightDetailChangesPreserveEditsAndUndo() {
        FileDiffWidget widget; widget.setEditable(Side::Right,true);
        widget.setContent({"prefix alpha_name suffix"},{"prefix alpha_game suffix"});
        auto* area=widget.rightEditor()->edit()->area(); auto* document=area->document();
        area->setCursorPosition({0,17}); QTest::keyClicks(area,"x");
        const auto edited=widget.text(Side::Right); const int undo=area->undoStack()->count();
        widget.setHighlightDetail(IntraLineDiffEngine::Detail::WholeWords);
        QTRY_VERIFY(!widget.isRecomputing());
        QCOMPARE(area->document(),document); QCOMPARE(area->undoStack()->count(),undo);
        QCOMPARE(widget.text(Side::Right),edited); QVERIFY(widget.isModified(Side::Right));
        QCOMPARE(widget.comparison()->options().highlightDetail,IntraLineDiffEngine::Detail::WholeWords);
        const auto ranges=widget.comparison()->highlights().rightRanges[0];
        QCOMPARE(ranges.size(),1); QCOMPARE(ranges[0].start,7); QCOMPARE(ranges[0].length,11);
        widget.setHighlightDetail(IntraLineDiffEngine::Detail::Characters); QTRY_VERIFY(!widget.isRecomputing());
        QCOMPARE(widget.text(Side::Right),edited); QCOMPARE(area->undoStack()->count(),undo);
        area->undo(); QTRY_VERIFY(!widget.isRecomputing()); QVERIFY(!widget.isModified(Side::Right));
        widget.setHighlightDetail(IntraLineDiffEngine::Detail::WholeWords); QTRY_VERIFY(!widget.isRecomputing());
        widget.setContent({"alpha_name"},{"alpha_game"});
        QCOMPARE(widget.highlightDetail(),IntraLineDiffEngine::Detail::WholeWords);
        QCOMPARE(widget.comparison()->highlights().leftRanges[0][0].length,10);
        widget.setViewMode(ViewMode::Unified);
        widget.setHighlightDetail(IntraLineDiffEngine::Detail::Characters); QTRY_VERIFY(!widget.isRecomputing());
        QCOMPARE(widget.viewMode(),ViewMode::Unified);
        QCOMPARE(widget.comparison()->highlights().leftRanges[0][0].length,1);
    }
    void largeFilesCompareBytesWithProgressAndCancellation() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto left=dir.filePath("left.bin"), right=dir.filePath("right.bin");
        const auto write=[](const QString& path,const QByteArray& bytes) {
            QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size();
        };
        QByteArray bytes(8*1024*1024+1,'a');
        QVERIFY(write(left,bytes)); QVERIFY(write(right,bytes));
        FileDiffWidget widget; widget.setEditable(Side::Right,true);
        QSignalSpy progress(&widget,&FileDiffWidget::byteComparisonProgress);
        QVERIFY(widget.loadFromPaths(left,right));
        QCOMPARE(widget.byteComparisonStatus(),ByteComparisonStatus::Comparing);
        QVERIFY(!widget.isEditable(Side::Right));
        QTRY_COMPARE(widget.byteComparisonStatus(),ByteComparisonStatus::Identical);
        QVERIFY(!widget.isRecomputing()); QVERIFY(!progress.isEmpty());
        QVERIFY(widget.findChild<QLabel*>("binaryComparison")->text().contains("are identical"));
        bytes[bytes.size()-1]='b'; QVERIFY(write(right,bytes));
        QVERIFY(widget.reloadSide(Side::Right));
        QTRY_COMPARE(widget.byteComparisonStatus(),ByteComparisonStatus::Different);
        QVERIFY(widget.findChild<QLabel*>("binaryComparison")->text().contains("differ"));
        QVERIFY(write(left,QByteArray(200001,'a'))); QVERIFY(write(right,QByteArray(200001,'a')));
        QVERIFY(widget.loadFromPaths(left,right));
        QTRY_COMPARE(widget.byteComparisonStatus(),ByteComparisonStatus::Identical);
        for(const auto& path : {left,right}) {
            QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.resize(qint64(1024)*1024*1024));
        }
        QVERIFY(widget.loadFromPaths(left,right));
        widget.findChild<QToolButton*>("cancelByteComparison")->click();
        QTRY_COMPARE(widget.byteComparisonStatus(),ByteComparisonStatus::Cancelled);
        QVERIFY(widget.findChild<QLabel*>("binaryComparison")->text().contains("unknown"));
        QVERIFY(widget.loadFromPaths(left,right));
        widget.setContent({"same"},{"same"});
        QCOMPARE(widget.byteComparisonStatus(),ByteComparisonStatus::NotApplicable);
        QTest::qWait(20);
        QCOMPARE(widget.byteComparisonStatus(),ByteComparisonStatus::NotApplicable);
        QCOMPARE(widget.text(Side::Left),QString("same"));
    }
    void binaryDetectionAndReloadPreserveDirtyText() {
        QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto left=dir.filePath("left.txt"), right=dir.filePath("right.txt");
        const auto write=[](const QString& path,const QByteArray& bytes) {
            QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes)==bytes.size();
        };
        QVERIFY(write(left,"text\n")); QVERIFY(write(right,"text\n"));
        FileDiffWidget widget; widget.setEditable(Side::Right,true); QVERIFY(widget.loadFromPaths(left,right));
        auto* area=widget.rightEditor()->edit()->area(); area->setCursorPosition({0,0}); QTest::keyClicks(area,"X");
        const auto edited=widget.text(Side::Right);
        QVERIFY(write(left,QByteArray("bin\0ary",7)));
        QVERIFY(!widget.reloadSide(Side::Left));
        QCOMPARE(widget.text(Side::Right),edited); QVERIFY(widget.isModified(Side::Right));
        widget.discardChanges(); QVERIFY(widget.reloadSide(Side::Left));
        QTRY_COMPARE(widget.byteComparisonStatus(),ByteComparisonStatus::Different);
        QVERIFY(write(right,QByteArray("bin\0ary",7))); QVERIFY(widget.loadFromPaths(left,right));
        QTRY_COMPARE(widget.byteComparisonStatus(),ByteComparisonStatus::Identical);
        QVERIFY(write(left,QByteArray(8001,'a')+QByteArray("\0",1)));
        QVERIFY(write(right,QByteArray(8001,'a')+QByteArray("\0",1)));
        QVERIFY(widget.loadFromPaths(left,right));
        QTRY_COMPARE(widget.byteComparisonStatus(),ByteComparisonStatus::Identical);
        QVERIFY(write(left,QByteArray("bin\0ary",7)));
        QVERIFY(widget.loadFromPaths(left,dir.filePath("missing")));
        QTRY_COMPARE(widget.byteComparisonStatus(),ByteComparisonStatus::Error);
        QVERIFY(!widget.isRecomputing());
    }
    void editingPreservesDocumentsAndUndo() {
        FileDiffWidget widget;
        widget.setContent({"old"}, {"new"}); widget.resize(850,300); widget.show();
        QVERIFY(!widget.isEditable(Side::Left)); QVERIFY(!widget.isEditable(Side::Right));
        widget.setEditable(Side::Right,true);
        auto* area=widget.rightEditor()->edit()->area();
        auto* document=area->document();
        area->setCursorPosition({0,3}); QTest::keyClicks(area," text");
        QVERIFY(widget.isModified(Side::Right)); QCOMPARE(widget.text(Side::Right),QString("new text"));
        const auto cursor=area->cursorPosition();
        const auto undoCount=area->undoStack()->count();
        QTRY_VERIFY(!widget.isRecomputing());
        QCOMPARE(area->document(),document); QCOMPARE(area->cursorPosition(),cursor);
        QCOMPARE(area->undoStack()->count(),undoCount);
        QCOMPARE(widget.comparison()->snapshot(Side::Right).lines,QStringList{"new text"});
        auto replacement=prepareComparison(TextSnapshot::fromText("a"),TextSnapshot::fromText("b"));
        widget.setComparison(replacement.comparison); QCOMPARE(widget.text(Side::Right),QString("new text"));
        widget.setViewMode(ViewMode::Unified); QCOMPARE(widget.viewMode(),ViewMode::SideBySide);
        area->undo(); QVERIFY(!widget.isModified(Side::Right));
        QTRY_VERIFY(!widget.isRecomputing());
        QCOMPARE(widget.text(Side::Right),QString("new"));
        area->redo(); QVERIFY(widget.isModified(Side::Right));
        widget.discardChanges(); QCOMPARE(widget.text(Side::Right),QString("new"));
        QTest::keyClicks(area,"first"); QTest::keyClicks(area,"second");
        const auto latest=widget.text(Side::Right); QTRY_VERIFY(!widget.isRecomputing());
        QCOMPARE(widget.comparison()->snapshot(Side::Right).lines,QStringList{latest});
    }
    void blockCopyIsOneUndoStep_data() {
        QTest::addColumn<QString>("left"); QTest::addColumn<QString>("right"); QTest::addColumn<bool>("fromLeft");
        for (bool left : {false,true}) {
            const auto suffix=left ? "-right" : "-left";
            QTest::newRow(qPrintable(QString("insert")+suffix)) << QString("head\n") << QString("head\nadded\n") << left;
            QTest::newRow(qPrintable(QString("delete")+suffix)) << QString("head\nremoved\n") << QString("head\n") << left;
            QTest::newRow(qPrintable(QString("replace")+suffix)) << QString("head\nold\n") << QString("head\nnew\n") << left;
        }
    }
    void blockCopyIsOneUndoStep() {
        QFETCH(QString,left); QFETCH(QString,right); QFETCH(bool,fromLeft);
        const auto source=fromLeft ? Side::Left : Side::Right, target=fromLeft ? Side::Right : Side::Left;
        FileDiffWidget widget;
        widget.setComparison(prepareComparison(TextSnapshot::fromText(left),TextSnapshot::fromText(right)).comparison);
        QVERIFY(!widget.copyChange(0,source)); widget.setEditable(target,true);
        const auto before=widget.text(target); auto* area=(target==Side::Left ? widget.leftEditor() : widget.rightEditor())->edit()->area();
        const auto count=area->undoStack()->count();
        QVERIFY(widget.copyChange(0,source)); QCOMPARE(widget.text(target),fromLeft ? left : right);
        QCOMPARE(area->undoStack()->count(),count+1); QTRY_VERIFY(!widget.isRecomputing()); QCOMPARE(widget.changeCount(),0);
        area->undo(); QCOMPARE(widget.text(target),before); QVERIFY(!widget.isModified(target));
        QTRY_VERIFY(!widget.isRecomputing()); QCOMPARE(widget.changeCount(),1);
    }
    void savingPreservesBytesAndRejectsUnsafeWrites() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto left=directory.filePath("left"), right=directory.filePath("right");
        const auto write=[](const QString& path,const QByteArray& bytes) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes)==bytes.size(); };
        const auto read=[](const QString& path) { QFile f(path); if(!f.open(QIODevice::ReadOnly)) return QByteArray{}; return f.readAll(); };
        QVERIFY(write(left,"old\n"));
        const QByteArray original=QByteArray("\xef\xbb\xbf")+"first\r\nsecond\nlast";
        QVERIFY(write(right,original)); QVERIFY(QFile::setPermissions(right,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        FileDiffWidget widget; widget.setEditable(Side::Right,true); QVERIFY(widget.loadFromPaths(left,right));
        auto* area=widget.rightEditor()->edit()->area(); area->setCursorPosition({0,5}); QTest::keyClicks(area,"!");
        QString error; QVERIFY2(widget.save(Side::Right,&error),qPrintable(error));
        QCOMPARE(read(right),QByteArray("\xef\xbb\xbf")+"first!\r\nsecond\nlast");
        QVERIFY(QFileInfo(right).permissions()&QFile::ExeOwner); QVERIFY(!widget.isModified(Side::Right));
        area->undo(); QVERIFY(widget.isModified(Side::Right)); area->redo(); QVERIFY(!widget.isModified(Side::Right));
        QVERIFY(write(right,"external")); QTest::keyClicks(area,"more"); QVERIFY(!widget.save(Side::Right,&error));
        QCOMPARE(read(right),QByteArray("external")); widget.discardChanges();
        QVERIFY(write(right,QByteArray("bad\xff",4))); QVERIFY(widget.loadFromPaths(left,right)); QVERIFY(!widget.isEditable(Side::Right));
        QVERIFY(!widget.save(Side::Right,&error)); QCOMPARE(read(right),QByteArray("bad\xff",4));
        widget.setEditable(Side::Left,true);
        QVERIFY(!widget.copyChange(0,Side::Right)); QCOMPARE(widget.text(Side::Left),QString("old\n"));
        QVERIFY(write(right,QByteArray("bin\0ary",7))); QVERIFY(widget.loadFromPaths(left,right));
        QVERIFY(!widget.isEditable(Side::Left) && !widget.isEditable(Side::Right));
        QTRY_VERIFY(widget.findChild<QLabel*>("binaryComparison")->text().contains("differ"));
        QVERIFY(widget.text(Side::Right).isEmpty());
        QVERIFY(write(right,"target\r\n")); const auto link=directory.filePath("link"); QVERIFY(QFile::link(right,link));
        QVERIFY(widget.loadFromPaths(left,link)); area->setCursorPosition({0,6}); QTest::keyClicks(area,"!");
        QVERIFY2(widget.save(Side::Right,&error),qPrintable(error)); QVERIFY(QFileInfo(link).isSymLink());
        QCOMPARE(read(right),QByteArray("target!\r\n"));
        QVERIFY(QFile::setPermissions(right,QFile::ReadOwner));
        QVERIFY(widget.loadFromPaths(left,right)); QVERIFY(!widget.isEditable(Side::Right));
        QVERIFY(!widget.save(Side::Right,&error));
        QVERIFY(QFile::setPermissions(right,QFile::ReadOwner|QFile::WriteOwner));
    }
    void connectorArrowsOnlyCopyTowardsEditableSides() {
        FileDiffWidget widget;
        widget.setContent({"old"},{"new"}); widget.resize(850,300); widget.show(); QApplication::processEvents();
        auto* splitter=widget.findChild<QSplitter*>(); auto* handle=splitter->handle(1);
        const auto viewport=widget.leftEditor()->edit()->area()->viewport();
        const int origin=handle->mapFromGlobal(viewport->mapToGlobal(QPoint(0,0))).y();
        const int y=origin+widget.leftEditor()->edit()->area()->viewportState().lineHeight/2;
        handle->grab(); QTest::mouseClick(handle,Qt::LeftButton,Qt::NoModifier,{handle->width()-11,y});
        QCOMPARE(widget.text(Side::Right),QString("new"));
        widget.setEditable(Side::Right,true); handle->grab();
        QTest::mouseMove(handle,{handle->width()-11,y});
        QCOMPARE(handle->cursor().shape(),Qt::ArrowCursor);
        QTest::mouseMove(handle,{handle->width()/2,handle->height()-10});
        QCOMPARE(handle->cursor().shape(),Qt::SplitHCursor);
        QTest::mouseClick(handle,Qt::LeftButton,Qt::NoModifier,{handle->width()-11,y});
        QCOMPARE(widget.text(Side::Right),QString("old")); QTRY_VERIFY(!widget.isRecomputing());
        QVERIFY(!widget.isModified(Side::Left));
    }
    void blockCopyKeepsIndependentlyScrolledViewports() {
        QStringList left;
        for(int n=0;n<300;++n) left.append(QStringLiteral("line %1 ").arg(n)+QString(300,'x'));
        auto right=left; right.insert(150,"extra 1"); right.insert(151,"extra 2");
        FileDiffWidget widget;
        widget.setContent(left,right); widget.setEditable(Side::Left,true);
        widget.resize(850,220); widget.show(); QApplication::processEvents();
        auto* leftArea=widget.leftEditor()->edit()->area(); auto* rightArea=widget.rightEditor()->edit()->area();
        leftArea->verticalScrollBar()->setValue(142);
        rightArea->verticalScrollBar()->setValue(146); QApplication::processEvents();
        leftArea->horizontalScrollBar()->setValue(40);
        auto* handle=widget.findChild<QSplitter*>()->handle(1);
        const auto l=leftArea->viewportState(), r=rightArea->viewportState();
        const int origin=handle->mapFromGlobal(leftArea->viewport()->mapToGlobal(QPoint(0,0))).y();
        const int y=origin+qRound(((150-l.firstVisibleLine)*l.lineHeight+l.contentOffsetY+
            (151-r.firstVisibleLine)*r.lineHeight+r.contentOffsetY)/2.0);
        QVERIFY(y>=origin && y<origin+leftArea->viewport()->height());
        QVERIFY(!handle->grab().isNull());
        const auto firstLeft=l.firstVisibleLine, firstRight=r.firstVisibleLine;
        const auto offsetLeft=l.contentOffsetY, offsetRight=r.contentOffsetY;
        const auto horizontalLeft=l.contentOffsetX, horizontalRight=r.contentOffsetX;
        QTest::mouseClick(handle,Qt::LeftButton,Qt::NoModifier,{11,y});
        QVERIFY(widget.isModified(Side::Left));
        QCOMPARE(leftArea->viewportState().firstVisibleLine,firstLeft);
        QCOMPARE(rightArea->viewportState().firstVisibleLine,firstRight);
        QTRY_VERIFY(!widget.isRecomputing());
        QCOMPARE(leftArea->viewportState().firstVisibleLine,firstLeft);
        QCOMPARE(rightArea->viewportState().firstVisibleLine,firstRight);
        QCOMPARE(leftArea->viewportState().contentOffsetY,offsetLeft);
        QCOMPARE(rightArea->viewportState().contentOffsetY,offsetRight);
        QCOMPARE(leftArea->viewportState().contentOffsetX,horizontalLeft);
        QCOMPARE(rightArea->viewportState().contentOffsetX,horizontalRight);
        leftArea->undo(); QTRY_VERIFY(!widget.isRecomputing());
        QCOMPARE(widget.comparison()->snapshot(Side::Left).lines,left);
    }
    void bothCopyArrowsPaintAboveNeighbouringConnectors() {
        QStringList left;
        for(int n=0;n<200;++n) left.append(QStringLiteral("line %1").arg(n));
        auto right=left; right[80]="changed 80"; right[85]="changed 85";
        FileDiffWidget widget; widget.setContent(left,right);
        widget.setEditable(Side::Left,true); widget.setEditable(Side::Right,true);
        widget.resize(850,500); widget.show(); QApplication::processEvents();
        QCOMPARE(widget.changeCount(),2);
        auto* l=widget.leftEditor()->edit()->area(); auto* r=widget.rightEditor()->edit()->area();
        {
            QSignalBlocker leftSignals(l),rightSignals(r);
            l->verticalScrollBar()->setValue(70); r->verticalScrollBar()->setValue(60);
        }
        auto* handle=widget.findChild<QSplitter*>()->handle(1);
        const int origin=handle->mapFromGlobal(l->viewport()->mapToGlobal(QPoint(0,0))).y();
        const auto lv=l->viewportState(), rv=r->viewportState();
        const int center=origin+qRound(((80.5-lv.firstVisibleLine)*lv.lineHeight+lv.contentOffsetY+
            (80.5-rv.firstVisibleLine)*rv.lineHeight+rv.contentOffsetY)/2.0);
        const QImage image=handle->grab().toImage(); const auto scale=image.devicePixelRatio();
        const auto background=handle->palette().button().color();
        // A later diagonal connector crosses the left button at this point.
        QCOMPARE(image.pixelColor(qRound(2*scale),qRound((center-6)*scale)),background);
        QCOMPARE(image.pixelColor(qRound((handle->width()-3)*scale),qRound((center-6)*scale)),background);
        QTest::mouseMove(handle,{11,center}); QCOMPARE(handle->cursor().shape(),Qt::ArrowCursor);
        QTest::mouseMove(handle,{handle->width()-11,center}); QCOMPARE(handle->cursor().shape(),Qt::ArrowCursor);
    }
    void panelSpacingChangesConnectorGeometry() {
        FileDiffWidget widget;
        widget.setContent({"unchanged", "old"}, {"unchanged", "new", "extra"});
        widget.resize(850, 300); widget.show(); QApplication::processEvents();
        auto* splitter = widget.findChild<QSplitter*>(); QVERIFY(splitter);
        QCOMPARE(widget.panelSpacing(), 48);
        for (int width : {8, 24, 80, 160}) {
            widget.setPanelSpacing(width); QApplication::processEvents();
            QCOMPARE(widget.panelSpacing(), width);
            QCOMPARE(splitter->handle(1)->width(), width);
            QVERIFY(widget.leftEditor()->width() > 0 && widget.rightEditor()->width() > 0);
            QVERIFY(!splitter->handle(1)->grab().isNull());
        }
        widget.setPanelSpacing(-10); QCOMPARE(widget.panelSpacing(), 8);
        widget.setPanelSpacing(1000); QCOMPARE(widget.panelSpacing(), 160);
        widget.setViewMode(ViewMode::Unified);
        widget.setPanelSpacing(32); widget.setViewMode(ViewMode::SideBySide);
        widget.resize(850, 180); QApplication::processEvents();
        QCOMPARE(splitter->handle(1)->width(), 32);
    }
    void horizontalScrollingSharesRangeAndOffset_data() {
        QTest::addColumn<bool>("longOnLeft");
        QTest::newRow("long-left") << true;
        QTest::newRow("long-right") << false;
    }
    void horizontalScrollingSharesRangeAndOffset() {
        QFETCH(bool, longOnLeft);
        FileDiffWidget widget;
        widget.resize(850, 300);
        const QStringList longLines{QString(300, 'x')}, shortLines{"short"};
        widget.setContent(longOnLeft ? longLines : shortLines, longOnLeft ? shortLines : longLines);
        widget.show(); QApplication::processEvents();
        auto* longArea = (longOnLeft ? widget.leftEditor() : widget.rightEditor())->edit()->area();
        auto* shortArea = (longOnLeft ? widget.rightEditor() : widget.leftEditor())->edit()->area();
        auto* longBar = longArea->horizontalScrollBar();
        auto* shortBar = shortArea->horizontalScrollBar();
        QVERIFY(longBar->maximum() > 100);
        QCOMPARE(shortBar->maximum(), longBar->maximum());
        longBar->setValue(80);
        QCOMPARE(longBar->value(), 80);
        QCOMPARE(shortBar->value(), 80);
        QCOMPARE(shortArea->viewportState().contentOffsetX, longArea->viewportState().contentOffsetX);
        shortBar->setValue(30);
        QCOMPARE(longBar->value(), 30);
        QCOMPARE(shortArea->viewportState().contentOffsetX, longArea->viewportState().contentOffsetX);
        longArea->setFocus(); longArea->setCursorPosition({0,0});
        QTest::keyClick(longArea, Qt::Key_End);
        QVERIFY(longBar->value() > 100);
        QCOMPARE(shortBar->value(), longBar->value());
        QCOMPARE(shortArea->viewportState().contentOffsetX, longArea->viewportState().contentOffsetX);
        widget.resize(1100, 400); QApplication::processEvents();
        QCOMPARE(shortBar->maximum(), longBar->maximum());
        QCOMPARE(shortBar->value(), longBar->value());
        QCOMPARE(shortArea->viewportState().contentOffsetX, longArea->viewportState().contentOffsetX);
        widget.findChild<QSplitter*>()->setSizes({300,700}); QApplication::processEvents();
        QCOMPARE(shortBar->maximum(), longBar->maximum());
        longBar->setValue(70); QCOMPARE(shortBar->value(), 70);
        widget.setUnchangedLinesSkipped(true); QApplication::processEvents();
        QCOMPARE(shortBar->value(), longBar->value());
        widget.setContent({"a"}, {"b"}); QApplication::processEvents();
        QCOMPARE(longBar->maximum(), 0); QCOMPARE(shortBar->maximum(), 0);
        QCOMPARE(longBar->value(), 0); QCOMPARE(shortBar->value(), 0);
    }

#ifdef DIFFMERGE_TEST_VIEW_MENU
    void desktopSaveButtonsSaveOneModifiedSideAtATime() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto name=QApplication::applicationName(), organization=QApplication::organizationName();
        const auto format=QSettings::defaultFormat();
        struct Restore {
            QString name, organization; QSettings::Format format;
            ~Restore() { QApplication::setApplicationName(name); QApplication::setOrganizationName(organization); QSettings::setDefaultFormat(format); }
        } restore{name,organization,format};
        QApplication::setApplicationName("SelectiveSaveTest"); QApplication::setOrganizationName("DiffMergeTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory.path());
        QSettings().setValue("syntaxDownloadDeclined",true);
        const auto left=directory.filePath("left"), right=directory.filePath("right");
        const auto write=[](const QString& path,const QByteArray& text) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(text)==text.size(); };
        const auto read=[](const QString& path) { QFile f(path); if(!f.open(QIODevice::ReadOnly)) return QByteArray{}; return f.readAll(); };
        QVERIFY(write(left,"left")); QVERIFY(write(right,"right"));
        MainWindow window; window.setEditMode(true,true); window.loadFiles(left,right);
        window.show(); QApplication::processEvents();
        auto* widget=window.findChild<FileDiffWidget*>(); QVERIFY(widget);
        auto* leftArea=widget->leftEditor()->edit()->area(); auto* rightArea=widget->rightEditor()->edit()->area();
        leftArea->setCursorPosition({0,4}); QTest::keyClicks(leftArea,"L");
        rightArea->setCursorPosition({0,5}); QTest::keyClicks(rightArea,"R");
        QVERIFY(widget->isModified(Side::Left) && widget->isModified(Side::Right));
        auto* leftSave=widget->findChild<QToolButton*>("saveLeftFile"); auto* rightSave=widget->findChild<QToolButton*>("saveRightFile");
        QVERIFY(leftSave && rightSave); QVERIFY(leftSave->isVisible() && rightSave->isVisible());
        QTest::mouseClick(leftSave,Qt::LeftButton);
        QCOMPARE(read(left),QByteArray("leftL")); QCOMPARE(read(right),QByteArray("right"));
        QVERIFY(!widget->isModified(Side::Left)); QVERIFY(widget->isModified(Side::Right));
        QVERIFY(!leftSave->isVisible()); QVERIFY(rightSave->isVisible());
        QTest::mouseClick(rightSave,Qt::LeftButton);
        QCOMPARE(read(right),QByteArray("rightR")); QVERIFY(!widget->isModified(Side::Right));
        QVERIFY(!rightSave->isVisible()); QVERIFY(window.close());
    }
    void desktopCloseCanCancelOrDiscardEdits() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto name=QApplication::applicationName(), organization=QApplication::organizationName();
        const auto format=QSettings::defaultFormat();
        struct Restore {
            QString name, organization; QSettings::Format format;
            ~Restore() { QApplication::setApplicationName(name); QApplication::setOrganizationName(organization); QSettings::setDefaultFormat(format); }
        } restore{name,organization,format};
        QApplication::setApplicationName("EditingWindowTest"); QApplication::setOrganizationName("DiffMergeTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory.path());
        QSettings().setValue("syntaxDownloadDeclined",true);
        MainWindow window;
        auto* widget=window.findChild<FileDiffWidget*>(); QVERIFY(widget);
        auto* directories=window.findChild<DirDiffWidget*>(); QVERIFY(directories);
        QVERIFY(directories->isReadOnly(Side::Left)); QVERIFY(!directories->isReadOnly(Side::Right));
        window.setEditMode(false,false);
        QVERIFY(directories->isReadOnly(Side::Left) && directories->isReadOnly(Side::Right));
        window.setEditMode(false,true);
        QVERIFY(!widget->isEditable(Side::Left)); QVERIFY(widget->isEditable(Side::Right));
        widget->setContent({"left"},{"right"}); window.show(); QApplication::processEvents();
        auto* area=widget->rightEditor()->edit()->area(); area->setCursorPosition({0,5}); QTest::keyClicks(area,"!");
        QVERIFY(widget->isModified(Side::Right)); QVERIFY(window.windowTitle().startsWith("* "));
        const auto answer=[](QMessageBox::StandardButton button) {
            QTimer::singleShot(0,[button] {
                auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                if(box) box->button(button)->click();
            });
        };
        answer(QMessageBox::Cancel); QVERIFY(!window.close()); QVERIFY(widget->isModified(Side::Right));
        answer(QMessageBox::Discard); QVERIFY(window.close()); QVERIFY(!widget->isModified(Side::Right));
        const auto target=directory.filePath("saved"); widget->setSaveTarget(Side::Right,target);
        window.show(); area->setCursorPosition({0,5}); QTest::keyClicks(area,"!");
        answer(QMessageBox::Save); QVERIFY(window.close()); QVERIFY(!widget->isModified(Side::Right));
        QFile saved(target); QVERIFY(saved.open(QIODevice::ReadOnly)); QCOMPARE(saved.readAll(),QByteArray("right!"));
    }
    void desktopViewMenuPersistsSelection() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto name=QApplication::applicationName(), organization=QApplication::organizationName();
        const auto format=QSettings::defaultFormat();
        struct Restore {
            QString name, organization; QSettings::Format format;
            ~Restore() { QApplication::setApplicationName(name); QApplication::setOrganizationName(organization); QSettings::setDefaultFormat(format); }
        } restore{name,organization,format};
        QApplication::setApplicationName("ViewMenuTest"); QApplication::setOrganizationName("DiffMergeTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,directory.path());
        {
            MainWindow window;
            QMenu* view=nullptr;
            for(auto* action:window.menuBar()->actions()) if(action->text()=="&View") view=action->menu();
            QVERIFY(view);
            QAction* unified=nullptr; QAction* side=nullptr; QAction* skip=nullptr;
            for(auto* action:view->actions()) {
                if(action->text()=="Unified") unified=action;
                if(action->text()=="Side by Side") side=action;
                if(action->text()=="Skip unchanged lines") skip=action;
            }
            QVERIFY(unified && side && skip); QVERIFY(side->isChecked());
            unified->trigger(); skip->trigger();
            auto* widget=window.findChild<FileDiffWidget*>(); QVERIFY(widget);
            auto* slider=window.findChild<QSlider*>("panelSpacingSlider"); QVERIFY(slider);
            slider->setValue(28); QCOMPARE(widget->panelSpacing(),28);
            auto* words=window.findChild<QAction*>("highlightWholeWords"); QVERIFY(words); QVERIFY(!words->isChecked());
            words->trigger(); QCOMPARE(widget->highlightDetail(),IntraLineDiffEngine::Detail::WholeWords);
            QCOMPARE(widget->viewMode(),ViewMode::Unified); QVERIFY(widget->unchangedLinesSkipped());
            QVERIFY(!side->isChecked());
        }
        MainWindow second;
        const auto* widget=second.findChild<FileDiffWidget*>(); QVERIFY(widget);
        QCOMPARE(widget->viewMode(),ViewMode::Unified); QVERIFY(widget->unchangedLinesSkipped());
        QCOMPARE(widget->panelSpacing(),28);
        QCOMPARE(widget->highlightDetail(),IntraLineDiffEngine::Detail::WholeWords);
        QVERIFY(second.findChild<QAction*>("highlightWholeWords")->isChecked());
    }
#endif

    void unifiedSyntaxComesFromWholeOriginalSides() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const QString previous=qce::kate::dataDir();
        struct Restore { QString path; ~Restore() { qce::kate::setDataDirOverride(path); } } restore{previous};
        qce::kate::setDataDirOverride(directory.path());
        QVERIFY(QDir(directory.path()).mkpath("syntax"));
        QFile definition(directory.filePath("syntax/test.xml")); QVERIFY(definition.open(QIODevice::WriteOnly));
        const QByteArray xml=R"XML(<language name="Projection Test" section="Tests" extensions="*.projection" version="1" kateversion="5.0">
<highlighting><contexts>
<context name="Normal" attribute="Normal" lineEndContext="#stay"><StringDetect String="/*" attribute="Comment" context="Comment"/><WordDetect String="if" attribute="Keyword"/></context>
<context name="Comment" attribute="Comment" lineEndContext="#stay"><StringDetect String="*/" attribute="Comment" context="#pop"/></context>
</contexts><itemDatas><itemData name="Normal" defStyleNum="dsNormal"/><itemData name="Comment" defStyleNum="dsComment"/><itemData name="Keyword" defStyleNum="dsKeyword"/></itemDatas></highlighting></language>)XML";
        QCOMPARE(definition.write(xml),xml.size()); definition.close();
        TextSnapshot left,right; left.lines={"/*","body","*/","if old"}; right.lines={"plain","body","*/","if new"};
        left.fileName=right.fileName="sample.projection";
        auto prepared=prepareComparison(left,right); QVERIFY(prepared.comparison);
        for(bool dark:{false,true}) {
            FileDiffWidget widget; widget.setComparison(prepared.comparison); widget.setViewMode(ViewMode::Unified);
            widget.unifiedEditor()->setColorScheme(dark ? ColorScheme::darkDefault() : ColorScheme::lightDefault());
            const auto* highlighter=widget.unifiedEditor()->edit()->area()->highlighter(); QVERIFY(highlighter);
            auto state=highlighter->initialState();
            QVector<QVector<qce::StyleSpan>> spans;
            const auto& rows=widget.unifiedEditor()->displayRows();
            for(int i=0;i<rows.size();++i) {
                QVector<qce::StyleSpan> lineSpans; qce::HighlightState next;
                highlighter->highlightLine(widget.unifiedEditor()->edit()->area()->document()->lineAt(i),state,lineSpans,next);
                spans.append(lineSpans); state=next;
            }
            QString language;
            auto syntax=loadSyntax(right.fileName,dark,language); QVERIFY(syntax);
            auto originalState=syntax->initialState(); QVector<qce::StyleSpan> body;
            for(int i=0;i<2;++i) { qce::HighlightState next; syntax->highlightLine(right.lines[i],originalState,body,next); originalState=next; }
            int bodyRow=-1, removedRow=-1, addedRow=-1;
            for(int i=0;i<rows.size();++i) {
                if(rows[i].rightLine==1) bodyRow=i;
                if(rows[i].leftLine==3 && rows[i].rightLine<0) removedRow=i;
                if(rows[i].rightLine==3) addedRow=i;
            }
            QVERIFY(bodyRow>=0 && removedRow>=0 && addedRow>=0);
            QVERIFY(!body.isEmpty() && !spans[bodyRow].isEmpty());
            QCOMPARE(highlighter->attributes()[spans[bodyRow][0].attributeId].foreground,syntax->attributes()[body[0].attributeId].foreground);
            // In one pane, old lines mark changed characters red and new lines green.
            for(int row:{removedRow,addedRow}) {
                bool strong=false, keyword=false;
                const auto& scheme=widget.unifiedEditor()->colorScheme();
                const QColor expected=row==removedRow ? scheme.removedCharBg : scheme.addedCharBg;
                for(const auto& span:spans[row]) {
                    const auto& attr=highlighter->attributes()[span.attributeId];
                    strong |= attr.background == expected;
                    keyword |= attr.bold;
                }
                QVERIFY(strong); QVERIFY(keyword);
            }
            QVERIFY(!widget.unifiedEditor()->edit()->area()->foldingProvider());
        }
    }

    void unifiedRowsAndOriginalMappings() {
        for (const auto& pair : {std::pair<QStringList,QStringList>{{"old"},{"new"}}, {{},{"new"}}, {{"old"},{}}, {{},{}}}) {
            TextSnapshot left, right; left.lines=pair.first; right.lines=pair.second;
            auto prepared=prepareComparison(left,right); QVERIFY(prepared.comparison);
            ViewProjection projection; projection.build(*prepared.comparison);
            int removed=0, added=0;
            for (int i=0;i<projection.rows().size();++i) {
                const auto& r=projection.rows()[i];
                if (r.leftLine>=0) { QCOMPARE(projection.rowFor(Side::Left,r.leftLine),i); ++removed; }
                if (r.rightLine>=0) { QCOMPARE(projection.rowFor(Side::Right,r.rightLine),i); ++added; }
            }
            QCOMPARE(removed,pair.first.size()); QCOMPARE(added,pair.second.size());
            FileDiffWidget widget; widget.setComparison(prepared.comparison); widget.setViewMode(ViewMode::Unified);
            QCOMPARE(widget.unifiedEditor()->displayRows().size(),projection.rows().size());
            if (!pair.first.isEmpty() && !pair.second.isEmpty()) {
                QCOMPARE(projection.rows()[0].rightLine,-1); QCOMPARE(projection.rows()[1].leftLine,-1);
                QVERIFY(widget.navigateToChange(0));
                QCOMPARE(widget.unifiedEditor()->edit()->area()->cursorPosition().line,0);
                QVERIFY(widget.revealText(Side::Right,{0,1,1}));
                QCOMPARE(widget.unifiedEditor()->edit()->area()->cursorPosition().line,1);
                QCOMPARE(widget.unifiedEditor()->edit()->area()->cursorPosition().column,1);
                QVERIFY(widget.setSearchHighlights(Side::Left,{{0,0,1}}));
                QVERIFY(widget.setSearchHighlights(Side::Right,{{0,0,1}}));
                const auto selections=widget.unifiedEditor()->edit()->area()->extraSelections();
                QCOMPARE(selections.size(),2); QCOMPARE(selections[0].start.line,0); QCOMPARE(selections[1].start.line,1);
            }
        }
    }
    void contextFoldsRevealAndTwinOpening() {
        QStringList left; for(int i=0;i<100;++i) left.append(QStringLiteral("line %1").arg(i));
        auto right=left; right[40]="changed"; right[44]="also changed";
        FileDiffWidget widget; widget.resize(800,250); widget.setContent(left,right);
        widget.setViewMode(ViewMode::Unified); widget.setUnchangedLinesSkipped(true); widget.show();
        QApplication::processEvents();
        auto rows=widget.unifiedEditor()->displayRows();
        QCOMPARE(rows.first().hiddenCount,37); QCOMPARE(rows.last().hiddenCount,52);
        QCOMPARE(rows[1].leftLine,37);
        int folds=0; for(const auto& r:rows) if(r.hiddenCount) ++folds; QCOMPARE(folds,2);
        QVERIFY(widget.revealText(Side::Right,{10,2,2},true));
        rows=widget.unifiedEditor()->displayRows(); QCOMPARE(rows.first().hiddenCount,0);
        QCOMPARE(widget.unifiedEditor()->edit()->area()->cursorPosition().line,10);
        QVERIFY(widget.setSearchHighlights(Side::Left,{{90,0,2}}));
        for(const auto& r:widget.unifiedEditor()->displayRows()) QCOMPARE(r.hiddenCount,0);
        widget.setUnchangedLinesSkipped(false); widget.setUnchangedLinesSkipped(true);
        // Existing search/reveal overlays reopen their equal runs.
        for(const auto& r:widget.unifiedEditor()->displayRows()) QCOMPARE(r.hiddenCount,0);
        widget.clearSearchHighlights();
        widget.setUnchangedLinesSkipped(false); widget.setUnchangedLinesSkipped(true);
        widget.setViewMode(ViewMode::SideBySide);
        QCOMPARE(widget.leftEditor()->displayRows().first().hiddenCount,37);
        QCOMPARE(widget.rightEditor()->displayRows().first().hiddenCount,37);
        widget.leftEditor()->edit()->area()->verticalScrollBar()->setValue(0);
        QApplication::processEvents();
        const auto vp=widget.leftEditor()->edit()->area()->viewportState();
        QTest::mouseClick(widget.leftEditor()->edit()->area()->viewport(),Qt::LeftButton,Qt::NoModifier,QPoint(20,vp.contentOffsetY+vp.lineHeight/2));
        QCOMPARE(widget.leftEditor()->displayRows().first().hiddenCount,0);
        QCOMPARE(widget.rightEditor()->displayRows().first().hiddenCount,0);
        QVERIFY(widget.navigateToChange(0));
        QCOMPARE(widget.leftEditor()->originalLine(widget.leftEditor()->edit()->area()->cursorPosition().line),40);
        widget.setContextLines(0); QCOMPARE(widget.contextLines(),0);
        for (bool dark : {false, true}) {
            const auto scheme = dark ? ColorScheme::darkDefault() : ColorScheme::lightDefault();
            widget.leftEditor()->setColorScheme(scheme); widget.rightEditor()->setColorScheme(scheme);
            QVERIFY(widget.navigateToChange(0)); widget.resize(900,300); QApplication::processEvents();
            auto* splitter=widget.findChild<QSplitter*>(); QVERIFY(splitter);
            auto* handle=splitter->handle(1);
            const auto image=handle->grab().toImage();
            QVERIFY(colorHeight(image,image.width()/2,scheme.replaceBg)>0);
            QCOMPARE(widget.leftEditor()->originalLine(widget.leftEditor()->edit()->area()->cursorPosition().line),40);
        }
    }
    void extremeContextKeepsAllLines() {
        FileDiffWidget widget;
        widget.setContent({"a", "b", "c"}, {"a", "changed", "c"});
        widget.setUnchangedLinesSkipped(true);
        widget.setContextLines(std::numeric_limits<int>::max());
        QCOMPARE(widget.leftEditor()->edit()->document()->lineCount(), 3);
        for (const auto& row : widget.leftEditor()->displayRows()) QCOMPARE(row.hiddenCount, 0);
    }
    void switchingModesPreservesOriginalAnchor() {
        QStringList left; for(int i=0;i<100;++i) left.append(QString::number(i));
        auto right=left; right[10]="different";
        FileDiffWidget widget; widget.resize(800,200); widget.setContent(left,right); widget.show(); QApplication::processEvents();
        widget.rightEditor()->edit()->area()->verticalScrollBar()->setValue(50);
        const int original=widget.rightEditor()->edit()->area()->viewportState().firstVisibleLine;
        widget.setViewMode(ViewMode::Unified); QApplication::processEvents();
        const int row=widget.unifiedEditor()->edit()->area()->viewportState().firstVisibleLine;
        QCOMPARE(widget.unifiedEditor()->displayRows()[row].rightLine,original);
        widget.setViewMode(ViewMode::SideBySide); QApplication::processEvents();
        QCOMPARE(widget.rightEditor()->edit()->area()->viewportState().firstVisibleLine,original);
    }

    void preparedAndSynchronousViewsAgree_data() {
        QTest::addColumn<QStringList>("left");
        QTest::addColumn<QStringList>("right");
        QTest::addColumn<bool>("ignoreTrailing");
        QTest::newRow("empty") << QStringList{} << QStringList{} << false;
        QTest::newRow("insert") << QStringList{} << QStringList{"new"} << false;
        QTest::newRow("delete") << QStringList{"old"} << QStringList{} << false;
        QTest::newRow("replace-word") << QStringList{"Shift = 0 modification means preference."}
                                      << QStringList{"Shift = 0 means preference."} << false;
        QTest::newRow("formatting") << QStringList{"head", "  a:=2;", "tail"}
                                   << QStringList{"head", "new();", "    a := 2;  ", "tail"} << false;
        QTest::newRow("ignore-trailing") << QStringList{"a  "} << QStringList{"a"} << true;
    }

    void preparedAndSynchronousViewsAgree() {
        QFETCH(QStringList, left);
        QFETCH(QStringList, right);
        QFETCH(bool, ignoreTrailing);
        ComparisonOptions options;
        options.diff.ignoreTrailingWhitespace = ignoreTrailing;
        TextSnapshot l, r;
        l.lines = left; r.lines = right;
        const auto result = prepareComparison(l, r, options);
        QCOMPARE(result.status, PreparationStatus::Ready);
        QVERIFY(result.comparison);
        FileDiffWidget synchronous, prepared;
        synchronous.setContent(left, right, options.diff);
        prepared.setComparison(result.comparison);
        for (auto* widget : {&synchronous, &prepared}) {
            widget->resize(850, 240);
            widget->show();
        }
        QApplication::processEvents();
        QCOMPARE(prepared.changeCount(), synchronous.changeCount());
        for (Side side : {Side::Left, Side::Right}) {
            auto* first = side == Side::Left ? synchronous.leftEditor() : synchronous.rightEditor();
            auto* second = side == Side::Left ? prepared.leftEditor() : prepared.rightEditor();
            QCOMPARE(first->edit()->document()->lineCount(), second->edit()->document()->lineCount());
            for (int i = 0; i < first->edit()->document()->lineCount(); ++i)
                QCOMPARE(first->edit()->document()->lineAt(i), second->edit()->document()->lineAt(i));
            // Identical data, layout and theme must render identical diff colors.
            first->edit()->area()->clearFocus(); second->edit()->area()->clearFocus();
            QCOMPARE(first->edit()->area()->viewport()->grab().toImage(),
                     second->edit()->area()->viewport()->grab().toImage());
        }
        const auto expected = IntraLineDiffEngine::compute(result.comparison->diff(), left, right);
        for (Side side : {Side::Left, Side::Right}) {
            const auto& actual = side == Side::Left ? result.comparison->highlights().leftRanges : result.comparison->highlights().rightRanges;
            const auto& reference = side == Side::Left ? expected.leftRanges : expected.rightRanges;
            QCOMPARE(actual.size(), reference.size());
            for (int i = 0; i < actual.size(); ++i) {
                QCOMPARE(actual[i].size(), reference[i].size());
                for (int j = 0; j < actual[i].size(); ++j) {
                    QCOMPARE(actual[i][j].start, reference[i][j].start);
                    QCOMPARE(actual[i][j].length, reference[i][j].length);
                }
            }
        }
    }

    void workerResultOwnsDataAndReplacementReleasesIt() {
        auto future = std::async(std::launch::async, [] {
            auto left = TextSnapshot::fromText("old word\n", "before");
            auto right = TextSnapshot::fromText("new word", "after");
            auto result = prepareComparison(left, right);
            left.lines[0] = "mutated worker input";
            right.lines.clear();
            return result;
        });
        auto result = future.get();
        QCOMPARE(result.status, PreparationStatus::Ready);
        QCOMPARE(result.comparison->snapshot(Side::Left).lines[0], QString("old word"));
        QCOMPARE(result.comparison->model().documentLines(Side::Right), QStringList{"new word"});
        FileDiffWidget widget;
        widget.resize(800, 200); widget.show();
        widget.setComparison(result.comparison);
        std::weak_ptr<const PreparedComparison> old = result.comparison;
        result.comparison.reset();
        QVERIFY(!old.expired());
        for (int i = 0; i < 10; ++i) {
            widget.setContent({"a", "same"}, {QString::number(i), "same"});
            QApplication::processEvents();
            widget.grab(); // Exercise model pointers retained by painters.
        }
        QVERIFY(old.expired());
        widget.clearComparison();
        QApplication::processEvents();
        widget.grab();
        QVERIFY(!widget.comparison());
        QCOMPARE(widget.changeCount(), 0);
    }

    void preparationLimits_data() {
        QTest::addColumn<int>("limit");
        QTest::newRow("combined-lines") << 0;
        QTest::newRow("combined-code-units") << 1;
        QTest::newRow("long-line") << 2;
        QTest::newRow("algorithm-work") << 3;
        QTest::newRow("trace-storage") << 4;
    }

    void preparationLimits() {
        QFETCH(int, limit);
        ComparisonOptions options;
        if (limit == 0) options.limits.maxInputLines = 1;
        if (limit == 1) options.limits.maxInputCodeUnits = 1;
        if (limit == 2) options.limits.maxLineCodeUnits = 1;
        if (limit == 3) options.limits.maxWork = 1;
        if (limit == 4) options.limits.maxTraceEntries = 0;
        auto result = prepareComparison(TextSnapshot::fromText("aaa\nbbb"), TextSnapshot::fromText("xxx\nyyy"), options);
        QCOMPARE(result.status, PreparationStatus::ResourceLimit);
        QVERIFY(!result.comparison);
        QVERIFY(!result.message.isEmpty());
        const auto equal = prepareComparison({}, {});
        QCOMPARE(equal.status, PreparationStatus::Ready);
        QVERIFY(equal.comparison->diff().isIdentical());
    }

    void cancelledAndMalformedInputsNeverYieldAComparison() {
        diffcore::CancellationToken token;
        auto copy = token;
        copy.requestCancellation();
        const auto cancelled = prepareComparison({}, {}, {}, token);
        QCOMPARE(cancelled.status, PreparationStatus::Cancelled);
        QVERIFY(!cancelled.comparison);
        for (auto malformed : {TextSnapshot{{"embedded\nnewline"}, {}, {}, {}, {}},
                               TextSnapshot{{}, true, {}, {}, {}},
                               TextSnapshot{{"x"}, false, {LineEnding::LF}, {}, {}}}) {
            const auto result = prepareComparison(malformed, {});
            QCOMPARE(result.status, PreparationStatus::Error);
            QVERIFY(!result.comparison);
        }
    }

    void intraLineWorkBudgetStopsInsideLongWords() {
        diffcore::DiffResult diff;
        diff.hunks = {{ChangeType::Replace, {0, 1}, {0, 1}}};
        diffcore::ComputationControl control({}, 100);
        bool stopped = false;
        try { IntraLineDiffEngine::compute(diff, {QString(10000, 'a')}, {QString(10000, 'b')}, &control); }
        catch (const diffcore::ComputationStopped& error) {
            QCOMPARE(error.reason, diffcore::StopReason::ResourceLimit);
            stopped = true;
        }
        QVERIFY(stopped);
        QCOMPARE(control.workPerformed(), std::uint64_t(100));
        diffcore::CancellationToken cancellation;
        cancellation.requestCancellation();
        diffcore::ComputationControl cancelled(cancellation);
        QVERIFY_EXCEPTION_THROWN(IntraLineDiffEngine::compute(diff, {"a"}, {"b"}, &cancelled), diffcore::ComputationStopped);
    }

    void cancellationFromAnotherThreadStopsPreparation() {
        diffcore::CancellationToken token;
        ComparisonOptions options;
        options.limits.maxWork = 10000000;
        const auto left = TextSnapshot::fromText(QString(50000, 'a'));
        const auto right = TextSnapshot::fromText(QString(50000, 'b'));
        std::promise<void> entered;
        auto started = entered.get_future();
        auto future = std::async(std::launch::async, [&] {
            entered.set_value();
            return prepareComparison(left, right, options, token);
        });
        started.wait();
        token.requestCancellation();
        const auto result = future.get();
        QCOMPARE(result.status, PreparationStatus::Cancelled);
        QVERIFY(!result.comparison);
    }

    void originalCoordinatesNavigationAndOverlayReset() {
        FileDiffWidget widget;
        widget.resize(850, 200); widget.show();
        widget.setContent({"header", "deleted", "same", "old", "tail"},
                          {"header", "same", "new", "tail", "added"});
        QSignalSpy selected(&widget, &FileDiffWidget::currentChangeChanged);
        QVERIFY(widget.changeCount() > 0);
        QVERIFY(widget.navigateToChange(0));
        QCOMPARE(widget.currentChangeIndex(), 0);
        QCOMPARE(selected.count(), 1);
        QVERIFY(!widget.navigateToChange(widget.changeCount()));
        QCOMPARE(widget.currentChangeIndex(), 0);
        QVERIFY(widget.revealText(Side::Left, {1, 2, 3}, true));
        QCOMPARE(widget.leftEditor()->edit()->area()->cursorPosition().line, 1);
        QCOMPARE(widget.leftEditor()->edit()->area()->cursorPosition().column, 2);
        QCOMPARE(widget.currentChangeIndex(), -1);
        QVERIFY(widget.revealLines(Side::Right, {4, 1}, true));
        QCOMPARE(widget.rightEditor()->edit()->area()->cursorPosition().line, 4);
        QVERIFY(widget.setSearchHighlights(Side::Left, {{1, 0, 7}, {5, 0, 0}}));
        QCOMPARE(widget.leftEditor()->edit()->area()->extraSelections().size(), 1);
        QVERIFY(!widget.setSearchHighlights(Side::Left, {{1, 0, 8}}));
        QCOMPARE(widget.leftEditor()->edit()->area()->extraSelections().size(), 1);
        QVERIFY(!widget.revealLines(Side::Left, {-1, 1}));
        QVERIFY(!widget.revealLines(Side::Left, {5, 1}));
        QVERIFY(!widget.revealLines(Side::Left, {1, std::numeric_limits<int>::max()}));
        QVERIFY(!widget.revealText(Side::Right, {5, 1, 0}));
        QVERIFY(widget.revealText(Side::Right, {5, 0, 0}, true));
        widget.clearSearchHighlights();
        QVERIFY(widget.leftEditor()->edit()->area()->extraSelections().isEmpty());
        QVERIFY(widget.setSearchHighlights(Side::Right, {{4, 0, 5}}));
        widget.setContent({}, {});
        QVERIFY(widget.rightEditor()->edit()->area()->extraSelections().isEmpty());
        QCOMPARE(widget.currentChangeIndex(), -1);
        QVERIFY(widget.revealLines(Side::Left, {0, 0}, true));
        QVERIFY(widget.revealText(Side::Right, {0, 0, 0}));
        QVERIFY(!widget.revealLines(Side::Right, {0, 1}));
        widget.clearComparison();
        QVERIFY(!widget.revealLines(Side::Left, {0, 0}));
    }

    void utf16ColumnsAndFinalNewlineMetadata() {
        const auto empty = TextSnapshot::fromText("");
        const auto emptyLine = TextSnapshot::fromText("\n");
        QVERIFY(empty.lines.isEmpty());
        QCOMPARE(empty.finalNewline, std::optional<bool>(false));
        QCOMPARE(emptyLine.lines, QStringList{""});
        QCOMPARE(emptyLine.finalNewline, std::optional<bool>(true));
        const auto mixed = TextSnapshot::fromText("a\r\nb\rc\nx");
        QCOMPARE(mixed.lines, (QStringList{"a", "b", "c", "x"}));
        QCOMPARE(mixed.lineEndings, (QVector<LineEnding>{LineEnding::CRLF, LineEnding::CR, LineEnding::LF, LineEnding::None}));
        FileDiffWidget widget;
        const auto result = prepareComparison(TextSnapshot::fromText(QString::fromUtf8("a😀b"), "parent"),
                                              TextSnapshot::fromText("a\r\n", "current"));
        QCOMPARE(result.status, PreparationStatus::Ready);
        widget.setComparison(result.comparison);
        QVERIFY(widget.revealText(Side::Left, {0, 3, 1}));
        QVERIFY(!widget.revealText(Side::Left, {0, 4, 1}));
        QVERIFY(!widget.findChild<QLabel*>("diffTextMetadata"));
        const auto& leftSnapshot = widget.comparison()->snapshot(Side::Left);
        const auto& rightSnapshot = widget.comparison()->snapshot(Side::Right);
        QCOMPARE(leftSnapshot.label, QString("parent"));
        QCOMPARE(leftSnapshot.finalNewline, std::optional<bool>(false));
        QCOMPARE(rightSnapshot.lineEndings, QVector<LineEnding>{LineEnding::CRLF});
        QCOMPARE(widget.leftEditor()->edit()->document()->lineCount(), 1);
        QCOMPARE(widget.rightEditor()->edit()->document()->lineCount(), 1);
        widget.clearComparison();
        QVERIFY(!widget.comparison());
    }

    void failedFileLoadReportsToHostWithoutOpeningADialog() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        FileDiffWidget widget;
        QSignalSpy errors(&widget, &FileDiffWidget::loadFailed);
        QVERIFY(!widget.loadFromPaths(directory.filePath("missing-left"),
                                      directory.filePath("missing-right")));
        QCOMPARE(errors.count(), 1);
        QVERIFY(errors.first().first().toString().contains("missing-left"));
        QVERIFY(widget.findChildren<QShortcut*>().isEmpty());
    }

    void largeUnequalBlocksClipAtViewportEdges_data() {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<int>("scrollTop");
        QTest::newRow("light-bottom") << false << 8;
        QTest::newRow("light-top-and-bottom") << false << 20;
        QTest::newRow("dark-bottom") << true << 8;
        QTest::newRow("dark-top-and-bottom") << true << 20;
    }

    void largeUnequalBlocksClipAtViewportEdges() {
        QFETCH(bool, dark);
        QFETCH(int, scrollTop);
        QStringList left, right;
        for (int i = 0; i < 100; ++i) left.append("short");
        for (int i = 0; i < 120; ++i) right.append(QString(200, 'x'));
        diffcore::DiffResult diff;
        diff.hunks = {
            {ChangeType::Equal, {0, 10}, {0, 10}},
            {ChangeType::Replace, {10, 30}, {10, 50}},
            {ChangeType::Equal, {40, 60}, {60, 60}},
        };
        AlignedLineModel model;
        model.build(diff, left, right);
        auto* leftEditor = new DiffEditor(Side::Left);
        auto* rightEditor = new DiffEditor(Side::Right);
        const auto scheme = dark ? ColorScheme::darkDefault() : ColorScheme::lightDefault();
        for (auto* editor : {leftEditor, rightEditor}) {
            editor->setColorScheme(scheme);
            editor->setAlignedModel(&model);
        }
        DiffConnectorSplitter splitter(leftEditor, rightEditor, &model);
        splitter.resize(850, 220);
        splitter.show();
        QApplication::processEvents();
        auto* leftArea = leftEditor->edit()->area();
        auto* rightArea = rightEditor->edit()->area();
        QVERIFY(rightArea->horizontalScrollBar()->isVisible());
        QVERIFY(!leftArea->horizontalScrollBar()->isVisible());
        leftArea->verticalScrollBar()->setValue(scrollTop);
        rightArea->verticalScrollBar()->setValue(scrollTop);
        QApplication::processEvents();
        auto* handle = splitter.handle(1);
        const int rightTop = rightArea->viewport()->mapTo(&splitter, QPoint()).y() - handle->y();
        const int clipBottom = rightTop + rightArea->viewport()->height();
        const QImage image = handle->grab().toImage();
        const qreal scale = image.devicePixelRatio();
        const int x = image.width() / 2;
        QCOMPARE(image.pixelColor(x, qRound((clipBottom - 2) * scale)), scheme.replaceBg);
        QCOMPARE(image.pixelColor(x, qRound((clipBottom + 2) * scale)), handle->palette().base().color());
        if (scrollTop > 10)
            QCOMPARE(image.pixelColor(x, qRound((rightTop + 2) * scale)), scheme.replaceBg);

        // After scrolling beyond both ranges no ribbon may remain.
        leftArea->verticalScrollBar()->setValue(80);
        rightArea->verticalScrollBar()->setValue(80);
        QApplication::processEvents();
        QCOMPARE(colorHeight(handle->grab().toImage(), x, scheme.replaceBg), 0);
    }

    void navigationCommandsKeepConnectorsOnSelectedBlocks() {
        QStringList left;
        for (int i = 0; i < 80; ++i) left.append(QStringLiteral("common-%1").arg(i));
        QStringList right = left;
        right[0] = "changed first line";
        right[50] = "changed later line";
        right.insert(25, "inserted line");
        right.insert(26, "second inserted line");
        FileDiffWidget widget;
        widget.resize(850, 350);
        widget.setContent(left, right);
        widget.show();
        widget.activateWindow();
        auto* leftArea = widget.leftEditor()->edit()->area();
        auto* rightArea = widget.rightEditor()->edit()->area();
        leftArea->setFocus();
        QApplication::processEvents();
        auto* label = widget.findChild<QLabel*>("diffChangePosition");
        QVERIFY(label);
        auto* splitter = widget.findChild<QSplitter*>();
        QVERIFY(splitter);
        auto* handle = splitter->handle(1);
        const auto checkConnector = [&](int lStart, int lCount, int rStart, int rCount, QColor color) {
            const auto& lvp = leftArea->viewportState();
            const auto& rvp = rightArea->viewportState();
            const int origin = handle->mapTo(&widget, QPoint()).y();
            const qreal ly = leftArea->viewport()->mapTo(&widget, QPoint()).y() - origin
                + (lStart + lCount / 2.0 - lvp.firstVisibleLine) * lvp.lineHeight;
            const qreal ry = rightArea->viewport()->mapTo(&widget, QPoint()).y() - origin
                + (rStart + rCount / 2.0 - rvp.firstVisibleLine) * rvp.lineHeight;
            const auto image = handle->grab().toImage();
            const qreal scale = image.devicePixelRatio();
            const int y = qRound((ly + ry) / 2 * scale);
            QVERIFY(y >= 0 && y < image.height());
            QCOMPARE(image.pixelColor(image.width() / 2, y), color);
        };
        QVERIFY(widget.findChildren<QShortcut*>().isEmpty());
        const auto scheme = widget.leftEditor()->colorScheme();
        widget.navigateToNext();
        QApplication::processEvents();
        QCOMPARE(label->text(), QStringLiteral("1 / 3"));
        checkConnector(0, 1, 0, 1, scheme.replaceBg);
        widget.navigateToNext();
        QApplication::processEvents();
        QCOMPARE(label->text(), QStringLiteral("2 / 3"));
        QCOMPARE(leftArea->cursorPosition().line, 25);
        checkConnector(25, 0, 25, 2, scheme.insertBg);
        widget.navigateToNext();
        QApplication::processEvents();
        QCOMPARE(label->text(), QStringLiteral("3 / 3"));
        QCOMPARE(leftArea->cursorPosition().line, 50);
        QCOMPARE(rightArea->cursorPosition().line, 52);
        checkConnector(50, 1, 52, 1, scheme.replaceBg);
        widget.resize(1000, 430);
        splitter->setSizes({350, 650});
        QApplication::processEvents();
        checkConnector(50, 1, 52, 1, scheme.replaceBg);
        widget.navigateToPrev();
        QApplication::processEvents();
        QCOMPARE(label->text(), QStringLiteral("2 / 3"));
        checkConnector(25, 0, 25, 2, scheme.insertBg);

        // Moving the caret manually makes navigation relative to that pane.
        rightArea->setFocus();
        rightArea->setCursorPosition({40, 0});
        widget.navigateToNext();
        QApplication::processEvents();
        QCOMPARE(label->text(), QStringLiteral("3 / 3"));
    }

    void navigationVisitsChangesSharingALeftBoundary() {
        FileDiffWidget widget;
        widget.resize(800, 300);
        widget.setContent({"old"}, {"new", "extra"});
        widget.show();
        QApplication::processEvents();
        auto* label = widget.findChild<QLabel*>("diffChangePosition");
        QVERIFY(label);
        // Both changes can point to the same clamped caret line in the shorter file.
        widget.navigateToNext();
        QCOMPARE(label->text(), QStringLiteral("1 / 2"));
        widget.navigateToNext();
        QCOMPARE(label->text(), QStringLiteral("2 / 2"));
        widget.navigateToPrev();
        QCOMPARE(label->text(), QStringLiteral("1 / 2"));
    }

    // Projected views paint their gutter row by row. Unchanged rows keep the gutter background instead of the
    // black of an invalid colour, folded rows take the placeholder colour, and the unified view marks old rows
    // red and new rows green. Both editors used here have their gutter left of the text.
    void projectedGuttersKeepTheirBackground() {
        QStringList left, right;
        for (int i = 0; i < 20; ++i) left.append(QStringLiteral("same %1").arg(i));
        right = left;
        left[10] = QStringLiteral("old line");
        right[10] = QStringLiteral("new line");
        const auto gutterRow = [](DiffEditor* editor, int row) {
            const QImage image = editor->grab().toImage();
            const qreal scale = image.devicePixelRatio();
            const QPoint viewport = editor->edit()->area()->viewport()->mapTo(editor, QPoint());
            const auto& vp = editor->edit()->area()->viewportState();
            const int y = viewport.y() + vp.contentOffsetY + (row - vp.firstVisibleLine) * vp.lineHeight
                        + vp.lineHeight / 2;
            QVector<QColor> colors;
            for (int x = 0; x < viewport.x(); ++x) colors.append(image.pixelColor(qRound(x * scale), qRound(y * scale)));
            return colors;
        };
        const auto rowOf = [](DiffEditor* editor, auto matches) {
            const auto& rows = editor->displayRows();
            for (int i = 0; i < rows.size(); ++i) if (matches(rows[i])) return i;
            return -1;
        };
        for (bool dark : {false, true}) {
            const auto scheme = dark ? ColorScheme::darkDefault() : ColorScheme::lightDefault();
            FileDiffWidget widget;
            widget.resize(800, 400);
            widget.setContent(left, right);
            widget.setUnchangedLinesSkipped(true);
            for (ViewMode mode : {ViewMode::Unified, ViewMode::SideBySide}) {
                widget.setViewMode(mode);
                DiffEditor* editor = mode == ViewMode::Unified ? widget.unifiedEditor() : widget.rightEditor();
                editor->setColorScheme(scheme);
                widget.show();
                QApplication::processEvents();
                const int equal = rowOf(editor, [](const ViewRow& r) { return !r.hiddenCount && r.type == ChangeType::Equal; });
                const int folded = rowOf(editor, [](const ViewRow& r) { return r.hiddenCount > 0; });
                QVERIFY(equal >= 0 && folded >= 0);
                QVERIFY(!gutterRow(editor, equal).contains(QColor(Qt::black)));
                QVERIFY(gutterRow(editor, equal).contains(scheme.gutterBg));
                QVERIFY(gutterRow(editor, folded).contains(scheme.placeholderBg));
                if (mode != ViewMode::Unified) continue;
                const int removed = rowOf(editor, [](const ViewRow& r) { return r.type != ChangeType::Equal && r.rightLine < 0; });
                const int added = rowOf(editor, [](const ViewRow& r) { return r.type != ChangeType::Equal && r.leftLine < 0; });
                QVERIFY(removed >= 0 && added >= 0);
                QVERIFY(gutterRow(editor, removed).contains(scheme.removedBg));
                QVERIFY(gutterRow(editor, added).contains(scheme.addedBg));
            }
        }
    }

    void systemPaletteChangesUpdateBlockAndConnectorColors() {
        struct PaletteRestore {
            QPalette saved = QApplication::palette();
            ~PaletteRestore() { QApplication::setPalette(saved); }
        } restore;
        FileDiffWidget widget;
        widget.resize(800, 300);
        widget.setContent({"before", "old", "after"}, {"before", "new", "after"});
        widget.show();
        for (bool dark : {true, false}) {
            QPalette palette = restore.saved;
            palette.setColor(QPalette::Window, dark ? QColor(30, 30, 30) : QColor(250, 250, 250));
            palette.setColor(QPalette::Base, dark ? QColor(25, 25, 25) : QColor(255, 255, 255));
            palette.setColor(QPalette::Text, dark ? Qt::white : Qt::black);
            QApplication::setPalette(palette);
            QApplication::processEvents();
            const auto expected = dark ? ColorScheme::darkDefault() : ColorScheme::lightDefault();
            QCOMPARE(widget.leftEditor()->colorScheme().darkTheme, dark);
            QCOMPARE(widget.rightEditor()->colorScheme().darkTheme, dark);
            const int lh = widget.leftEditor()->edit()->area()->viewportState().lineHeight;
            QCOMPARE(pixelAt(*widget.leftEditor(), lh + lh / 2), expected.replaceBg);
            auto* editor = widget.leftEditor();
            const auto railImage = editor->grab().toImage();
            const qreal railScale = railImage.devicePixelRatio();
            const int rowY = editor->edit()->area()->viewport()->mapTo(editor, QPoint()).y()
                           + lh + lh / 2;
            QCOMPARE(railImage.pixelColor(qRound((editor->width() - 2) * railScale),
                                          qRound(rowY * railScale)), expected.replaceBg);
            auto* handle = widget.findChild<QSplitter*>()->handle(1);
            QVERIFY(colorHeight(handle->grab().toImage(), 24, expected.replaceBg) > 0);
            const auto image = handle->grab().toImage();
            QCOMPARE(image.pixelColor(image.width() / 2, image.height() - 10), palette.base().color());
        }
    }

    void connectors_data() {
        QTest::addColumn<int>("leftCount");
        QTest::addColumn<int>("rightCount");
        QTest::addColumn<int>("prefixCount");
        QTest::addColumn<bool>("dark");
        for (bool dark : {false, true}) {
            const QByteArray theme = dark ? "dark-" : "light-";
            QTest::newRow((theme + "insert-middle").constData()) << 0 << 3 << 1 << dark;
            QTest::newRow((theme + "delete-middle").constData()) << 3 << 0 << 1 << dark;
            QTest::newRow((theme + "insert-empty").constData()) << 0 << 3 << 0 << dark;
            QTest::newRow((theme + "delete-empty").constData()) << 3 << 0 << 0 << dark;
            QTest::newRow((theme + "replace-expand").constData()) << 1 << 3 << 1 << dark;
            QTest::newRow((theme + "replace-contract").constData()) << 3 << 1 << 1 << dark;
            QTest::newRow((theme + "replace-equal").constData()) << 2 << 2 << 1 << dark;
        }
    }

    void connectors() {
        QFETCH(int, leftCount);
        QFETCH(int, rightCount);
        QFETCH(int, prefixCount);
        QFETCH(bool, dark);
        QStringList left, right;
        diffcore::DiffResult diff;
        if (prefixCount) {
            left.append("before");
            right.append("before");
            diff.hunks.push_back({ChangeType::Equal, {0, 1}, {0, 1}});
        }
        for (int i = 0; i < leftCount; ++i) left.append("old");
        for (int i = 0; i < rightCount; ++i) right.append("new");
        const auto type = !leftCount ? ChangeType::Insert
                        : !rightCount ? ChangeType::Delete : ChangeType::Replace;
        diff.hunks.push_back({type, {prefixCount, leftCount}, {prefixCount, rightCount}});
        AlignedLineModel model;
        model.build(diff, left, right);
        auto* leftEditor = new DiffEditor(Side::Left);
        auto* rightEditor = new DiffEditor(Side::Right);
        const auto scheme = dark ? ColorScheme::darkDefault() : ColorScheme::lightDefault();
        for (auto* editor : {leftEditor, rightEditor}) {
            editor->setColorScheme(scheme);
            editor->setAlignedModel(&model);
        }
        // Place the splitter below a toolbar to catch window/local coordinate mixups.
        QWidget container;
        QVBoxLayout layout(&container);
        layout.setContentsMargins(0, 0, 0, 0);
        layout.addSpacing(70);
        DiffConnectorSplitter splitter(leftEditor, rightEditor, &model);
        layout.addWidget(&splitter);
        container.resize(800, 290);
        container.show();
        QApplication::processEvents();
        auto* handle = splitter.handle(1);
        const QImage image = handle->grab().toImage();
        const qreal scale = image.devicePixelRatio();
        const QColor fill = scheme.backgroundFor(type);
        const int leftHeight = colorHeight(image, qRound(2 * scale), fill);
        const int rightHeight = colorHeight(image, image.width() - qRound(3 * scale), fill);
        if (leftCount < rightCount) QVERIFY(leftHeight < rightHeight);
        else if (leftCount > rightCount) QVERIFY(leftHeight > rightHeight);
        else QCOMPARE(leftHeight, rightHeight);
        QVERIFY(leftHeight > 0 || rightHeight > 0);

        // The connector midpoint joins the centers of the two document ranges.
        auto* leftArea = leftEditor->edit()->area();
        auto* rightArea = rightEditor->edit()->area();
        const int handleTop = handle->mapTo(&container, QPoint(0, 0)).y();
        const qreal leftCenter = leftArea->viewport()->mapTo(&container, QPoint(0, 0)).y() - handleTop
            + (prefixCount + leftCount / 2.0) * leftArea->viewportState().lineHeight;
        const qreal rightCenter = rightArea->viewport()->mapTo(&container, QPoint(0, 0)).y() - handleTop
            + (prefixCount + rightCount / 2.0) * rightArea->viewportState().lineHeight;
        QCOMPARE(image.pixelColor(qRound(handle->width() / 2.0 * scale),
                                  qRound((leftCenter + rightCenter) / 2 * scale)), fill);
    }

    void connectorsFollowViewportAndClearOnReload() {
        QStringList left;
        for (int i = 0; i < 60; ++i) left.append(QString::number(i));
        QStringList right = left;
        right.insert(25, "inserted");
        right.insert(26, "another inserted line");
        AlignedLineModel model;
        model.build(diffcore::DiffEngine{}.compute(left, right), left, right);
        auto* leftEditor = new DiffEditor(Side::Left);
        auto* rightEditor = new DiffEditor(Side::Right);
        for (auto* editor : {leftEditor, rightEditor}) editor->setAlignedModel(&model);
        DiffConnectorSplitter splitter(leftEditor, rightEditor, &model);
        splitter.resize(800, 200);
        splitter.show();
        QApplication::processEvents();
        auto* handle = splitter.handle(1);
        const auto scheme = leftEditor->colorScheme();
        QVERIFY(colorHeight(handle->grab().toImage(), 24, scheme.insertBg) == 0);

        // Independent offsets also model a connector partially outside the view.
        leftEditor->edit()->area()->verticalScrollBar()->setValue(26);
        rightEditor->edit()->area()->verticalScrollBar()->setValue(25);
        splitter.resize(950, 240);
        splitter.setSizes({300, 600});
        QApplication::processEvents();
        QVERIFY(colorHeight(handle->grab().toImage(), 24, scheme.insertBg) > 0);
        QCOMPARE(handle->width(), 48);
        QVERIFY(leftEditor->width() < rightEditor->width());

        const auto dark = ColorScheme::darkDefault();
        for (auto* editor : {leftEditor, rightEditor}) editor->setColorScheme(dark);
        QApplication::processEvents();
        QVERIFY(colorHeight(handle->grab().toImage(), 24, dark.insertBg) > 0);

        model.build(diffcore::DiffEngine{}.compute(left, left), left, left);
        for (auto* editor : {leftEditor, rightEditor}) editor->setAlignedModel(&model);
        splitter.updateConnections();
        QApplication::processEvents();
        QCOMPARE(colorHeight(handle->grab().toImage(), 24, dark.insertBg), 0);
    }

    void oneSidedChange_data() {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<bool>("deletion");
        QTest::addColumn<int>("boundary");
        QTest::addColumn<bool>("empty");
        for (bool dark : {false, true}) {
            for (bool deletion : {false, true}) {
                for (int boundary = 0; boundary <= 2; ++boundary) {
                    const QByteArray name = QByteArray::number(dark) + "-"
                        + QByteArray::number(deletion) + "-" + QByteArray::number(boundary);
                    QTest::newRow(name.constData()) << dark << deletion << boundary << false;
                }
                const QByteArray name = QByteArray::number(dark) + "-"
                    + QByteArray::number(deletion) + "-empty";
                QTest::newRow(name.constData()) << dark << deletion << 0 << true;
            }
        }
    }

    void oneSidedChange() {
        QFETCH(bool, dark);
        QFETCH(bool, deletion);
        QFETCH(int, boundary);
        QFETCH(bool, empty);
        const auto scheme = dark ? ColorScheme::darkDefault() : ColorScheme::lightDefault();
        const QStringList unchanged = empty ? QStringList{} : QStringList{"before", "after"};
        QStringList changed = unchanged;
        changed.insert(boundary, "added line");
        changed.insert(boundary + 1, "another added line");
        const QStringList left = deletion ? changed : unchanged;
        const QStringList right = deletion ? unchanged : changed;
        diffcore::DiffOptions options;
        options.applySliderHeuristics = false;
        AlignedLineModel model;
        model.build(diffcore::DiffEngine{}.compute(left, right, options), left, right);

        DiffEditor missing(deletion ? Side::Right : Side::Left);
        DiffEditor block(deletion ? Side::Left : Side::Right);
        showEditor(missing, model, scheme);
        showEditor(block, model, scheme);

        const int height = missing.edit()->area()->viewportState().lineHeight;
        QVERIFY(height > 2);
        QCOMPARE(pixelAt(missing, boundary * height), scheme.sideMarginInsertStripe);
        const QImage railImage = missing.grab().toImage();
        const qreal scale = railImage.devicePixelRatio();
        const int railX = missing.side() == Side::Left ? missing.width() - 2 : 1;
        const int boundaryY = missing.edit()->area()->viewport()->mapTo(&missing, QPoint()).y()
                            + boundary * height;
        QCOMPARE(railImage.pixelColor(qRound(railX * scale), qRound(boundaryY * scale)),
                 scheme.sideMarginInsertStripe);
        QVERIFY(pixelAt(missing, boundary * height + 2) != scheme.sideMarginInsertStripe);
        const int blockHeight = block.edit()->area()->viewportState().lineHeight;
        QCOMPARE(pixelAt(block, boundary * blockHeight + blockHeight / 2), scheme.insertBg);
        QCOMPARE(pixelAt(block, (boundary + 1) * blockHeight + blockHeight / 2), scheme.insertBg);
        QCOMPARE(missing.edit()->document()->lineCount(), unchanged.size());
    }

    void replacementsAndReload() {
        const QStringList left{"before", "old", "after"};
        const QStringList right{"before", "new", "after"};
        AlignedLineModel model;
        model.build(diffcore::DiffEngine{}.compute(left, right), left, right);
        const auto scheme = ColorScheme::lightDefault();
        DiffEditor editor(Side::Left);
        showEditor(editor, model, scheme);
        const int height = editor.edit()->area()->viewportState().lineHeight;
        QCOMPARE(pixelAt(editor, height + height / 2), scheme.replaceBg);
        QVERIFY(scheme.replaceBg != scheme.insertBg);

        // Replace a missing-side marker with an identical comparison.
        const QStringList extra{"before", "inserted", "old", "after"};
        model.build(diffcore::DiffEngine{}.compute(left, extra), left, extra);
        editor.setAlignedModel(&model);
        QApplication::processEvents();
        QCOMPARE(pixelAt(editor, height), scheme.sideMarginInsertStripe);
        model.build(diffcore::DiffEngine{}.compute(left, left), left, left);
        editor.setAlignedModel(&model);
        QApplication::processEvents();
        QVERIFY(pixelAt(editor, height) != scheme.sideMarginInsertStripe);
        editor.setAlignedModel(nullptr);
        QApplication::processEvents();
        QVERIFY(pixelAt(editor, 0) != scheme.sideMarginInsertStripe);
    }

    void boundaryFollowsScrollingAndResize() {
        QStringList left;
        for (int i = 0; i < 50; ++i) left.append(QString::number(i));
        QStringList right = left;
        right.insert(25, "inserted");
        AlignedLineModel model;
        model.build(diffcore::DiffEngine{}.compute(left, right), left, right);
        const auto scheme = ColorScheme::lightDefault();
        DiffEditor editor(Side::Left);
        showEditor(editor, model, scheme);
        editor.edit()->area()->verticalScrollBar()->setValue(23);
        editor.resize(500, 250);
        QApplication::processEvents();
        const auto& vp = editor.edit()->area()->viewportState();
        const int y = vp.contentOffsetY + (25 - vp.firstVisibleLine) * vp.lineHeight;
        QVERIFY(y >= 0 && y < vp.viewportHeight);
        QCOMPARE(pixelAt(editor, y), scheme.sideMarginInsertStripe);
    }
};

QTEST_MAIN(TestDiffEditor)
#include "test_diff_editor.moc"
