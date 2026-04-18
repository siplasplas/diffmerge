// Integration tests for the diffmerge CLI. Each test invokes the compiled
// binary with real fixture files on disk and verifies exit code + output.

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTest>

class TestCli : public QObject {
    Q_OBJECT

public:
    // Paths supplied by CMake via -D compile definitions.
    static QString binaryPath() {
        return QStringLiteral(DIFFMERGE_CLI_BINARY);
    }
    static QString fixturePath(const QString& name) {
        return QStringLiteral(DIFFMERGE_CLI_FIXTURES "/") + name;
    }

private:
    // Run the CLI with given args; return (exitCode, stdout, stderr).
    struct RunResult {
        int exitCode = -1;
        QString stdOut;
        QString stdErr;
    };

    RunResult run(const QStringList& args) {
        QProcess p;
        p.start(binaryPath(), args);
        p.waitForFinished(10000);
        RunResult r;
        r.exitCode = p.exitCode();
        r.stdOut = QString::fromUtf8(p.readAllStandardOutput());
        r.stdErr = QString::fromUtf8(p.readAllStandardError());
        return r;
    }

private slots:
    // --- Basic operation ---

    void identicalFilesExitZero() {
        // Compare a file to itself - must exit 0, no output.
        const QString path = fixturePath("greek_a.txt");
        auto r = run({path, path});
        QCOMPARE(r.exitCode, 0);
        QVERIFY(r.stdOut.isEmpty());
    }

    void differentFilesExitOne() {
        auto r = run({fixturePath("greek_a.txt"), fixturePath("greek_b.txt")});
        QCOMPARE(r.exitCode, 1);
        QVERIFY(!r.stdOut.isEmpty());
    }

    void missingFileExitTwo() {
        auto r = run({fixturePath("does_not_exist.txt"),
                      fixturePath("greek_a.txt")});
        QCOMPARE(r.exitCode, 2);
        QVERIFY(r.stdErr.contains("cannot open"));
    }

    // --- Output contents ---

    void outputShowsChangedLines() {
        auto r = run({fixturePath("greek_a.txt"), fixturePath("greek_b.txt")});
        // "beta" → "BETA": both sides shown with < / > prefixes.
        QVERIFY(r.stdOut.contains("< beta"));
        QVERIFY(r.stdOut.contains("> BETA"));
        // "iota" newly added in right file.
        QVERIFY(r.stdOut.contains("> iota"));
    }

    // Normal diff format: "LcR", "LaR", "LdR" range commands (no "@@").
    void outputShowsRangeCommands() {
        auto r = run({fixturePath("greek_a.txt"), fixturePath("greek_b.txt")});
        QVERIFY(r.stdOut.contains("2c2"));   // beta → BETA
        QVERIFY(r.stdOut.contains("6c6"));   // zeta → ZETA_NEW
        QVERIFY(r.stdOut.contains("8a9"));   // iota appended
        QVERIFY(!r.stdOut.contains("@@"));   // not unified format
    }

    // Replace hunks must have "---" separator between < and > lines.
    void outputShowsReplaceSeparator() {
        auto r = run({fixturePath("greek_a.txt"), fixturePath("greek_b.txt")});
        QVERIFY(r.stdOut.contains("\n---\n"));
    }

    // --- Brief mode ---

    void briefModeForIdenticalIsSilent() {
        const QString path = fixturePath("greek_a.txt");
        auto r = run({"--brief", path, path});
        QCOMPARE(r.exitCode, 0);
        QVERIFY(r.stdOut.isEmpty());
    }

    void briefModeForDifferentPrintsOneLine() {
        auto r = run({"--brief",
                      fixturePath("greek_a.txt"),
                      fixturePath("greek_b.txt")});
        QCOMPARE(r.exitCode, 1);
        QVERIFY(r.stdOut.contains("differ"));
        QVERIFY(!r.stdOut.contains("@@"));  // No hunks in brief mode
    }

    // --- Options affecting the diff ---

    void ignoreCaseMakesFilesEqual() {
        auto r = run({"-i",
                      fixturePath("ws_a.txt"),
                      fixturePath("ws_b.txt")});
        // ws files differ in case AND whitespace - case-only fold
        // won't make them equal.
        QCOMPARE(r.exitCode, 1);
    }

    void ignoreWhitespaceAndCaseTogether() {
        auto r = run({"-iw",
                      fixturePath("ws_a.txt"),
                      fixturePath("ws_b.txt")});
        // Now they should normalize to the same content.
        QCOMPARE(r.exitCode, 0);
    }

    void unknownOptionExitsError() {
        auto r = run({"-Q",
                      fixturePath("greek_a.txt"),
                      fixturePath("greek_b.txt")});
        QCOMPARE(r.exitCode, 2);
        QVERIFY(r.stdErr.contains("unknown option"));
    }

    // --- Help ---

    void helpFlagExitsZero() {
        auto r = run({"--help"});
        QCOMPARE(r.exitCode, 0);
        QVERIFY(r.stdOut.contains("Usage:"));
    }

    void missingArgumentsReportsError() {
        auto r = run({});
        QCOMPARE(r.exitCode, 2);
        QVERIFY(r.stdErr.contains("missing"));
    }

    // --- Code file (real-world-ish) ---

    void codeFilesDiffCorrectly() {
        auto r = run({fixturePath("code_a.cpp"),
                      fixturePath("code_b.cpp")});
        QCOMPARE(r.exitCode, 1);
        // Added line "#include <string>"
        QVERIFY(r.stdOut.contains("> #include <string>"));
        // Added function "multiply"
        QVERIFY(r.stdOut.contains("int multiply"));
    }
};

QTEST_APPLESS_MAIN(TestCli)
#include "test_cli.moc"
