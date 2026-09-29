#include "downloadmetadatapolicy.h"

#include <QFile>
#include <QFileInfo>
#include <QMetaType>
#include <QRegularExpression>

namespace DownloadMetadataPolicy {
namespace {
const QString UrlKey = QStringLiteral("url");
const QString UrlsKey = QStringLiteral("urls");
const QString UserDataKey = QStringLiteral("userData");
const QString DownloadMapKey = QStringLiteral("downloadMap");

QStringList readLegacyUrls(const QString& joined)
{
  // Historical alternatives were separated with ';'. Treat it as a separator
  // only when another URL scheme follows, preserving semicolons in query
  // parameters such as response-content-disposition=...;filename=....
  static const QRegularExpression separator(
      QStringLiteral(R"(;(?=[A-Za-z][A-Za-z0-9+.-]*://))"));
  return joined.split(separator);
}
} // namespace

CapabilityRetention retentionForPhase(DownloadPhase phase) {
  switch (phase) {
  case DownloadPhase::Started:
  case DownloadPhase::Downloading:
  case DownloadPhase::Canceling:
  case DownloadPhase::Pausing:
  case DownloadPhase::Paused:
  case DownloadPhase::Error:
    return CapabilityRetention::Resumable;
  default:
    return CapabilityRetention::Retire;
  }
}

QString unambiguousFinalBaseName(QString baseName) {
  const QString partialSuffix = QStringLiteral(".unfinished");
  while (baseName.endsWith(partialSuffix)) {
    baseName.chop(partialSuffix.size());
  }
  if (baseName.isEmpty()) {
    baseName = QStringLiteral("download");
  }
  return baseName;
}

bool isSafeMetadataLeaf(const QString &path) {
  const QFileInfo leaf(path);
  return !leaf.isSymLink() && (!leaf.exists() || leaf.isFile());
}

CapabilityRetention retentionAfterPublication(const QString &path,
                                              const QString &partialPath) {
  const QFileInfo archive(path);
  return path != partialPath && archive.isFile() && !archive.isSymLink()
             ? CapabilityRetention::Retire
             : CapabilityRetention::Resumable;
}

bool retireFile(const QString &path) {
  const QFileInfo leaf(path);
  if (!leaf.exists() && !leaf.isSymLink()) {
    return true;
  }
  if (QFile::remove(path)) {
    return true;
  }
  if (leaf.isSymLink() || !leaf.isFile()) {
    return false;
  }

  QSettings settings(path, QSettings::IniFormat);
  const auto retired = loadAndConverge(settings, CapabilityRetention::Retire);
  return retired.status == QSettings::NoError;
}

LoadedCapabilities loadAndConverge(QSettings &settings,
                                   CapabilityRetention retention) {
  LoadedCapabilities result;
  if (!isSafeMetadataLeaf(settings.fileName())) {
    result.status = QSettings::AccessError;
    return result;
  }

  const QVariant rawUserData = settings.value(UserDataKey);
  result.userData = rawUserData.toMap();

  if (retention == CapabilityRetention::Resumable) {
    if (settings.contains(UrlsKey)) {
      // The dedicated key is the unambiguous QStringList representation. A
      // one-item list may be read back by QSettings as a QString, but
      // toStringList() still preserves it as one URL.
      result.urls = settings.value(UrlsKey).toStringList();
    } else {
      // Older Fluorine builds joined URL alternatives with semicolons. Keep
      // reading that format, but store future metadata as a real QStringList
      // so semicolons inside signed URLs do not become fake server entries.
      result.urls = readLegacyUrls(settings.value(UrlKey, QString()).toString());
    }
    result.status = settings.status();
    return result;
  }

  const bool hasUrl = settings.contains(UrlKey) || settings.contains(UrlsKey);

  // Do not reinterpret or overwrite malformed/foreign userData values. The
  // production representation is a QVariantMap; only that known shape grants
  // authority to remove the duplicated capability list.
  const bool hasDownloadMap =
      rawUserData.metaType().id() == QMetaType::QVariantMap &&
      result.userData.remove(DownloadMapKey) > 0;
  result.changed = hasUrl || hasDownloadMap;
  if (!result.changed) {
    result.status = settings.status();
    return result;
  }

  if (settings.status() != QSettings::NoError) {
    result.status = settings.status();
    return result;
  }
  if (hasUrl) {
    settings.remove(UrlKey);
    settings.remove(UrlsKey);
  }
  if (hasDownloadMap) {
    settings.setValue(UserDataKey, result.userData);
  }
  settings.sync();
  result.status = settings.status();
  return result;
}

void write(QSettings &settings, CapabilityRetention retention,
           const QStringList &urls, const QVariantMap &userData) {
  if (retention == CapabilityRetention::Resumable) {
    settings.remove(UrlKey);
    settings.setValue(UrlsKey, urls);
    settings.setValue(UserDataKey, userData);
    return;
  }

  settings.remove(UrlKey);
  settings.remove(UrlsKey);
  QVariantMap retainedUserData = userData;
  retainedUserData.remove(DownloadMapKey);
  settings.setValue(UserDataKey, retainedUserData);
}
} // namespace DownloadMetadataPolicy
