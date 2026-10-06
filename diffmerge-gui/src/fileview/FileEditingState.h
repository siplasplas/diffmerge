#pragma once
#include <diffmerge/Comparison.h>
#include <QByteArray>
#include <QFileDevice>
#include <QTimer>
#include <array>
namespace diffmerge::gui {
struct FileEditingState {
    std::array<bool, 2> editable{}, modified{}, raw{}, bom{}, unsafe{}, encodingUnsafe{};
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
