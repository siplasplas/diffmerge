#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#ifdef DIFFMERGE_TEST_SYNTAX_STARTUP
#include "../src/MainWindow.h"
#include <QAbstractButton>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>
#include <qce/kate/KateSyntaxVersion.h>
#endif
#include <qce/CodeEditArea.h>
#include <qce/kate/KatePaths.h>
#include <diffmerge/DiffEditor.h>
#include <diffmerge/FileDiffWidget.h>
#include "../src/editor/DiffHighlighter.h"
#include "../src/editor/SyntaxLoader.h"

using namespace diffmerge::gui;

namespace {
class SyntaxDirectory {
public:
    SyntaxDirectory() : previous(qce::kate::dataDir()) { qce::kate::setDataDirOverride(directory.path()); }
    ~SyntaxDirectory() { qce::kate::setDataDirOverride(previous); }
    QTemporaryDir directory;
    QString previous;
};

bool write(const QString& path, const QByteArray& content) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

bool installDefinition(const QString& directory) {
    if (!QDir(directory).mkpath("syntax")) return false;
    return write(directory + "/syntax/test.xml", R"XML(<?xml version="1.0"?>
<language name="DiffMerge Test" section="Tests" extensions="*.fixturecpp" version="1" kateversion="5.0">
<highlighting><contexts>
<context name="Normal" attribute="Normal" lineEndContext="#stay">
<WordDetect String="if" attribute="Keyword"/>
<RegExpr String="&lt;&lt;([A-Z]+)" attribute="String" context="Heredoc"/>
<StringDetect String="/*" attribute="Comment" context="Comment" beginRegion="comment"/>
<DetectIdentifier attribute="Normal"/>
</context>
<context name="Comment" attribute="Comment" lineEndContext="#stay">
<StringDetect String="*/" attribute="Comment" context="#pop" endRegion="comment"/>
</context>
<context name="Heredoc" attribute="String" lineEndContext="#stay">
<RegExpr String="^%1$" dynamic="true" attribute="String" context="#pop"/>
</context>
</contexts><itemDatas>
<itemData name="Normal" defStyleNum="dsNormal"/>
<itemData name="Keyword" defStyleNum="dsKeyword"/>
<itemData name="Comment" defStyleNum="dsComment"/>
<itemData name="String" defStyleNum="dsString"/>
</itemDatas></highlighting>
</language>)XML");
}

qce::TextAttribute attributeAt(const qce::IHighlighter& highlighter,
                              const QVector<qce::StyleSpan>& spans, int column) {
    for (const auto& span : spans)
        if (column >= span.start && column < span.start + span.length)
            return highlighter.attributes().at(span.attributeId);
    return {};
}
}

class TestSyntaxHighlighting : public QObject {
    Q_OBJECT
private slots:
#ifdef DIFFMERGE_TEST_SYNTAX_STARTUP
    void startupChecksLocalManifestsAndRemembersDeclinedDownload() {
        SyntaxDirectory data;
        QVERIFY(installDefinition(data.directory.path()));
        // Isolate application settings from the user's normal configuration.
        const auto originalName = QApplication::applicationName();
        const auto originalOrganization = QApplication::organizationName();
        const auto originalFormat = QSettings::defaultFormat();
        struct RestoreSettings {
            QString name, organization;
            QSettings::Format format;
            ~RestoreSettings() {
                QApplication::setApplicationName(name);
                QApplication::setOrganizationName(organization);
                QSettings::setDefaultFormat(format);
            }
        } restore{originalName, originalOrganization, originalFormat};
        QApplication::setApplicationName("SyntaxStartupTest");
        QApplication::setOrganizationName("DiffMergeTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, data.directory.path());
        QSettings().clear();

        int offers = 0;
        QTimer dismiss;
        dismiss.setInterval(1);
        connect(&dismiss, &QTimer::timeout, this, [&] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                auto* message = qobject_cast<QMessageBox*>(widget);
                if (!message || !message->isVisible()) continue;
                if (message->windowTitle() != "Syntax Definitions") continue;
                ++offers;
                QVERIFY(message->text().contains(data.directory.path()));
                message->button(QMessageBox::No)->click();
            }
        });
        dismiss.start();
        {
            MainWindow first;
            QTest::qWait(20);
            QCOMPARE(offers, 1); // XML alone is not a complete manifest-backed dataset.
            QVERIFY(QSettings().value("syntaxDownloadDeclined").toBool());
        }
        {
            MainWindow second;
            QTest::qWait(20);
            QCOMPARE(offers, 1); // No repeated question after declining.
        }
        QSettings().clear();
        const auto updateName = qce::kate::supportedSyntaxVersion().updateFileName();
        QVERIFY(write(data.directory.filePath(updateName),
            "<Definitions><Definition name=\"DiffMerge Test\" version=\"1\" url=\"https://example.invalid/test.xml\"/></Definitions>"));
        QVERIFY(QDir(data.directory.path()).mkpath("themes"));
        QVERIFY(write(data.directory.filePath("theme-data.qrc"), "<RCC><qresource><file>fixture.theme</file></qresource></RCC>"));
        QVERIFY(write(data.directory.filePath("themes/fixture.theme"), "{}"));
        {
            MainWindow complete;
            QTest::qWait(20);
            QCOMPARE(offers, 1); // Complete local data never starts a download or asks.
        }
    }
