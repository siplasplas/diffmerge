#include <QTest>
#include <QProcess>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
class TestResolveCli : public QObject {
    Q_OBJECT
    static void write(const QString& path,const QByteArray& bytes) { QFile f(path); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(bytes),bytes.size()); }
    static QByteArray read(const QString& path) { QFile f(path); if(!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
    static int run(const QStringList& args,QByteArray* output=nullptr) { QProcess p; p.start(RESOLVE_BINARY,args); if(!p.waitForFinished(5000)) return -1; if(output) *output=p.readAllStandardOutput(); return p.exitCode(); }
private slots:
    void automaticAndReport() {
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto input=dir.filePath("in.cpp"),out=dir.filePath("out.cpp"),report=dir.filePath("report.json");
        write(input,"prefix\r\n<<<<<<<\nguard();\nold();\n|||||||\nold();\n=======\nnew();\n>>>>>>>\ntail");
        QCOMPARE(run({input,"-o",out,"--report",report}),0); QCOMPARE(read(out),QByteArray("prefix\r\nguard();\nnew();\ntail"));
        const auto json=QJsonDocument::fromJson(read(report)).object(); QCOMPARE(json["status"].toString(),QString("clean")); QVERIFY(json["publication"].toObject()["contentPublished"].toBool());
        QCOMPARE(run({input,"-o",out}),2); QCOMPARE(run({input,"-o",input,"--overwrite"}),2);
    }
    void unresolvedStrictAndDraft() {
        QTemporaryDir dir; const auto input=dir.filePath("in.cpp"),out=dir.filePath("out.cpp"),report=dir.filePath("report.json"),draft=dir.filePath("draft.cpp");
        const QByteArray bytes="<<<<<<<\nleft();\n=======\nright();\n>>>>>>>\n"; write(input,bytes);
        QCOMPARE(run({input,"-o",out,"--require-clean","--report",report}),1); QVERIFY(!QFile::exists(out));
        QCOMPARE(run({input,"-o",out,"--review-draft",draft,"--report",report,"--overwrite"}),1); QCOMPARE(read(out),bytes); QCOMPARE(read(draft),QByteArray("right();\n"));
        QVERIFY(!read(draft).contains("TODO")); QCOMPARE(QJsonDocument::fromJson(read(report)).object()["reviewDraft"].toObject()["status"].toString(),QString("draft-needs-review"));
    }
    void validationAndCheck() {
        QTemporaryDir dir; const auto input=dir.filePath("in.cpp"),out=dir.filePath("out.cpp");
        write(input,"<<<<<<<\nmissing\n"); QByteArray stdoutBytes;
        QCOMPARE(run({input,"-o",out},&stdoutBytes),2); QVERIFY(stdoutBytes.isEmpty()); QVERIFY(!QFile::exists(out));
        write(input,"no markers\r\n"); QCOMPARE(run({input,"--check"},&stdoutBytes),0); QVERIFY(stdoutBytes.isEmpty());
        QCOMPARE(run({input,"--max-input-bytes","1"}),3); QCOMPARE(run({input},&stdoutBytes),0); QCOMPARE(stdoutBytes,read(input));
    }
};
QTEST_GUILESS_MAIN(TestResolveCli)
#include "test_resolve_cli.moc"
