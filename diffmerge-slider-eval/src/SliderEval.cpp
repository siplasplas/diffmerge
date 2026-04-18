#include "SliderEval.h"

#include <QFile>
#include <QTextStream>
#include <climits>
#include <cmath>

namespace diffmerge::slidereval {

// ---- CSV parsing -------------------------------------------------------

CsvEntry parseCsvLine(const QString& line) {
    const QStringList fields = line.trimmed().split(QLatin1Char(','));
    if (fields.size() < 2) return {};

    CsvEntry entry;
    entry.digest = fields[0].trimmed();
    if (entry.digest.isEmpty()) return {};

    for (int i = 1; i < fields.size(); ++i) {
        const QStringList parts = fields[i].trimmed().split(QLatin1Char(' '),
                                                             Qt::SkipEmptyParts);
        if (parts.size() < 3) continue;
        SliderCase sc;
        sc.sign       = parts[0].isEmpty() ? '+' : parts[0][0].toLatin1();
        sc.blockBegin = parts[1].toInt();
        sc.delta      = parts[2].toInt();
        if (parts.size() >= 4) sc.diffPos = parts[3].toInt();
        if (sc.blockBegin <= 0) continue;
        entry.sliders.append(sc);
    }
    return entry;
}

QVector<CsvEntry> loadCsvFile(const QString& csvPath) {
    QFile f(csvPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream in(&f);
    QVector<CsvEntry> result;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty()) continue;
        CsvEntry e = parseCsvLine(line);
        if (!e.digest.isEmpty() && !e.sliders.isEmpty())
            result.append(e);
    }
    return result;
}

// ---- Slider position finding -------------------------------------------

int findDiffmergePos(const diffcore::DiffResult& result,
                     const SliderCase& sc,
                     int maxSlide) {
    // Use diffPos (system diff anchor) to identify the exact hunk.
    // If diffPos is not annotated we cannot reliably locate the hunk — skip it.
    if (sc.diffPos <= 0) return -1;
    const int target = sc.diffPos - 1;  // convert to 0-based
    int bestPos  = -1;
    int bestDist = INT_MAX;

    for (const diffcore::Hunk& h : result.hunks) {
        if (sc.sign == '+' && h.type != diffcore::ChangeType::Insert)  continue;
        if (sc.sign == '-' && h.type != diffcore::ChangeType::Delete)  continue;

        const int hunkStart = (sc.sign == '+')
                              ? h.rightRange.start
                              : h.leftRange.start;
        const int dist = std::abs(hunkStart - target);
        if (dist < bestDist) {
            bestDist = dist;
            bestPos  = hunkStart + 1;  // back to 1-based
        }
    }
    // When diffPos is available, use a tight tolerance — system diff and O(NP)
    // should agree within a few lines at most. A large distance means we've
    // matched an unrelated hunk elsewhere in the file.
    const int effectiveMax = (sc.diffPos > 0) ? 3 : maxSlide;
    if (bestDist > effectiveMax) return -1;
    return bestPos;
}

// ---- Statistics formatting ---------------------------------------------

