#pragma once
#include <QString>
class QWidget;
namespace diffmerge::gui::desktop {
int runResolveDialog(const QString& path,int markerSize=7,QWidget* parent=nullptr);
}
