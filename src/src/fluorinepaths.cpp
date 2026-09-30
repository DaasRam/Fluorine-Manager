#include "fluorinepaths.h"
#include "fluorineconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSaveFile>
#include <uibase/utility.h>
#include <vector>

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

QString fluorineDataDir()
{
  return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
      .filePath("fluorine");
}

QString fluorineCredentialsPath()
{
  const QString path =
      QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
          .filePath("ModOrganizer/credentials.ini");
  const QString legacy = QDir::homePath() + "/.config/ModOrganizer/credentials.ini";
  // Preserve existing logins when upgrading with a custom XDG_CONFIG_HOME.
  // A file already at the requested location always takes precedence.
  const QFileInfo destinationInfo(path);
  if (path != legacy && !destinationInfo.exists() && !destinationInfo.isSymLink()) {
    // Open without following a symlink or waiting on a special file. Restrict
    // the retained legacy copy too: securing only the new file would leave the
    // old plaintext credentials readable after an XDG directory change.
    const int fd = ::open(QFile::encodeName(legacy).constData(),
                          O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd >= 0) {
      struct stat metadata {};
      QFile source;
      if (::fstat(fd, &metadata) == 0 && S_ISREG(metadata.st_mode) &&
          metadata.st_size <= 1024 * 1024 && ::fchmod(fd, 0600) == 0 &&
          source.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        if (QDir().mkpath(QFileInfo(path).absolutePath())) {
          QSaveFile destination(path);
          const QByteArray contents = source.read(1024 * 1024 + 1);
          if (source.error() == QFileDevice::NoError && source.atEnd() &&
              contents.size() <= 1024 * 1024 &&
              destination.open(QIODevice::WriteOnly) &&
              destination.setPermissions(QFileDevice::ReadOwner |
                                         QFileDevice::WriteOwner) &&
              destination.write(contents) == contents.size()) {
            destination.commit();
          } else {
            destination.cancelWriting();
          }
        }
      } else {
        ::close(fd);
      }
    }
  }
  return path;
}

QString fluorineVfsCacheDir()
{
  return fluorineDataDir() + "/vfs_cache";
}

void fluorineMigrateDataDir()
{
  const QString oldRoot = QDir::homePath() + "/.var/app/com.fluorine.manager";
  const QString newRoot = fluorineDataDir();
  if (!QDir(oldRoot).exists() || QDir::cleanPath(oldRoot) == QDir::cleanPath(newRoot)) {
    return;
  }

  QString configRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
  if (configRoot.isEmpty()) configRoot = QDir::homePath() + "/.config";
  const QString newConfig = QDir(configRoot).filePath("fluorine/config.json");
  const QString relativeConfig = "/config/fluorine/config.json";
  const QString oldConfig = QFile::exists(oldRoot + relativeConfig)
                                ? oldRoot + relativeConfig : newRoot + relativeConfig;
  if (!QFileInfo::exists(newConfig) && QFileInfo::exists(oldConfig) &&
      !MOBase::shellCopy(oldConfig, newConfig)) {
    fprintf(stderr, "[fluorine] Migration deferred: cannot copy config.json\n");
    return;
  }

  auto config = FluorineConfig::load();
  // Repair the old migration's premature path rewrite, including installations
  // where it also wrote MOVED.txt after a failed move.
  if (config && config->prefix_path.startsWith(newRoot + '/') &&
      !config->prefixExists()) {
    const QString original = oldRoot + config->prefix_path.mid(newRoot.size());
    if (QDir(original).exists()) {
      config->prefix_path = original;
      if (!config->save()) return;
    }
  }

  const QStringList subdirs = {"logs", "bin", "config", "Prefix"};
  bool hasData = false;
  for (const auto& sub : subdirs) hasData |= QFileInfo::exists(oldRoot + '/' + sub);
  if (!hasData) return;
  if (!QDir().mkpath(newRoot)) {
    fprintf(stderr, "[fluorine] Migration deferred: cannot create data directory\n");
    return;
  }

  struct Transfer { QString source; QString destination; bool copied; };
  std::vector<Transfer> transferred;
  bool complete = true;
  for (const auto& sub : subdirs) {
    const QString source = oldRoot + '/' + sub;
    const QString destination = newRoot + '/' + sub;
    const QFileInfo info(source);
    if (!info.exists()) continue;
    if (!info.isDir() || info.isSymLink() || QFileInfo::exists(destination) ||
        QFileInfo(destination).isSymLink()) {
      fprintf(stderr, "[fluorine] Migration deferred for %s: destination conflict or unsupported source\n",
              qUtf8Printable(sub));
      complete = false;
      continue;
    }
    if (QDir().rename(source, destination)) {
      transferred.push_back({source, destination, false});
    } else if (MOBase::copyDir(source, destination, false)) {
      // Across filesystems, retain the original until the new config is committed.
      transferred.push_back({source, destination, true});
    } else {
      fprintf(stderr, "[fluorine] Migration deferred: failed to transfer %s\n",
              qUtf8Printable(sub));
      complete = false;
    }
  }

  bool configSaved = true;
  if (config && config->prefix_path.startsWith(oldRoot + '/')) {
    const QString replacement = newRoot + config->prefix_path.mid(oldRoot.size());
    bool transferredPrefix = false;
    for (const auto& transfer : transferred) {
      transferredPrefix |= config->prefix_path == transfer.source ||
                           config->prefix_path.startsWith(transfer.source + '/');
    }
    if ((transferredPrefix || !QDir(config->prefix_path).exists()) &&
        QDir(replacement).exists()) {
      config->prefix_path = replacement;
      configSaved = config->save();
    } else {
      complete = false;
    }
  }
  if (!configSaved) {
    // Keep the on-disk config usable when publishing the new path fails.
    for (auto it = transferred.rbegin(); it != transferred.rend(); ++it) {
      const bool restored = it->copied
          ? QDir(it->destination).removeRecursively()
          : QDir().rename(it->destination, it->source);
      if (!restored) {
        fprintf(stderr, "[fluorine] Could not roll back migration; data retained at %s\n",
                qUtf8Printable(it->destination));
      }
    }
    return;
  }
  for (const auto& transfer : transferred) {
    if (transfer.copied && !QDir(transfer.source).removeRecursively()) complete = false;
  }
  if (!complete) {
    // An old marker is not evidence of success: the old implementation wrote it
    // even when every rename failed. The next startup must be able to retry.
    QFile::remove(oldRoot + "/MOVED.txt");
    return;
  }

  QSaveFile marker(oldRoot + "/MOVED.txt");
  const QByteArray contents = ("Data migrated to " + newRoot + '\n').toUtf8();
  if (!marker.open(QIODevice::WriteOnly) || marker.write(contents) != contents.size() ||
      !marker.commit()) {
    fprintf(stderr, "[fluorine] Data migrated, but could not write completion marker\n");
    return;
  }
  fprintf(stderr, "[fluorine] Migration complete.\n");
}
