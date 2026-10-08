#pragma once
#include <diffmerge/Comparison.h>
#include <QByteArray>
#include <QFileDevice>
#include <QTimer>
#include <array>
namespace qce::encoding { class EncodingGuard; }
namespace diffmerge::gui {
struct FileEditingState {
    std::array<bool, 2> editable{}, modified{}, raw{}, bom{}, unsafe{}, encodingUnsafe{};
    // File encoding per side (cpg name: utf8, cp1250, ...); written back on save.
    std::array<QString, 2> encoding{QStringLiteral("utf8"), QStringLiteral("utf8")};
    // Keep typed/pasted text storable in the side's encoding; they belong to
    // the editors and swap with them.
    std::array<qce::encoding::EncodingGuard*, 2> guards{};
    // Gives guards[i] the side's encoding and BOM (line breaks stay in the text).
    void syncGuard(int i);
    std::array<QString, 2> cleanText, targets, canonicalTargets;
    std::array<QByteArray, 2> diskHash;
    std::array<bool, 2> diskExists{};
    std::array<TextSnapshot, 2> cleanSnapshots;
    QTimer* timer = nullptr;
    quint64 generation = 0;
    diffcore::CancellationToken cancellation;
    bool installing = false, pending = false;
    ~FileEditingState() { cancellation.requestCancellation(); }
};
}