QString formatStats(const EvalStats& s) {
    if (s.total == 0) return QStringLiteral("No sliders evaluated.\n");

    const int found = s.total - s.notFound;

    auto pct = [&](int n, int base) -> QString {
        if (base == 0) return QStringLiteral("-");
        return QStringLiteral("%1 / %2  (%3%)")
            .arg(n).arg(base)
            .arg(100.0 * n / base, 0, 'f', 1);
    };

    QString out;
    QTextStream ts(&out);

    ts << QStringLiteral("Total sliders      : %1\n").arg(s.total);
    ts << QStringLiteral("Hunk found (<=maxSlide): %1  not found: %2\n\n")
          .arg(found).arg(s.notFound);

    ts << QStringLiteral("                 Wrong (≠ human)       Right (= human)\n");
    ts << QStringLiteral("Git diff :  ") << pct(s.gnuWrong,        s.total)
       << QStringLiteral("   ") << pct(s.total - s.gnuWrong, s.total) << '\n';
    if (s.sysTotal > 0) {
        ts << QStringLiteral("Sys diff :  ") << pct(s.sysWrong,    s.sysTotal)
           << QStringLiteral("   ") << pct(s.sysTotal - s.sysWrong, s.sysTotal)
           << QStringLiteral("  (on %1 annotated)\n").arg(s.sysTotal);
    }
    ts << QStringLiteral("Diffmerge:  ") << pct(s.dmWrong,         found)
       << QStringLiteral("   ") << pct(found - s.dmWrong,    found)   << '\n';

    ts << QStringLiteral("\nDiffmerge vs Git diff (on %1 found sliders):\n").arg(found);
    ts << QStringLiteral("  Better (closer to human) : ") << pct(s.dmBetter, found) << '\n';
    ts << QStringLiteral("  Worse  (further from human): ") << pct(s.dmWorse,  found) << '\n';
    ts << QStringLiteral("  Tie    (same distance)    : ") << pct(s.dmTie,    found) << '\n';

    if (s.sysTotal > 0) {
        const int sysCmp = s.dmBetterThanSys + s.dmWorseThanSys + s.dmTieWithSys;
        ts << QStringLiteral("\nDiffmerge vs Sys diff (on %1 sliders with diffPos):\n").arg(sysCmp);
        ts << QStringLiteral("  Better (closer to human) : ") << pct(s.dmBetterThanSys, sysCmp) << '\n';
        ts << QStringLiteral("  Worse  (further from human): ") << pct(s.dmWorseThanSys, sysCmp) << '\n';
        ts << QStringLiteral("  Tie    (same distance)    : ") << pct(s.dmTieWithSys,   sysCmp) << '\n';
    }

    ts << QStringLiteral("\nError histogram  (diffmerge_pos - human_pos):\n");
    ts << QStringLiteral("  shift   count\n");
    // Print all non-zero entries sorted by shift value.
    for (auto it = s.errorHist.cbegin(); it != s.errorHist.cend(); ++it) {
        if (it.value() == 0) continue;
        const QString bar(std::min(it.value() * 40 / s.total + 1, 40), QLatin1Char('#'));
        ts << QStringLiteral("  %1  %2  %3\n")
              .arg(it.key(), 4).arg(it.value(), 6).arg(bar);
    }

    // Per-heuristic stats (only printed when at least one heuristic fired)
    static const char* kHNames[] = {
        nullptr, "H1 (empty/bare)   ", "H2 (indent drop)  ",
        "H3 (separator/1) ", "H3b(doc comment) ", "H4 (mark)         ",
        "H5 (},{)          ", "H6 (// series)    ", "H7 (empty outer)  "
    };
    bool anyFired = false;
    for (int i = 1; i < diffcore::kHeuristicCount; ++i)
        if (s.hStats[i].fired > 0) { anyFired = true; break; }

    if (anyFired) {
        ts << QStringLiteral("\nHeuristic stats (vs no-heuristic base):\n");
        ts << QStringLiteral("  heuristic           fired   better    worse     tie\n");
        for (int i = 1; i < diffcore::kHeuristicCount; ++i) {
            const auto& h = s.hStats[i];
            if (h.fired == 0) continue;
            ts << QStringLiteral("  %1  %2   %3 (%4%)   %5 (%6%)   %7 (%8%)\n")
                  .arg(QLatin1String(kHNames[i]))
                  .arg(h.fired, 6)
                  .arg(h.better, 6)
                  .arg(h.fired ? 100.0 * h.better / h.fired : 0.0, 0, 'f', 1)
                  .arg(h.worse, 6)
                  .arg(h.fired ? 100.0 * h.worse  / h.fired : 0.0, 0, 'f', 1)
                  .arg(h.tie, 6)
                  .arg(h.fired ? 100.0 * h.tie    / h.fired : 0.0, 0, 'f', 1);
        }
    }
    return out;
}

}  // namespace diffmerge::slidereval