#endif

    void syntaxAndCharacterDiffCompose_data() {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    void syntaxAndCharacterDiffCompose() {
        QFETCH(bool, dark);
        SyntaxDirectory data;
        QVERIFY(data.directory.isValid());
        QVERIFY(installDefinition(data.directory.path()));
        QString language;
        auto syntax = loadSyntax("sample.fixturecpp", dark, language);
        QVERIFY(syntax);
        QCOMPARE(language, QString("DiffMerge Test"));
        QVector<qce::StyleSpan> original;
        qce::HighlightState originalEnd;
        syntax->highlightLine("if value", syntax->initialState(), original, originalEnd);
        const auto keyword = attributeAt(*syntax, original, 0);
        QVERIFY(keyword.bold);
        QVERIFY(keyword.foreground.isValid());

        DiffHighlighter composed;
        composed.setSyntax(std::move(syntax));
        const auto scheme = dark ? ColorScheme::darkDefault() : ColorScheme::lightDefault();
        composed.setData({{{1, 1}, {4, 2}}}, scheme.replaceCharBg);
        QVector<qce::StyleSpan> spans;
        qce::HighlightState out;
        composed.highlightLine("if value", composed.initialState(), spans, out);
        QCOMPARE(attributeAt(composed, spans, 0).foreground, keyword.foreground);
        QVERIFY(attributeAt(composed, spans, 0).bold);
        QVERIFY(!attributeAt(composed, spans, 0).background.isValid());
        QCOMPARE(attributeAt(composed, spans, 1).foreground, keyword.foreground);
        QVERIFY(attributeAt(composed, spans, 1).bold);
        QCOMPARE(attributeAt(composed, spans, 1).background, scheme.replaceCharBg);
        QCOMPARE(attributeAt(composed, spans, 4).background, scheme.replaceCharBg);
        QVERIFY(!attributeAt(composed, spans, 6).background.isValid());
        int end = 0;
        for (const auto& span : spans) {
            QVERIFY(span.start >= end);
            QVERIFY(span.length > 0);
            QVERIFY(span.start + span.length <= 8);
            end = span.start + span.length;
        }
    }

    void multilineSyntaxRetainsStateWithoutFolding() {
        SyntaxDirectory data;
        QVERIFY(installDefinition(data.directory.path()));
        QString language;
        DiffHighlighter composed;
        composed.setSyntax(loadSyntax("sample.fixturecpp", false, language));
        composed.setData({{}, {{0, 6}}, {}}, ColorScheme::lightDefault().replaceCharBg);
        auto state = composed.initialState();
        QVector<qce::StyleSpan> spans;
        QVector<qce::FoldMarker> folds;
        qce::HighlightState next;
        composed.highlightLineWithFolds("/* start", state, spans, next, folds);
        QVERIFY(folds.isEmpty());
        QVERIFY(attributeAt(composed, spans, 3).italic);
        QCOMPARE(next.contextStack.size(), next.captureStack.size());
        state = next;
        composed.highlightLineWithFolds("middle", state, spans, next, folds);
        const auto inside = attributeAt(composed, spans, 0);
        QVERIFY(inside.italic);
        QCOMPARE(inside.background, ColorScheme::lightDefault().replaceCharBg);
        QVERIFY(folds.isEmpty());
        state = next;
        composed.highlightLineWithFolds("end */ if", state, spans, next, folds);
        QVERIFY(attributeAt(composed, spans, 0).italic);
        QVERIFY(attributeAt(composed, spans, 7).bold);
        QVERIFY(!attributeAt(composed, spans, 7).background.isValid());
        QCOMPARE(next.contextStack.size(), next.captureStack.size());
        QCOMPARE(next.contextStack.last(), 3);
        QVERIFY(folds.isEmpty());
    }

    void dynamicSyntaxCapturesSurviveTheDiffLineCounter() {
        SyntaxDirectory data;
        QVERIFY(installDefinition(data.directory.path()));
        QString language;
        DiffHighlighter composed;
        composed.setSyntax(loadSyntax("sample.fixturecpp", false, language));
        composed.setData({{}, {{0, 4}}, {}, {}}, ColorScheme::lightDefault().replaceCharBg);
        QVector<qce::StyleSpan> spans;
        qce::HighlightState state = composed.initialState(), next;
        composed.highlightLine("<<END", state, spans, next);
        QVERIFY(next.captureStack.size() >= 3);
        QCOMPARE(next.captureStack[next.captureStack.size() - 2].value(1), QString("END"));
        state = next;
        composed.highlightLine("body", state, spans, next);
        QCOMPARE(attributeAt(composed, spans, 0).background, ColorScheme::lightDefault().replaceCharBg);
        QCOMPARE(next.captureStack[next.captureStack.size() - 2].value(1), QString("END"));
        state = next;
        composed.highlightLine("END", state, spans, next);
        QCOMPARE(next.contextStack.size(), 2); // Syntax root and our line counter only.
        state = next;
        composed.highlightLine("if", state, spans, next);
        QVERIFY(attributeAt(composed, spans, 0).bold);
        QVERIFY(!attributeAt(composed, spans, 0).background.isValid());
    }

    void missingXmlCanBeReloadedAndSnapshotHintsAreIndependentOfLabels() {
        SyntaxDirectory data;
        const auto prepared = prepareComparison(
            TextSnapshot::fromText("if before", "Parent revision", "before.fixturecpp"),
            TextSnapshot::fromText("if after", "Current revision", "after.fixturecpp"));
        QCOMPARE(prepared.status, PreparationStatus::Ready);
        FileDiffWidget view;
        view.setComparison(prepared.comparison);
        auto* editor = view.leftEditor();
        QVERIFY(editor->syntaxLanguage().isEmpty());
        QCOMPARE(editor->syntaxFileName(), QString("before.fixturecpp"));
        QVERIFY(editor->edit()->area()->highlighter()); // Diff still works without XML.
        QVERIFY(!editor->edit()->area()->foldingProvider());
        QVERIFY(installDefinition(data.directory.path()));
        view.reloadSyntaxDefinitions();
        QCOMPARE(editor->syntaxLanguage(), QString("DiffMerge Test"));
        QVERIFY(view.setSearchHighlights(Side::Left, {{0, 0, 2}}));
        view.clearSearchHighlights();
        QVERIFY(editor->edit()->area()->highlighter());
        editor->setColorScheme(ColorScheme::darkDefault());
        QCOMPARE(editor->syntaxLanguage(), QString("DiffMerge Test"));
        QVERIFY(!editor->edit()->area()->foldingProvider());
        view.clearComparison();
        QVERIFY(editor->syntaxFileName().isEmpty());
        QVERIFY(editor->syntaxLanguage().isEmpty());
        QVERIFY(!editor->edit()->area()->highlighter());
    }

    void equalFilesAndFileLoadingUseSyntaxButNeverFold() {
        SyntaxDirectory data;
        QVERIFY(installDefinition(data.directory.path()));
        const auto path = data.directory.filePath("source.fixturecpp");
        QVERIFY(write(path, "if value\n/* comment\ncontinues */\n"));
        FileDiffWidget view;
        QVERIFY(view.loadFromPaths(path, path));
        QCOMPARE(view.changeCount(), 0);
        for (auto* editor : {view.leftEditor(), view.rightEditor()}) {
            QCOMPARE(editor->syntaxLanguage(), QString("DiffMerge Test"));
            QVERIFY(editor->edit()->area()->highlighter());
            QVERIFY(!editor->edit()->area()->foldingProvider());
        }
        view.setContent({"if value"}, {"if value"});
        QVERIFY(!view.leftEditor()->edit()->area()->highlighter());
        view.setSyntaxFileName(Side::Left, "memory.fixturecpp");
        QCOMPARE(view.leftEditor()->syntaxLanguage(), QString("DiffMerge Test"));
        view.setSyntaxFileName(Side::Left, "unknown.extension");
        QVERIFY(view.leftEditor()->syntaxLanguage().isEmpty());
        QVERIFY(!view.leftEditor()->edit()->area()->highlighter());
    }
};

QTEST_MAIN(TestSyntaxHighlighting)
#include "test_syntax_highlighting.moc"
