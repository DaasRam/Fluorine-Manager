#ifndef MODDIRECTORYDISCOVERY_H
#define MODDIRECTORYDISCOVERY_H

#include <QDir>
#include <QString>
#include <QStringList>

namespace ModDirectoryDiscovery
{

// Mod names are user-controlled and may begin with a dot on Unix. Keep
// QDir's dot-entry exclusion, but include hidden directories in the scan.
inline QStringList directModDirectories(const QString& modsDirectory)
{
  QDir mods(QDir::fromNativeSeparators(modsDirectory));
  return mods.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                        QDir::Name);
}

}  // namespace ModDirectoryDiscovery

#endif  // MODDIRECTORYDISCOVERY_H
