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
                     const SliderCase& sc) {
    // blockBegin is 1-based; convert to 0-based for comparison with LineRange.
    const int target = sc.blockBegin - 1;
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
    return bestPos;
}

// ---- Statistics formatting ---------------------------------------------

QString formatStats(const EvalStats& s) {
    if (s.total == 0) return QStringLiteral("No sliders evaluated.\n");

    auto pct = [&](int n) {
        return QStringLiteral("%1 / %2  (%3%)")
            .arg(n).arg(s.total)
            .arg(100.0 * n / s.total, 0, 'f', 1);
    };

    QString out;
    QTextStream ts(&out);

    ts << QStringLiteral("Total sliders evaluated : %1\n").arg(s.total);
    ts << QStringLiteral("Not found in diff output: %1\n\n").arg(s.notFound);

    ts << QStringLiteral("                 Wrong          Right\n");
    ts << QStringLiteral("Git diff :  ") << pct(s.gnuWrong)
       << QStringLiteral("   ") << pct(s.total - s.gnuWrong) << '\n';
    ts << QStringLiteral("Diffmerge:  ") << pct(s.dmWrong)
       << QStringLiteral("   ") << pct(s.total - s.dmWrong) << '\n';

    ts << QStringLiteral("\nError histogram  (diffmerge_pos - human_pos):\n");
    ts << QStringLiteral("  shift   count\n");
    const int minShift = s.errorHist.isEmpty() ? 0 : s.errorHist.firstKey();
    const int maxShift = s.errorHist.isEmpty() ? 0 : s.errorHist.lastKey();
    for (int k = minShift; k <= maxShift; ++k) {
        const int cnt = s.errorHist.value(k, 0);
        if (cnt == 0) continue;
        const QString bar(std::min(cnt * 40 / s.total + 1, 40), QLatin1Char('#'));
        ts << QStringLiteral("  %1  %2  %3\n")
              .arg(k, 4).arg(cnt, 6).arg(bar);
    }
    return out;
}

}  // namespace diffmerge::slidereval
