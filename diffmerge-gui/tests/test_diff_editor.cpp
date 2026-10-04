#include <QApplication>
#include <QImage>
#include <QScrollBar>
#include <QSplitterHandle>
#include <QTest>
#include <QVBoxLayout>
#include <QPalette>
#include <QShortcut>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <diffcore/DiffEngine.h>
#include <qce/CodeEditArea.h>

#include <diffmerge/DiffEditor.h>
#include "../src/fileview/DiffConnectorSplitter.h"
#include <diffmerge/FileDiffWidget.h>

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
