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
#include <future>
#include <limits>
#include <qce/ExtraSelection.h>

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
        for (auto malformed : {TextSnapshot{{"embedded\nnewline"}, {}, {}, {}},
                               TextSnapshot{{}, true, {}, {}},
                               TextSnapshot{{"x"}, false, {LineEnding::LF}, {}}}) {
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
        auto* metadata = widget.findChild<QLabel*>("diffTextMetadata");
        QVERIFY(metadata);
        QVERIFY(metadata->text().contains("parent"));
        QVERIFY(metadata->text().contains("No final newline"));
        QVERIFY(metadata->text().contains("CRLF"));
        QCOMPARE(widget.leftEditor()->edit()->document()->lineCount(), 1);
        QCOMPARE(widget.rightEditor()->edit()->document()->lineCount(), 1);
        widget.clearComparison();
        QVERIFY(metadata->isHidden());
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
