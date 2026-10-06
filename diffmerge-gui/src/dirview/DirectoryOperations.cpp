#include <diffmerge/DirectoryOperations.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <stdexcept>
#include <algorithm>
namespace diffmerge::gui {
namespace {
QString inside(const QString& root, const QString& relative) {
    if(relative.isEmpty() || QDir::isAbsolutePath(relative) || relative=="." || relative==".." || relative.startsWith("../") || relative.contains("/../"))
        throw std::runtime_error("Invalid selected relative path");
    const QString base=QFileInfo(root).canonicalFilePath();
    if(base.isEmpty() || !QFileInfo(base).isDir()) throw std::runtime_error("Directory root is unavailable");
    const QString path=QDir::cleanPath(QDir(base).filePath(relative));
    if(!path.startsWith(base.endsWith('/') ? base : base+'/')) throw std::runtime_error("Selected path escapes its root");
    QString current=base;
    const auto components=QDir(base).relativeFilePath(path).split('/');
    for(const auto& component:components) {
        current=QDir(current).filePath(component);
        if(QFileInfo(current).isSymLink()) throw std::runtime_error("Symbolic links are not supported by directory operations");
    }
    return path;
}
void add(DirectoryCopyPlan& plan,const QString& relative) {
    if(plan.items.size()>=100000) throw std::runtime_error("Copy entry limit exceeded");
    const auto source=inside(plan.sourceRoot,relative), destination=inside(plan.destinationRoot,relative);
    const QFileInfo input(source), output(destination);
    if(!input.exists() || !input.isReadable() || (!input.isDir() && !input.isFile())) throw std::runtime_error("Source is unavailable or unsupported");
    if(output.exists() && input.isDir()!=output.isDir()) throw std::runtime_error("File/directory collision: remove the destination explicitly first");
    plan.items.append({source,destination,input.isDir(),output.exists() && !input.isDir(),input.size(),input.lastModified(),output.size(),output.lastModified()});
    if(input.isDir()) for(const auto& entry:QDir(source).entryInfoList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot,QDir::Name))
        add(plan,relative+'/'+entry.fileName());
}
}
DirectoryCopyPlan planDirectoryCopy(const QString& sourceRoot,const QString& destinationRoot,const QStringList& relativePaths) {
    DirectoryCopyPlan plan; plan.sourceRoot=QFileInfo(sourceRoot).canonicalFilePath(); plan.destinationRoot=QFileInfo(destinationRoot).canonicalFilePath();
    try {
        if(plan.sourceRoot.isEmpty() || plan.destinationRoot.isEmpty() || plan.sourceRoot==plan.destinationRoot ||
           plan.sourceRoot.startsWith(plan.destinationRoot+'/') || plan.destinationRoot.startsWith(plan.sourceRoot+'/'))
            throw std::runtime_error("Copy roots must be existing, disjoint directories");
        auto paths=relativePaths; paths.removeDuplicates(); std::sort(paths.begin(),paths.end());
        QStringList selected;
        for(const auto& path:paths) {
            bool covered=false; for(const auto& parent:selected) covered |= path.startsWith(parent+'/');
            if(!covered) { add(plan,path); selected.append(path); }
        }
    } catch(const std::exception& error) { plan.items.clear(); plan.error=QString::fromUtf8(error.what()); }
    return plan;
}
QString executeDirectoryCopy(const DirectoryCopyPlan& plan) {
    if(!plan.error.isEmpty()) return plan.error;
    try {
        for(const auto& item:plan.items) {
            const auto sourceRelative=QDir(plan.sourceRoot).relativeFilePath(item.source);
            const auto targetRelative=QDir(plan.destinationRoot).relativeFilePath(item.destination);
            if(inside(plan.sourceRoot,sourceRelative)!=item.source || inside(plan.destinationRoot,targetRelative)!=item.destination)
                throw std::runtime_error("Copy plan paths changed");
            const QFileInfo source(item.source), target(item.destination);
            if(!source.exists() || (!item.directory && !source.isFile()) || source.isDir()!=item.directory || source.lastModified()!=item.modified || (!item.directory && source.size()!=item.size))
                throw std::runtime_error("Source changed since copy confirmation");
            if(target.exists() && target.isDir()!=item.directory) throw std::runtime_error("Destination type changed since confirmation");
            if(!item.directory && item.overwrites && (!target.exists() || target.size()!=item.destinationSize || target.lastModified()!=item.destinationModified))
                throw std::runtime_error("Destination changed since copy confirmation");
            if(!item.directory && target.exists() && !item.overwrites) throw std::runtime_error("A destination appeared after confirmation");
            if(item.directory) {
                if(!QDir().mkpath(item.destination)) throw std::runtime_error("Cannot create destination directory");
                continue;
            }
            if(!QDir().mkpath(QFileInfo(item.destination).absolutePath())) throw std::runtime_error("Cannot create destination parents");
            QFile input(item.source); QSaveFile output(item.destination);
            if(!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly)) throw std::runtime_error("Cannot open copy source/destination");
            while(!input.atEnd()) {
                const auto bytes=input.read(64*1024);
                if(input.error()!=QFileDevice::NoError || output.write(bytes)!=bytes.size()) throw std::runtime_error("Copy read/write failed");
            }
            const QFileInfo after(item.source);
            if(after.size()!=item.size || after.lastModified()!=item.modified) throw std::runtime_error("Source changed during copy");
            if(!output.setPermissions(source.permissions()) || !output.commit()) throw std::runtime_error("Cannot commit copied file");
        }
    } catch(const std::exception& error) { return QString::fromUtf8(error.what()); }
    return {};
}
QString trashDirectoryEntries(const QString& root,const QStringList& relativePaths) {
    try {
        auto paths=relativePaths; paths.removeDuplicates(); std::sort(paths.begin(),paths.end());
        QStringList selected;
        for(const auto& path:paths) {
            bool covered=false; for(const auto& parent:selected) covered |= path.startsWith(parent+'/');
            if(!covered) { inside(root,path); selected.append(path); }
        }
        for(const auto& relative:selected) {
            const auto path=inside(root,relative);
            if(!QFileInfo(path).exists()) throw std::runtime_error("Selected entry disappeared");
            if(!QFile::moveToTrash(path)) throw std::runtime_error("Cannot move entry to trash; nothing is permanently deleted");
        }
    } catch(const std::exception& error) { return QString::fromUtf8(error.what()); }
    return {};
}
}
