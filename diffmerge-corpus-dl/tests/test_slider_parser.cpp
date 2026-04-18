// Unit tests for SliderParser — pure parsing logic, no network needed.

#include <QObject>
#include <QTest>
#include <QTemporaryFile>
#include <QTextStream>

#include "../src/SliderParser.h"

using namespace diffmerge::corpus;

class TestSliderParser : public QObject {
    Q_OBJECT

private slots:

    // --- parseRepoPath ---

    void parseRepoPath_stripsGithubPrefixAndGitSuffix() {
        const QString r = parseRepoPath(
            QStringLiteral("https://github.com/AFNetworking/AFNetworking.git"));
        QCOMPARE(r, QStringLiteral("/AFNetworking/AFNetworking"));
    }

    void parseRepoPath_noGitSuffix() {
        const QString r = parseRepoPath(
            QStringLiteral("https://github.com/git/git"));
        QCOMPARE(r, QStringLiteral("/git/git"));
    }

    void parseRepoPath_nonGithubReturnsEmpty() {
        QVERIFY(parseRepoPath(
            QStringLiteral("https://git-wip-us.apache.org/repos/asf/ant.git"))
            .isEmpty());
    }

    void parseRepoPath_trimsWhitespace() {
        const QString r = parseRepoPath(
            QStringLiteral("  https://github.com/user/repo.git  "));
        QCOMPARE(r, QStringLiteral("/user/repo"));
    }

    // --- parseSliderLine ---

    void parseSliderLine_delete() {
        const QString line = QStringLiteral(
            "3e3d94f93828f5eeb11fc1218c3bc45399e9a66e:AFNetworking/AFRestClient.h "
            "5cf1028433228b20e7cf30a463353981809fcd0b:AFNetworking/AFRestClient.h "
            "- 77 -2");
        const SliderEntry e = parseSliderLine(line, QStringLiteral("/AFNetworking/AFNetworking"));

        QCOMPARE(e.sign, '-');
        QCOMPARE(e.blockBegin, 77);
        QCOMPARE(e.delta, -2);
        QCOMPARE(e.ext, QStringLiteral(".h"));
        QVERIFY(e.url[0].contains(QStringLiteral("3e3d94f93828f5eeb11fc1218c3bc45399e9a66e")));
        QVERIFY(e.url[0].contains(QStringLiteral("AFNetworking/AFRestClient.h")));
        QVERIFY(e.url[1].contains(QStringLiteral("5cf1028433228b20e7cf30a463353981809fcd0b")));
        QVERIFY(e.url[0].startsWith(QStringLiteral("https://raw.githubusercontent.com")));
    }

    void parseSliderLine_insert() {
        const QString line = QStringLiteral(
            "6f63157e33149226bfc2f014748cb5ef5c9a3a9b:AFNetworking/AFURLConnectionOperation.h "
            "f2ae5c40ba087957883f77c826834d9068bc0981:AFNetworking/AFURLConnectionOperation.h "
            "+ 43 -1");
        const SliderEntry e = parseSliderLine(line, QStringLiteral("/AFNetworking/AFNetworking"));
        QCOMPARE(e.sign, '+');
        QCOMPARE(e.blockBegin, 43);
        QCOMPARE(e.delta, -1);
    }

    void parseSliderLine_urlContainsRepoAndHash() {
        const QString line = QStringLiteral(
            "aabbccdd1122aabbccdd1122aabbccdd1122aabb:src/foo.cpp "
            "11223344aabbccdd11223344aabbccdd11223344:src/foo.cpp "
            "+ 10 0");
        const SliderEntry e = parseSliderLine(line, QStringLiteral("/myuser/myrepo"));
        QCOMPARE(e.url[0],
            QStringLiteral("https://raw.githubusercontent.com/myuser/myrepo/"
                           "aabbccdd1122aabbccdd1122aabbccdd1122aabb/src/foo.cpp"));
        QCOMPARE(e.url[1],
            QStringLiteral("https://raw.githubusercontent.com/myuser/myrepo/"
                           "11223344aabbccdd11223344aabbccdd11223344/src/foo.cpp"));
    }

    void parseSliderLine_malformedLineReturnsEmpty() {
        const SliderEntry e = parseSliderLine(
            QStringLiteral("not_a_valid_line"), QStringLiteral("/user/repo"));
        QVERIFY(e.url[0].isEmpty());
    }

    // --- loadInfoFile / loadSlidersFile via temp files ---

    void loadInfoFile_readsFirstLine() {
        QTemporaryFile f;
        f.open();
        QTextStream(&f) << "https://github.com/git/git.git\n";
        f.flush();
        const QString repo = loadInfoFile(f.fileName());
        QCOMPARE(repo, QStringLiteral("/git/git"));
    }

    void loadSlidersFile_parsesTwoEntries() {
        QTemporaryFile f;
        f.open();
        QTextStream s(&f);
        s << "3e3d94f93828f5eeb11fc1218c3bc45399e9a66e:AFNetworking/AFRestClient.h "
             "5cf1028433228b20e7cf30a463353981809fcd0b:AFNetworking/AFRestClient.h "
             "- 77 -2\n";
        s << "6f63157e33149226bfc2f014748cb5ef5c9a3a9b:AFNetworking/AFURLConnectionOperation.h "
             "f2ae5c40ba087957883f77c826834d9068bc0981:AFNetworking/AFURLConnectionOperation.h "
             "+ 43 -1\n";
        s.flush();
        const auto entries = loadSlidersFile(f.fileName(),
                                             QStringLiteral("/AFNetworking/AFNetworking"));
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries[0].sign, '-');
        QCOMPARE(entries[1].sign, '+');
    }

    void loadSlidersFile_skipsBlankLines() {
        QTemporaryFile f;
        f.open();
        QTextStream s(&f);
        s << "\n"
          << "3e3d94f93828f5eeb11fc1218c3bc45399e9a66e:foo.cpp "
             "5cf1028433228b20e7cf30a463353981809fcd0b:foo.cpp + 5 0\n"
          << "\n";
        s.flush();
        const auto entries = loadSlidersFile(f.fileName(), QStringLiteral("/u/r"));
        QCOMPARE(entries.size(), 1);
    }
};

QTEST_APPLESS_MAIN(TestSliderParser)
#include "test_slider_parser.moc"
