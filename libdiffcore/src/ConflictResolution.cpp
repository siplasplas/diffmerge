#include <diffcore/ConflictResolution.h>
#include <diffcore/SequenceDiff.h>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QRegularExpression>
#include <QStringDecoder>
#include <algorithm>
#include <cmath>
#include <set>

namespace diffcore {
namespace {
QByteArray slice(const ParsedConflictFile& file, ByteRange range) { return file.bytes.mid(range.start, range.length); }
std::vector<QByteArray> lines(const QByteArray& bytes) {
    std::vector<QByteArray> result;
    qint64 start = 0;
    for (qint64 i = 0; i < bytes.size(); ++i) {
        if (bytes[i] != '\n' && bytes[i] != '\r') continue;
        if (bytes[i] == '\r' && i + 1 < bytes.size() && bytes[i+1] == '\n') ++i;
        result.push_back(bytes.mid(start, i+1-start)); start = i+1;
    }
    if (start < bytes.size()) result.push_back(bytes.mid(start));
    return result;
}
QVector<ByteRange> lineRanges(ByteRange range, const std::vector<QByteArray>& text) {
    QVector<ByteRange> result; qint64 start = range.start;
    for (const auto& line : text) { result.append({start, line.size()}); start += line.size(); }
    return result;
}
struct Edit { int start, count; QVector<ByteRange> pieces; QByteArray bytes; };
QVector<Edit> edits(const std::vector<QByteArray>& base, const std::vector<QByteArray>& changed,
    ByteRange source, ComputationControl& control) {
    const auto mapping = lineRanges(source, changed);
    QVector<Edit> result;
    for (const auto& h : SequenceDiff::compute(base, changed, {}, &control).hunks) {
        if (h.type == ChangeType::Equal) continue;
        Edit edit{h.leftRange.start, h.leftRange.count, {}, {}};
        for (int i = h.rightRange.start; i < h.rightRange.end(); ++i) {
            edit.pieces.append(mapping[i]); edit.bytes += changed[i];
        }
        result.append(edit);
    }
    return result;
}
std::vector<QString> tokens(const QByteArray& bytes, ComputationControl& control) {
    const QString text = QString::fromUtf8(bytes);
    std::vector<QString> result;
    int start = -1;
    const auto word = [](QChar c) { return c.isLetterOrNumber() || c == '_'; };
    for (int i = 0; i <= text.size(); ++i) {
        control.step();
        if (i < text.size() && word(text[i])) { if (start < 0) start = i; continue; }
        if (start >= 0) { result.push_back(text.mid(start, i-start)); start = -1; }
        if (i < text.size() && !text[i].isSpace()) result.push_back(text.mid(i,1));
    }
    return result;
}
double overlap(const std::vector<QString>& a, const std::vector<QString>& b, ComputationControl& control) {
    const auto result = SequenceDiff::compute(a,b,{},&control);
    qint64 equal = 0;
    for (const auto& h : result.hunks) if (h.type == ChangeType::Equal) equal += h.leftRange.count;
    return a.empty() && b.empty() ? 1 : 2.0 * equal / (a.size()+b.size());
}
bool independentAddition(const QVector<Edit>& script) {
    for (const auto& edit : script) if (edit.count == 0 || edit.pieces.size() > edit.count) return true;
    return false;
}
bool repeated(const std::vector<QByteArray>& base) {
    std::set<QByteArray> unique;
    for (const auto& line : base) if (!unique.insert(line).second) return true;
    return false;
}
std::optional<QVector<ByteRange>> compose(const std::vector<QByteArray>& base,
    ByteRange baseRange, QVector<Edit> a, const QVector<Edit>& b) {
    if (repeated(base)) return std::nullopt;
    for (const auto& other : b) {
        bool identical = false;
        for (const auto& edit : a) {
            if (edit.start == other.start && edit.count == other.count && edit.bytes == other.bytes) { identical = true; break; }
            if (edit.count == 0 && other.count == 0 && edit.start == other.start) return std::nullopt;
            if (edit.count && other.count && std::max(edit.start,other.start) < std::min(edit.start+edit.count,other.start+other.count)) return std::nullopt;
            if (!edit.count && other.count && edit.start >= other.start && edit.start <= other.start+other.count) return std::nullopt;
            if (!other.count && edit.count && other.start >= edit.start && other.start <= edit.start+edit.count) return std::nullopt;
        }
        if (!identical) a.append(other);
    }
    std::sort(a.begin(), a.end(), [](const Edit& x, const Edit& y) { return x.start < y.start; });
    const auto original = lineRanges(baseRange,base);
    QVector<ByteRange> result; int cursor = 0;
    for (const auto& edit : a) {
        if (edit.start < cursor) return std::nullopt;
        while (cursor < edit.start) result.append(original[cursor++]);
        result += edit.pieces; cursor += edit.count;
    }
    while (cursor < int(base.size())) result.append(original[cursor++]);
    return result;
}
void accept(ConflictDecision& decision, const ParsedConflictFile& file, const QVector<ByteRange>& pieces,
    const QString& rule, bool policy = false) {
    decision.rule = rule; decision.state = policy ? DecisionState::AutomaticPolicy : DecisionState::AutomaticExact;
    decision.sourceSlices = pieces; QByteArray replacement;
    for (auto range : pieces) replacement += slice(file,range);
    decision.replacement = replacement;
    decision.explanation = policy ? "The selected target supersedes edits to removed material; this policy decision can be overridden."
        : "Compatible textual edits were composed from original source bytes.";
}
bool boundaryComposition(ConflictDecision& decision, const ParsedConflictFile& file,
    ByteRange side, ByteRange target, ByteRange base) {
    const auto b = slice(file,base), s = slice(file,side), t = slice(file,target);
    if (b.isEmpty() || t.isEmpty() || t.contains(b) || s.size() <= b.size() || s.count(b) != 1) return false;
    if (s.endsWith(b)) {
        const ByteRange prefix{side.start,s.size()-b.size()};
        if (t.contains(slice(file,prefix))) return false;
        accept(decision,file,{prefix,target},"prefix-plus-replacement"); return true;
    }
    if (s.startsWith(b)) {
        const ByteRange suffix{side.start+b.size(),s.size()-b.size()};
        if (t.contains(slice(file,suffix))) return false;
        accept(decision,file,{target,suffix},"suffix-plus-replacement"); return true;
    }
    return false;
}
void addCandidate(ConflictDecision& d, const QString& id, const QString& title,
    const QByteArray& bytes, const QVector<ByteRange>& pieces, const QStringList& assumptions = {}) {
    for (const auto& c : d.candidates) if (c.replacement == bytes) return;
    d.candidates.append({id,title,bytes,assumptions,pieces,std::nullopt});
}
// A bounded statement adaptation is a suggestion only. It never establishes symbol identity.
void adaptedCandidate(ConflictDecision& d, const ParsedConflictFile& file, const ConflictBlock& block,
    ByteRange sideRange, ByteRange targetRange, ComputationControl& control) {
    if (!block.base) return;
    const auto base = lines(slice(file,*block.base)), side = lines(slice(file,sideRange));
    auto target = lines(slice(file,targetRange));
    auto script = edits(base,side,sideRange,control);
    int prefix = 0, suffix = 0;
    while (prefix < int(base.size()) && prefix < int(side.size()) && base[prefix] == side[prefix]) ++prefix;
    while (suffix < int(base.size())-prefix && suffix < int(side.size())-prefix &&
           base[base.size()-1-suffix] == side[side.size()-1-suffix]) ++suffix;
    if (int(side.size())-prefix-suffix == 1 && int(base.size())-prefix-suffix >= 2) {
        const auto mapping = lineRanges(sideRange,side);
        script = {{prefix,int(base.size())-prefix-suffix,{mapping[prefix]},side[prefix]}};
    }
    if (script.size() != 1) return;
    const auto& change = script[0];
    if (change.count < 2 || change.pieces.size() != 1) return;
    const QString oldLast = QString::fromUtf8(base[change.start+change.count-1]).trimmed();
    const QString newLine = QString::fromUtf8(change.bytes).trimmed();
    const QRegularExpression assignment(QStringLiteral("^(?:[A-Za-z_][A-Za-z_0-9]*\\s+)+([A-Za-z_][A-Za-z_0-9]*)\\s*="));
    const auto oldMatch = assignment.match(oldLast), newMatch = assignment.match(newLine);
    if (!oldMatch.hasMatch() || !newMatch.hasMatch() || oldMatch.captured(1) != newMatch.captured(1)) return;
    int found = -1;
    QString oldFirst = QString::fromUtf8(base[change.start]).trimmed();
    QString targetFirst;
    for (int i = 0; i+change.count <= int(target.size()); ++i) {
        control.step();
        bool matches = true;
        for (int j=1;j<change.count;++j) if (QString::fromUtf8(target[i+j]).trimmed() != QString::fromUtf8(base[change.start+j]).trimmed()) matches=false;
        if (!matches) continue;
        const auto first = QString::fromUtf8(target[i]).trimmed();
        const auto fm = assignment.match(first), bm = assignment.match(oldFirst);
        if (!fm.hasMatch() || !bm.hasMatch() || fm.captured(1) != bm.captured(1)) continue;
        if (found >= 0) return;
        found = i; targetFirst = first;
    }
    if (found < 0) return;
    const auto oldTokens = tokens(oldFirst.toUtf8(),control), newTokens = tokens(targetFirst.toUtf8(),control);
    if (oldTokens.size() != newTokens.size()) return;
    QString from, to;
    for (size_t i=0;i<oldTokens.size();++i) if (oldTokens[i] != newTokens[i]) {
        if (!from.isEmpty()) return;
        from=oldTokens[i]; to=newTokens[i];
    }
    const QRegularExpression identifier("^[A-Za-z_][A-Za-z_0-9]*$");
    if (!from.isEmpty() && (!identifier.match(from).hasMatch() || !identifier.match(to).hasMatch())) return;
    QString adapted = newLine;
    if (!from.isEmpty()) adapted.replace(QRegularExpression("\\b"+QRegularExpression::escape(from)+"\\b"),to);
    const auto original = target[found]; int leading=0;
    while (leading<original.size() && (original[leading]==' ' || original[leading]=='\t')) ++leading;
    const QByteArray eol = original.endsWith("\r\n") ? QByteArray("\r\n") : original.endsWith('\n') ? QByteArray("\n") : original.endsWith('\r') ? QByteArray("\r") : QByteArray{};
    QByteArray prefixBytes, suffixBytes, targetGroup;
    for(int i=0;i<found;++i) prefixBytes+=target[i];
    for(int i=found;i<found+change.count;++i) targetGroup+=target[i];
    for(int i=found+change.count;i<int(target.size());++i) suffixBytes+=target[i];
    const auto adaptedLine=original.left(leading)+adapted.toUtf8()+eol;
    const auto replayedLine=original.left(leading)+newLine.toUtf8()+eol;
    d.candidates.clear();
    addCandidate(d,"adapted","Apply replayed edit with target identifier",prefixBytes+adaptedLine+suffixBytes,{},
        {"Moved context and identifier correspondence require review.","Deletion of the target-side legacy statements requires review."});
    d.candidates.last().focusRange=ByteRange{prefixBytes.size(),adaptedLine.size()};
    // Keep the unadapted edit in the same target context so only the decision differs.
    if(adaptedLine!=replayedLine) {
        addCandidate(d,"replayed","Apply replayed edit with original identifier",prefixBytes+replayedLine+suffixBytes,{},
            {"The original identifier may no longer be in scope in the moved context."});
        d.candidates.last().focusRange=ByteRange{prefixBytes.size(),replayedLine.size()};
    }
    const auto targetId=targetRange.start==block.right.start ? QString("right"):QString("left");
    addCandidate(d,targetId,"Keep target statements",prefixBytes+targetGroup+suffixBytes,{targetRange},
        {"The replayed simplification is not applied."});
    d.candidates.last().focusRange=ByteRange{prefixBytes.size(),targetGroup.size()};
    QStringList ids{"adapted"}; if(adaptedLine!=replayedLine) ids.append("replayed"); ids.append(targetId);
    d.reviewPresentation=ConflictReviewPresentation{prefixBytes,suffixBytes,from,to,ids};
    d.reasons={"moved-context","delete-modified-line","identifier-adaptation"};
}
ConflictDecision resolveBlock(const ParsedConflictFile& file, const ConflictBlock& block,
    const ResolutionOptions& options, ComputationControl& control) {
    ConflictDecision d; d.id=block.id;
    const auto left=slice(file,block.left), right=slice(file,block.right);
    const auto target=options.target==ResolutionTarget::Right ? block.right : block.left;
    const auto side=options.target==ResolutionTarget::Right ? block.left : block.right;
    if(left==right) accept(d,file,{block.left},"same-sides");
    else if(block.base) {
        const auto base=slice(file,*block.base);
        if(left==base) accept(d,file,{block.right},"left-unchanged");
        else if(right==base) accept(d,file,{block.left},"right-unchanged");
        else if(!boundaryComposition(d,file,side,target,*block.base) && !boundaryComposition(d,file,target,side,*block.base)) {
            const auto b=lines(base), s=lines(slice(file,side)), t=lines(slice(file,target));
            const auto se=edits(b,s,side,control), te=edits(b,t,target,control);
            if(auto pieces=compose(b,*block.base,se,te)) accept(d,file,*pieces,"independent-edits");
            if(!d.replacement && options.policy==ResolutionPolicy::Replay && !base.isEmpty() && !independentAddition(se)) {
                if(t.empty()) accept(d,file,{target},"target-deletion-supersedes-edits",true);
                else {
                    const auto bt=tokens(base,control), st=tokens(slice(file,side),control), tt=tokens(slice(file,target),control);
                    const double so=overlap(bt,st,control), to=overlap(bt,tt,control);
                    d.metrics={{"baseTokens",int(bt.size())},{"sideTokens",int(st.size())},{"targetTokens",int(tt.size())},{"sideOverlap",so},{"targetOverlap",to}};
                    bool surviving=false;
                    for(const auto& edit:se) for(int i=edit.start;i<edit.start+edit.count;++i) {
                        const auto key=QString::fromUtf8(b[i]).trimmed();
                        if(key.isEmpty()) continue;
                        for(const auto& line:t) { control.step(); if(QString::fromUtf8(line).trimmed()==key) surviving=true; }
                    }
                    if(!surviving && int(bt.size())>=options.minimumRewriteTokens && so>=options.minimumSideOverlap && to<=options.maximumTargetOverlap)
                        accept(d,file,{target},"target-rewrite-supersedes-edits",true);
                }
            }
        }
    }
    if(!d.replacement) {
        d.reasons={block.base ? (slice(file,target).isEmpty() ? "delete-modify" : "overlapping-replacement") : "missing-base"};
        d.explanation="The edits cannot be accepted by the selected rules. Choose a candidate or edit the result.";
        adaptedCandidate(d,file,block,side,target,control);
    }
    addCandidate(d,"left","Keep LEFT",left,{block.left});
    addCandidate(d,"right","Keep RIGHT",right,{block.right});
    addCandidate(d,"left-right","Keep LEFT then RIGHT",left+right,{block.left,block.right});
    addCandidate(d,"right-left","Keep RIGHT then LEFT",right+left,{block.right,block.left});
    return d;
}
QJsonObject jsonRange(ByteRange r) { return {{"start",r.start},{"length",r.length}}; }
}
QString decisionStateName(DecisionState state) {
    switch(state) {
        case DecisionState::AutomaticExact:return "automatic-exact";
        case DecisionState::AutomaticPolicy:return "automatic-policy";
        case DecisionState::Reviewed:return "reviewed";
        case DecisionState::Deferred:return "deferred";
        default:return "needs-review";
    }
}
QString conflictStatusName(ConflictStatus status) {
    switch(status) {
        case ConflictStatus::Complete:return "complete";
        case ConflictStatus::InvalidInput:return "invalid-input";
        case ConflictStatus::Unsupported:return "unsupported";
        case ConflictStatus::ResourceLimit:return "resource-limit";
        case ConflictStatus::Cancelled:return "cancelled";
        default:return "error";
    }
}
ResolutionPlan planConflictResolution(const QByteArray& bytes,const MarkerOptions& markers,
    const ResolutionOptions& options,const ConflictLimits& limits,const CancellationToken& cancellation,
    const ResolutionProgress& progress) {
    ResolutionPlan plan; plan.options=options;
    ComputationControl control(cancellation,limits.maxWork,limits.maxTraceEntries);
    const auto parsed=detail::parseConflictFileControlled(bytes,markers,limits,control);
    plan.status=parsed.status; plan.message=parsed.message;
    if(parsed.status!=ConflictStatus::Complete) return plan;
    plan.input=parsed.file;
    try {
        if(!std::isfinite(options.minimumSideOverlap) || !std::isfinite(options.maximumTargetOverlap) ||
           options.minimumSideOverlap<0 || options.minimumSideOverlap>1 || options.maximumTargetOverlap<0 || options.maximumTargetOverlap>1 || options.minimumRewriteTokens<1)
            throw std::invalid_argument("Invalid rewrite policy thresholds");
        quint64 candidates=0;
        for(const auto& block:plan.input.conflicts) {
            control.step();
            auto d=resolveBlock(plan.input,block,options,control);
            for(const auto& c:d.candidates) candidates+=c.replacement.size();
            if(candidates>limits.maxCandidateBytes) {
                d.candidates.clear(); d.reasons.append("candidate-search-limited");
            }
            plan.decisions.append(d);
            if(progress) progress(plan.decisions.size(),plan.input.conflicts.size());
        }
    } catch(const ComputationStopped& e) { plan.status=e.reason==StopReason::Cancelled ? ConflictStatus::Cancelled:ConflictStatus::ResourceLimit; plan.message=QString::fromUtf8(e.what()); }
    catch(const std::bad_alloc&) { plan.status=ConflictStatus::ResourceLimit; plan.message="Resolver allocation failed"; }
    catch(const std::exception& e) { plan.status=ConflictStatus::Error; plan.message=QString::fromUtf8(e.what()); }
    if(plan.status!=ConflictStatus::Complete) plan.decisions.clear();
    plan.workPerformed=control.workPerformed(); return plan;
}
MaterializedResolution materializeResolution(const ResolutionPlan& plan,bool reviewDraft,
    const ConflictLimits& limits,const CancellationToken& cancellation) {
    MaterializedResolution output; output.status=plan.status;
    if(plan.status!=ConflictStatus::Complete) { output.message=plan.message; return output; }
    ComputationControl control(cancellation,limits.maxWork,limits.maxTraceEntries);
    try {
        if(plan.decisions.size()!=plan.input.conflicts.size()) throw std::invalid_argument("Incomplete resolution plan");
        if(QCryptographicHash::hash(plan.input.bytes,QCryptographicHash::Sha256)!=plan.input.sha256)
            throw std::invalid_argument("The input changed; recompute the resolution plan");
        bool clean=true; qint64 cursor=0;
        const auto append=[&](const QByteArray& text,std::optional<ByteRange> source,const QString& id) {
            control.step(text.size());
            if(quint64(text.size())>limits.maxOutputBytes-quint64(output.bytes.size())) throw ComputationStopped(StopReason::ResourceLimit);
            output.provenance.append({{output.bytes.size(),text.size()},source,id}); output.bytes+=text;
        };
        for(int i=0;i<plan.decisions.size();++i) {
            const auto& d=plan.decisions[i]; const auto& block=plan.input.conflicts[i];
            if(d.id!=block.id || block.envelope.start<cursor || block.envelope.end()>plan.input.bytes.size()) throw std::invalid_argument("Invalid resolution plan ranges");
            append(plan.input.bytes.mid(cursor,block.envelope.start-cursor),ByteRange{cursor,block.envelope.start-cursor},{});
            const qint64 start=output.bytes.size();
            const bool pending=d.state==DecisionState::NeedsReview || d.state==DecisionState::Deferred;
            if(pending) clean=false;
            else if(!d.replacement) throw std::invalid_argument("An accepted decision has no replacement");
            if(reviewDraft && pending) {
                const auto r=d.replacement ? std::optional<ByteRange>{} : std::optional<ByteRange>{plan.options.target==ResolutionTarget::Right ? block.right:block.left};
                append(d.replacement ? *d.replacement : slice(plan.input,*r),r,d.id); output.deferredIds.append(d.id);
            } else if(!d.replacement) append(slice(plan.input,block.envelope),block.envelope,d.id);
            else if(d.sourceSlices.isEmpty()) append(*d.replacement,std::nullopt,d.id);
            else {
                QByteArray expected;
                for(auto r:d.sourceSlices) {
                    if(r.start<0 || r.length<0 || r.end()>plan.input.bytes.size()) throw std::invalid_argument("Invalid source provenance");
                    expected+=slice(plan.input,r);
                }
                if(expected!=*d.replacement) throw std::invalid_argument("Source provenance differs from replacement");
                for(auto r:d.sourceSlices) append(slice(plan.input,r),r,d.id);
            }
            output.conflictRanges.append({start,output.bytes.size()-start}); cursor=block.envelope.end();
        }
        append(plan.input.bytes.mid(cursor),ByteRange{cursor,plan.input.bytes.size()-cursor},{});
        output.status=ConflictStatus::Complete; output.clean=clean;
    } catch(const ComputationStopped& e) { output.status=e.reason==StopReason::Cancelled ? ConflictStatus::Cancelled:ConflictStatus::ResourceLimit; output.message=QString::fromUtf8(e.what()); }
    catch(const std::bad_alloc&) { output.status=ConflictStatus::ResourceLimit; output.message="Output allocation failed"; }
    catch(const std::exception& e) { output.status=ConflictStatus::Error; output.message=QString::fromUtf8(e.what()); }
    if(output.status!=ConflictStatus::Complete) { output.bytes.clear(); output.provenance.clear(); output.conflictRanges.clear(); output.deferredIds.clear(); }
    return output;
}
bool reviewConflictDecision(ResolutionPlan& plan,const QByteArray& digest,const QString& id,
    const QByteArray& bytes,bool deferred,QString* error) {
    const auto fail=[&](const QString& text) { if(error) *error=text; return false; };
    if(plan.status!=ConflictStatus::Complete || digest!=plan.input.sha256) return fail("The input changed; recompute the resolution plan");
    QStringDecoder decoder(QStringDecoder::Utf8,QStringDecoder::Flag::Stateless);
    const QString text=decoder(bytes);
    if(decoder.hasError() || text.contains(QChar::Null)) return fail("Replacement must be valid UTF-8 text");
    for(auto& d:plan.decisions) if(d.id==id) {
        d.replacement=bytes; d.sourceSlices.clear(); d.state=deferred ? DecisionState::Deferred:DecisionState::Reviewed;
        d.rule=deferred ? "kept-for-review":"user-reviewed"; return true;
    }
    return fail("Unknown conflict decision");
}
QJsonObject resolutionReport(const ResolutionPlan& plan,const MaterializedResolution& output,
    const std::optional<MaterializedResolution>& draft) {
    QJsonObject report{{"schemaVersion",1},{"algorithmVersion","1"},
        {"input",QJsonObject{{"sha256",QString::fromLatin1(plan.input.sha256.toHex())},{"byteLength",plan.input.bytes.size()},{"markerSize",plan.input.options.markerSize}}},
        {"policy",QJsonObject{{"name",plan.options.policy==ResolutionPolicy::Replay ? "replay":"conservative"},{"target",plan.options.target==ResolutionTarget::Right ? "right":"left"},
            {"minimumSideOverlap",plan.options.minimumSideOverlap},{"maximumTargetOverlap",plan.options.maximumTargetOverlap},{"minimumRewriteTokens",plan.options.minimumRewriteTokens},{"tokenizerVersion",1}}}};
    report["status"]=output.status!=ConflictStatus::Complete ? conflictStatusName(output.status)
        : plan.decisions.isEmpty() ? QString("no-markers") : output.clean ? QString("clean"):QString("needs-review");
    report["message"]=output.message;
    report["sourceLabels"]=QJsonObject{{"left",plan.options.sourceLabels.left},{"base",plan.options.sourceLabels.base},{"right",plan.options.sourceLabels.right}};
    report["publication"]=QJsonObject{{"contentPublished",false},{"partial",!output.clean},{"outputSha256",QString::fromLatin1(QCryptographicHash::hash(output.bytes,QCryptographicHash::Sha256).toHex())},{"outputByteLength",output.bytes.size()}};
    QJsonArray decisions; QJsonObject counts{{"total",plan.decisions.size()},{"automaticExact",0},{"automaticPolicy",0},{"reviewed",0},{"needsReview",0},{"deferred",0}};
    for(int i=0;i<plan.decisions.size();++i) {
        const auto& d=plan.decisions[i]; const auto& block=plan.input.conflicts[i];
        const QString key=d.state==DecisionState::AutomaticExact ? "automaticExact":d.state==DecisionState::AutomaticPolicy ? "automaticPolicy":d.state==DecisionState::Reviewed ? "reviewed":d.state==DecisionState::Deferred ? "deferred":"needsReview";
        counts[key]=counts[key].toInt()+1;
        QJsonObject item{{"id",d.id},{"state",decisionStateName(d.state)},{"rule",d.rule},{"explanation",d.explanation},{"reasons",QJsonArray::fromStringList(d.reasons)},
            {"inputByteRange",jsonRange(block.envelope)},{"inputLines",QJsonObject{{"first",block.lines.start+1},{"last",block.lines.end()}}},
            {"labels",QJsonObject{{"left",block.leftLabel},{"base",block.baseLabel},{"right",block.rightLabel}}},{"basePresent",bool(block.base)},{"metrics",d.metrics},
            {"requiresUserDecision",d.state==DecisionState::NeedsReview || d.state==DecisionState::Deferred}};
        if(i<output.conflictRanges.size()) item["outputByteRange"]=jsonRange(output.conflictRanges[i]);
        QJsonArray candidates;
        for(const auto& c:d.candidates) {
            QJsonObject candidate{{"id",c.id},{"title",c.title},{"replacementUtf8",QString::fromUtf8(c.replacement)},{"assumptions",QJsonArray::fromStringList(c.assumptions)}};
            if(c.focusRange) candidate["focusByteRange"]=jsonRange(*c.focusRange);
            candidates.append(candidate);
        }
        if(d.reviewPresentation) {
            const auto& r=*d.reviewPresentation;
            item["reviewPresentation"]=QJsonObject{{"prefixUtf8",QString::fromUtf8(r.prefix)},{"suffixUtf8",QString::fromUtf8(r.suffix)},
                {"sourceIdentifier",r.sourceIdentifier},{"targetIdentifier",r.targetIdentifier},{"candidateIds",QJsonArray::fromStringList(r.candidateIds)}};
        }
        item["candidates"]=candidates;
        if(d.state==DecisionState::AutomaticPolicy && block.base) {
            const auto side=plan.options.target==ResolutionTarget::Right ? block.left:block.right;
            item["supersededEdits"]=QJsonArray{QJsonObject{{"baseRange",jsonRange(*block.base)},{"sideRange",jsonRange(side)},{"baseUtf8",QString::fromUtf8(slice(plan.input,*block.base))},{"sideUtf8",QString::fromUtf8(slice(plan.input,side))}}};
        }
        decisions.append(item);
    }
    report["summary"]=counts; report["conflicts"]=decisions;
    QJsonArray provenance;
    for(const auto& piece:output.provenance) { QJsonObject p{{"output",jsonRange(piece.output)},{"conflictId",piece.conflictId}}; if(piece.input) p["input"]=jsonRange(*piece.input); provenance.append(p); }
    report["provenance"]=provenance;
    if(draft) {
        QJsonArray items;
        for(int i=0;i<plan.decisions.size() && i<draft->conflictRanges.size();++i) if(draft->deferredIds.contains(plan.decisions[i].id))
            items.append(QJsonObject{{"id",plan.decisions[i].id},{"range",jsonRange(draft->conflictRanges[i])},{"state","deferred"},{"message","Keep for review; add a TODO manually in your editor if useful."}});
        report["reviewDraft"]=QJsonObject{{"published",false},{"status",draft->clean ? "clean":"draft-needs-review"},{"sha256",QString::fromLatin1(QCryptographicHash::hash(draft->bytes,QCryptographicHash::Sha256).toHex())},{"byteLength",draft->bytes.size()},{"decisions",items}};
    }
    return report;
}
} // namespace diffcore
