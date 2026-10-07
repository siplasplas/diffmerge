#include <diffcore/ConflictResolution.h>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QTextStream>
#include <csignal>
#include <thread>
#include <chrono>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif
using namespace diffcore;
namespace {
volatile std::sig_atomic_t interrupted=0;
void interrupt(int) { interrupted=1; }
int statusCode(ConflictStatus status) {
    return status==ConflictStatus::Cancelled ? 130 : status==ConflictStatus::ResourceLimit ? 3 : 2;
}
bool aliases(const QString& a,const QString& b) {
    if(a.isEmpty() || b.isEmpty() || a=="-" || b=="-") return false;
    const QFileInfo x(a),y(b);
    if(x.absoluteFilePath()==y.absoluteFilePath()) return true;
    if(x.exists() && y.exists() && x.canonicalFilePath()==y.canonicalFilePath()) return true;
#ifdef Q_OS_UNIX
    struct stat xs{},ys{};
    if(::stat(QFile::encodeName(a).constData(),&xs)==0 && ::stat(QFile::encodeName(b).constData(),&ys)==0)
        return xs.st_dev==ys.st_dev && xs.st_ino==ys.st_ino;
#endif
    return false;
}
bool writeFile(const QString& path,const QByteArray& bytes,QString& error) {
    QSaveFile file(path); file.setDirectWriteFallback(false);
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()) {
        error=QStringLiteral("Could not publish %1: %2").arg(path,file.errorString()); return false;
    }
    return true;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv); app.setApplicationName("diffmerge-resolve");
    app.setApplicationVersion("1.5.0");
    QCommandLineParser parser;
    parser.setApplicationDescription("Resolve marker-based text conflicts. Replay policy prefers the selected target for superseded edits. Review drafts require manual acceptance even when they compile.");
    parser.addHelpOption(); parser.addVersionOption();
    parser.addPositionalArgument("INPUT","Existing UTF-8 conflict file; input is never overwritten");
    parser.addOption({{"o","output"},"Output path or '-' for stdout (default)","PATH","-"});
    parser.addOption({"report","Write a JSON report with decisions and original evidence","PATH"});
    parser.addOption({"review-draft","Also write a marker-free TARGET-retaining draft; requires --report. Decisions remain pending; rescanning the draft alone cannot restore them.","PATH"});
    parser.addOption({"policy","Acceptance policy: replay or conservative","POLICY","replay"});
    parser.addOption({"target","Target side for directional rules: right or left","SIDE","right"});
    parser.addOption({"marker-size","Marker run length","N","7"});
    parser.addOption({"literal-marker-line","Treat this one-based input line as literal source (repeatable)","N"});
    parser.addOption({"check","Analyze without publishing content"});
    parser.addOption({"require-clean","Suppress content if any decision needs review"});
    parser.addOption({"overwrite","Permit replacing existing output/report files"});
    parser.addOption({"max-input-bytes","Input byte budget","N","33554432"});
    parser.addOption({"max-output-bytes","Output byte budget","N","67108864"});
    parser.addOption({"max-work","Comparison work budget","N","20000000"});
    parser.addOption({"max-trace-entries","Sequence trace-entry budget","N","1000000"});
    const auto fail=[](const QString& message,int code=2) { QTextStream(stderr)<<"diffmerge-resolve: "<<message<<'\n'; return code; };
    if(!parser.parse(app.arguments())) return fail(parser.errorText());
    if(parser.isSet("help")) parser.showHelp();
    if(parser.isSet("version")) parser.showVersion();
    if(parser.positionalArguments().size()!=1) return fail("One INPUT path is required");
    const QString inputPath=parser.positionalArguments()[0],outputPath=parser.value("output"),reportPath=parser.value("report"),draftPath=parser.value("review-draft");
    const bool check=parser.isSet("check"),strict=parser.isSet("require-clean"),overwrite=parser.isSet("overwrite");
    if(check && (parser.isSet("output") || overwrite || !draftPath.isEmpty())) return fail("--check cannot be combined with --output, --overwrite or --review-draft");
    if(!draftPath.isEmpty() && (reportPath.isEmpty() || strict)) return fail("--review-draft requires --report and cannot be combined with --require-clean");
    ResolutionOptions options;
    if(parser.value("policy")=="conservative") options.policy=ResolutionPolicy::Conservative;
    else if(parser.value("policy")!="replay") return fail("Unknown policy");
    if(parser.value("target")=="left") options.target=ResolutionTarget::Left;
    else if(parser.value("target")!="right") return fail("Unknown target side");
    MarkerOptions markers; bool valid=false;
    markers.markerSize=parser.value("marker-size").toInt(&valid);
    if(!valid || markers.markerSize<1 || markers.markerSize>200000) return fail("Invalid marker size");
    for(const auto& value:parser.values("literal-marker-line")) {
        const int line=value.toInt(&valid); if(!valid || line<1) return fail("Literal marker lines must be positive integers");
        markers.literalMarkerLines.insert(line-1);
    }
    ConflictLimits limits;
    for(auto item:{std::pair<const char*,quint64*>{"max-input-bytes",&limits.maxInputBytes},{"max-output-bytes",&limits.maxOutputBytes},{"max-work",&limits.maxWork},{"max-trace-entries",&limits.maxTraceEntries}}) {
        *item.second=parser.value(item.first).toULongLong(&valid);
        if(!valid || *item.second==0) return fail("Resource limits must be positive integers");
    }
    const QFileInfo inputInfo(inputPath);
    if(!inputInfo.isFile() || inputInfo.isSymLink()) return fail("INPUT must be a regular file, not a symbolic link");
    if(inputInfo.size()<0 || quint64(inputInfo.size())>limits.maxInputBytes) return fail("Input exceeds the byte budget",3);
    QStringList destinations;
    if(!check && outputPath!="-") destinations.append(outputPath);
    if(!reportPath.isEmpty()) destinations.append(reportPath);
    if(!draftPath.isEmpty()) destinations.append(draftPath);
    for(int i=0;i<destinations.size();++i) {
        const auto path=destinations[i]; const QFileInfo info(path);
        if(path=="-" || info.isSymLink() || (info.exists() && !info.isFile())) return fail("Reports and file outputs must use regular file paths");
        if(aliases(inputPath,path)) return fail("An output aliases the input");
        if(info.exists() && !overwrite) return fail("Output already exists; use --overwrite: "+path);
        for(int j=0;j<i;++j) if(aliases(path,destinations[j])) return fail("Output artifacts alias each other");
    }
    QFile input(inputPath);
    if(!input.open(QIODevice::ReadOnly)) return fail(input.errorString());
    const auto bytes=input.read(qint64(std::min<quint64>(limits.maxInputBytes,INT_MAX))+1);
    if(input.error()!=QFileDevice::NoError) return fail(input.errorString());
    if(quint64(bytes.size())>limits.maxInputBytes) return fail("Input exceeds the byte budget",3);
    std::signal(SIGINT,interrupt);
