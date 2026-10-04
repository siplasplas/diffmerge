#include <QApplication>
#include <QImage>
#include <QScrollBar>
#include <QSplitterHandle>
#include <QTest>
#include <QVBoxLayout>

#include <diffcore/DiffEngine.h>
#include <qce/CodeEditArea.h>

#include "../src/editor/DiffEditor.h"
#include "../src/fileview/DiffConnectorSplitter.h"

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
