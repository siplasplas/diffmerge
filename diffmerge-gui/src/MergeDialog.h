#pragma once
#include "LaunchOptions.h"
class QWidget;
namespace diffmerge::gui::desktop {
// 0 only for a verified, explicitly Resolved save; otherwise 1.
int runMergeDialog(const MergeLaunchOptions& options, QWidget* parent = nullptr);
}