#ifdef SIGPIPE
    std::signal(SIGPIPE,SIG_IGN);
#endif
    CancellationToken token;
    std::jthread monitor([token](std::stop_token stop) { while(!stop.stop_requested()) { if(interrupted) { token.requestCancellation(); return; } std::this_thread::sleep_for(std::chrono::milliseconds(10)); } });
    const auto plan=planConflictResolution(bytes,markers,options,limits,token,[](int n,int total) {
        if(total>10 && (n==total || n%25==0)) QTextStream(stderr)<<"Analyzed "<<n<<'/'<<total<<" conflicts\n";
    });
    const auto result=materializeResolution(plan,false,limits,token);
    std::optional<MaterializedResolution> draft;
    if(!draftPath.isEmpty() && result.status==ConflictStatus::Complete) draft=materializeResolution(plan,true,limits,token);
    auto report=resolutionReport(plan,result,draft);
    QString error;
    if(result.status!=ConflictStatus::Complete || (draft && draft->status!=ConflictStatus::Complete)) {
        const auto status=result.status!=ConflictStatus::Complete ? result.status:draft->status;
        if(!reportPath.isEmpty() && !writeFile(reportPath,QJsonDocument(report).toJson(),error)) return fail(error);
        return fail(result.status!=ConflictStatus::Complete ? result.message:draft->message,statusCode(status));
    }
    if(interrupted) return fail("Cancelled",130);
    if(!check && (!strict || result.clean)) {
        if(outputPath=="-") {
            QFile stdoutFile;
            if(!stdoutFile.open(stdout,QIODevice::WriteOnly) || stdoutFile.write(result.bytes)!=result.bytes.size() || !stdoutFile.flush()) return fail("Could not write stdout");
        } else if(!writeFile(outputPath,result.bytes,error)) return fail(error);
        auto published=report["publication"].toObject(); published["contentPublished"]=true; report["publication"]=published;
    }
    if(draft) {
        if(!writeFile(draftPath,draft->bytes,error)) return fail(error+"; ordinary output may already have been published");
        auto published=report["reviewDraft"].toObject(); published["published"]=true; report["reviewDraft"]=published;
    }
    if(!reportPath.isEmpty() && !writeFile(reportPath,QJsonDocument(report).toJson(),error)) return fail(error+"; content may already have been published");
    const auto counts=report["summary"].toObject();
    QTextStream(stderr)<<"Conflicts: "<<plan.decisions.size()<<"; exact: "<<counts["automaticExact"].toInt()<<"; policy: "<<counts["automaticPolicy"].toInt()<<"; review: "<<counts["needsReview"].toInt()<<'\n';
    for(const auto& d:plan.decisions) if(d.state==DecisionState::NeedsReview || d.state==DecisionState::AutomaticPolicy)
        QTextStream(stderr)<<d.id<<": "<<decisionStateName(d.state)<<" ("<<d.rule<<d.reasons.join(", ")<<")\n";
    return result.clean ? 0:1;
}
