#include "credentialstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QSettings>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace CredentialStore
{
namespace
{
bool secureFile(const QString& path, bool create)
{
  const int descriptor = ::open(QFile::encodeName(path).constData(),
      (create ? O_RDWR | O_CREAT : O_RDONLY) | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
  if (descriptor < 0) return false;
  struct stat info{};
  const bool secured = ::fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) &&
                       info.st_uid == ::geteuid() && ::fchmod(descriptor, 0600) == 0;
  ::close(descriptor);
  return secured;
}
}

QString read(const QString& path, const QString& key)
{
  QLockFile lock(path + ".fluorine-lock");
  if (!lock.tryLock(1000) || !secureFile(path, false)) return {};
  QSettings settings(path, QSettings::IniFormat);
  settings.sync();
  if (settings.status() != QSettings::NoError) return {};
  return settings.value("ModOrganizer2_" + key).toString();
}

bool write(const QString& path, const QString& key, const QString& value)
{
  if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
  QLockFile lock(path + ".fluorine-lock");
  if (!lock.tryLock(1000) || !secureFile(path, true)) return false;
  QSettings settings(path, QSettings::IniFormat);
  settings.setAtomicSyncRequired(true);
  settings.sync();
  if (settings.status() != QSettings::NoError) return false;
  const QString name = "ModOrganizer2_" + key;
  if (value.isEmpty()) settings.remove(name);
  else settings.setValue(name, value);
  settings.sync();
  return settings.status() == QSettings::NoError && secureFile(path, false);
}
}  // namespace CredentialStore
