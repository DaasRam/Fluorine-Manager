#include "lootmanager.h"
#include "fluorinepaths.h"

#ifdef FLUORINE_LOOTMANAGER_TESTING
#include "lootmanager_test.h"
#endif

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>

#include <atomic>

#include <uibase/log.h>

#include <algorithm>

namespace
{

// LOOT 0.29.2 updated to Qt 6.11.1 and stopped bundling icuuc.dll. The
// upstream changelog says it is supplied by Wine 11.5+, but older Proton
// builds do not consistently provide it. 0.29.1 includes the DLL and is the
// last selected LOOT release whose Windows archive bundles icuuc.dll. Keep
// the immutable release asset and its GitHub-published digest together so
// "latest" cannot silently regress managed installs.
constexpr auto LootVersion = "0.29.1";
constexpr auto LootAssetName = "loot_0.29.1-win64.7z";
constexpr auto LootAssetUrl =
    "https://github.com/loot/loot/releases/download/0.29.1/loot_0.29.1-win64.7z";
constexpr auto LootAssetSha256 =
    "699dbb1157e26cbd8b8758632b8370bbb372759c9a00ffd9a4300a05f3409837";
constexpr auto LootVersionMarker = ".fluorine-loot-version";

using HttpGet = std::function<QByteArray(
    const QString&, const std::atomic_bool*, const std::function<void(float)>&,
    const QString&)>;
using ExtractArchive =
    std::function<bool(const QString&, const QString&)>;

struct LootRelease
{
  QString version;
  QString assetName;
  QString assetUrl;
  QByteArray sha256Hex;
};

QByteArray httpGet(const QString& url, const std::atomic_bool* cancelFlag,
                   const std::function<void(float)>& progressCb = nullptr,
                   const QString& destFile = {})
{
  QFile outFile;
  if (!destFile.isEmpty()) {
    outFile.setFileName(destFile);
    if (!outFile.open(QIODevice::WriteOnly))
      return {};
  }

  QNetworkAccessManager mgr;
  QNetworkRequest req{QUrl(url)};
  req.setRawHeader("User-Agent", "Fluorine-Manager/loot");
  req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                   QNetworkRequest::NoLessSafeRedirectPolicy);
  req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

  QNetworkReply* reply = mgr.get(req);
  QEventLoop loop;
  qint64 total = -1;
  qint64 received = 0;
  bool writeFailed = false;
  QByteArray inMemory;

  QObject::connect(reply, &QNetworkReply::readyRead, [&]() {
    if (total < 0)
      total = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
    const QByteArray chunk = reply->readAll();
    received += chunk.size();
    if (outFile.isOpen()) {
      if (outFile.write(chunk) != chunk.size()) {
        writeFailed = true;
        reply->abort();
        loop.quit();
        return;
      }
    } else {
      inMemory.append(chunk);
    }
    if (progressCb && total > 0)
      progressCb(static_cast<float>(received) / static_cast<float>(total));
  });
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

  QTimer cancelTimer;
  if (cancelFlag) {
    QObject::connect(&cancelTimer, &QTimer::timeout, [&]() {
      if (cancelFlag->load(std::memory_order_relaxed)) {
        reply->abort();
        loop.quit();
      }
    });
    cancelTimer.start(200);
  }

  loop.exec();
  cancelTimer.stop();
  const bool flushFailed = outFile.isOpen() && !outFile.flush();
  if (outFile.isOpen())
    outFile.close();

  if (writeFailed || flushFailed || reply->error() != QNetworkReply::NoError) {
    MOBase::log::warn("LOOT download request failed: {} ({})", url,
                      reply->errorString());
    reply->deleteLater();
    if (!destFile.isEmpty())
      QFile::remove(destFile);
    return {};
  }
  reply->deleteLater();
  return inMemory;
}

// Try the bundle's 7zz first, then system 7z/7za/7zz.
bool extract7z(const QString& archivePath, const QString& destDir,
               const QString& dataDir)
{
  QStringList candidates;
  const QString bundled = QDir(dataDir).filePath("bin/7zz");
  if (QFileInfo::exists(bundled))
    candidates << bundled;
  for (const QString& n : {QStringLiteral("7z"), QStringLiteral("7za"),
                            QStringLiteral("7zz")}) {
    const QString found = QStandardPaths::findExecutable(n);
    if (!found.isEmpty() && !candidates.contains(found))
      candidates << found;
  }

  for (const QString& exe : candidates) {
    QProcess proc;
    proc.start(exe, {QStringLiteral("x"), archivePath,
                     QStringLiteral("-o") + destDir, QStringLiteral("-y")});
    if (proc.waitForFinished(300000) &&
        proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0)
      return true;
  }
  return false;
}

// Recursively find a file by name (case-insensitive) under dir. Ignore
// symlinks from the archive so traversal cannot escape the staging tree.
QString findFileInDir(const QString& dir, const QString& name)
{
  const auto entries = QDir(dir).entryInfoList(
      QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::NoSymLinks);
  for (const auto& entry : entries) {
    if (entry.isFile() &&
        entry.fileName().compare(name, Qt::CaseInsensitive) == 0)
      return entry.filePath();
    if (entry.isDir()) {
      const QString found = findFileInDir(entry.filePath(), name);
      if (!found.isEmpty())
        return found;
    }
  }
  return {};
}

bool isLootInstallCompatible(const QString& installDir,
                             const QString& requiredVersion)
{
  const QFileInfo rootInfo(installDir);
  if (!rootInfo.isDir() || rootInfo.isSymLink())
    return false;

  const QFileInfo exeInfo(QDir(installDir).filePath("LOOT.exe"));
  if (!exeInfo.isFile() || exeInfo.isSymLink())
    return false;

  const QString markerPath = QDir(installDir).filePath(LootVersionMarker);
  const QFileInfo markerInfo(markerPath);
  if (!markerInfo.isFile() || markerInfo.isSymLink())
    return false;
  QFile marker(markerPath);
  if (!marker.open(QIODevice::ReadOnly))
    return false;
  return QString::fromUtf8(marker.readAll()).trimmed() == requiredVersion;
}

bool writeVersionMarker(const QString& installDir, const QString& version)
{
  QSaveFile marker(QDir(installDir).filePath(LootVersionMarker));
  if (!marker.open(QIODevice::WriteOnly))
    return false;
  const QByteArray contents = version.toUtf8() + '\n';
  if (marker.write(contents) != contents.size())
    return false;
  return marker.commit();
}

bool validRelease(const LootRelease& release)
{
  if (release.version.isEmpty() || release.assetName.isEmpty() ||
      QFileInfo(release.assetName).fileName() != release.assetName ||
      release.assetUrl.isEmpty() || release.sha256Hex.size() != 64)
    return false;
  return std::all_of(release.sha256Hex.cbegin(), release.sha256Hex.cend(),
                     [](char ch) { return (ch >= '0' && ch <= '9') ||
                                           (ch >= 'a' && ch <= 'f') ||
                                           (ch >= 'A' && ch <= 'F'); });
}

QString fileSha256(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};

  QCryptographicHash hash(QCryptographicHash::Sha256);
  if (!hash.addData(&file))
    return {};
  return QString::fromLatin1(hash.result().toHex());
}

bool replaceInstallDirectory(const QString& preparedDir,
                             const QString& installDir,
                             QString* error)
{
  const QFileInfo currentInfo(installDir);
  if (currentInfo.isSymLink() || (currentInfo.exists() && !currentInfo.isDir())) {
    *error = QStringLiteral(
        "Refusing to replace LOOT because its managed path is not a directory");
    return false;
  }

  QString backupDir;
  const bool hasCurrent = currentInfo.exists();
  if (hasCurrent) {
    backupDir = QFileInfo(installDir).dir().filePath(
        QStringLiteral(".fluorine-loot-backup-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    if (!QDir().rename(installDir, backupDir)) {
      *error = QStringLiteral("Failed to stage the current LOOT installation");
      return false;
    }
  }

  if (!QDir().rename(preparedDir, installDir)) {
    if (hasCurrent && !QDir().rename(backupDir, installDir)) {
      MOBase::log::error(
          "LOOT update failed and the previous installation could not be restored from '{}'",
          backupDir.toStdString());
      *error = QStringLiteral(
          "LOOT update failed; previous installation is preserved at %1")
                   .arg(backupDir);
    } else {
      *error = QStringLiteral("Failed to activate the new LOOT installation");
    }
    return false;
  }

  if (hasCurrent && !QDir(backupDir).removeRecursively()) {
    MOBase::log::warn("Could not remove old LOOT files from '{}'",
                      backupDir.toStdString());
  }
  return true;
}

QString downloadLootImpl(
    const QString& dataDir, const LootRelease& release, const HttpGet& get,
    const ExtractArchive& extract,
    const std::function<void(float)>& progressCb,
    const std::function<void(const QString&)>& statusCb,
    const std::atomic_bool* cancelFlag)
{
  auto status = [&](const QString& message) {
    if (statusCb)
      statusCb(message);
  };
  auto progress = [&](float value) {
    if (progressCb)
      progressCb(value);
  };

  if (!validRelease(release) || !get || !extract)
    return QStringLiteral("Invalid LOOT release configuration");

  status(QStringLiteral("Preparing compatible LOOT %1...").arg(release.version));
  const QString toolsDir = QDir(dataDir).filePath("tools");
  if (!QDir().mkpath(toolsDir)) {
    MOBase::log::error("Failed to create LOOT tools directory '{}'",
                       toolsDir.toStdString());
    return QStringLiteral("Failed to create LOOT tools directory");
  }

  const QString installDir = QDir(toolsDir).filePath("loot");
  QTemporaryDir staging(QDir(toolsDir).filePath(".fluorine-loot-download-XXXXXX"));
  if (!staging.isValid()) {
    MOBase::log::error("Failed to create LOOT staging directory in '{}'",
                       toolsDir.toStdString());
    return QStringLiteral("Failed to create temporary download directory");
  }

  status(QStringLiteral("Downloading LOOT %1...").arg(release.version));
  const QString archivePath = staging.filePath(release.assetName);
  get(release.assetUrl, cancelFlag, progress, archivePath);
  if (cancelFlag && cancelFlag->load(std::memory_order_relaxed))
    return QStringLiteral("LOOT download was cancelled");
  const QFileInfo archiveInfo(archivePath);
  if (!archiveInfo.isFile() || archiveInfo.size() <= 0)
    return QStringLiteral("LOOT download failed");

  const QString actualHash = fileSha256(archivePath);
  if (actualHash.isEmpty() ||
      actualHash.compare(QString::fromLatin1(release.sha256Hex.constData(),
                                             release.sha256Hex.size()),
                         Qt::CaseInsensitive) != 0) {
    MOBase::log::error("LOOT {} archive checksum mismatch", release.version);
    return QStringLiteral("LOOT archive failed its SHA-256 integrity check");
  }
  progress(1.0f);

  status(QStringLiteral("Extracting LOOT..."));
  const QString extractDir = staging.filePath(QStringLiteral("extracted"));
  if (!QDir().mkpath(extractDir) || !extract(archivePath, extractDir))
    return QStringLiteral("Failed to extract the LOOT archive");

  const QString lootExe = findFileInDir(extractDir, QStringLiteral("LOOT.exe"));
  if (lootExe.isEmpty())
    return QStringLiteral("LOOT.exe not found after extraction");

  const QString preparedDir = staging.filePath(QStringLiteral("prepared-install"));
  const QString lootRoot = QFileInfo(lootExe).absolutePath();
  if (!QDir().rename(lootRoot, preparedDir))
    return QStringLiteral("Failed to prepare the extracted LOOT files");
  if (!writeVersionMarker(preparedDir, release.version))
    return QStringLiteral("Failed to write LOOT version metadata");

  QString replaceError;
  if (!replaceInstallDirectory(preparedDir, installDir, &replaceError))
    return replaceError;

  MOBase::log::info("LOOT {} installed to {}", release.version,
                    installDir.toStdString());
  status(QStringLiteral("LOOT installed successfully"));
  return {};
}

LootRelease pinnedLootRelease()
{
  return {QString::fromLatin1(LootVersion), QString::fromLatin1(LootAssetName),
          QString::fromLatin1(LootAssetUrl),
          QByteArray(LootAssetSha256)};
}

}  // namespace

QString lootInstallDir()
{
  return QDir(fluorineDataDir()).filePath("tools/loot");
}

bool isLootInstalled()
{
  return isLootInstallCompatible(lootInstallDir(),
                                 QString::fromLatin1(LootVersion));
}

QString getLootExePath()
{
  const QString path = QDir(lootInstallDir()).filePath("LOOT.exe");
  const QFileInfo info(path);
  return info.isFile() && !info.isSymLink() ? path : QString{};
}

QString downloadLoot(const std::function<void(float)>& progressCb,
                     const std::function<void(const QString&)>& statusCb,
                     const std::atomic_bool* cancelFlag)
{
  const QString dataDir = fluorineDataDir();
  HttpGet get = httpGet;
  ExtractArchive extract = [dataDir](const QString& archive,
                                     const QString& dest) {
    return extract7z(archive, dest, dataDir);
  };
  return downloadLootImpl(dataDir, pinnedLootRelease(), get, extract,
                          progressCb, statusCb, cancelFlag);
}

#ifdef FLUORINE_LOOTMANAGER_TESTING
bool LootManagerTesting::isInstalledAt(const QString& installDir,
                                      const QString& requiredVersion)
{
  return isLootInstallCompatible(installDir, requiredVersion);
}

QString LootManagerTesting::downloadAt(
    const QString& dataDir, const LootManagerTesting::Release& release,
    const LootManagerTesting::Dependencies& dependencies,
    const std::function<void(float)>& progressCb,
    const std::function<void(const QString&)>& statusCb,
    const std::atomic_bool* cancelFlag)
{
  return downloadLootImpl(
      dataDir,
      LootRelease{release.version, release.assetName, release.assetUrl,
                  release.sha256Hex},
      dependencies.httpGet, dependencies.extractArchive, progressCb, statusCb,
      cancelFlag);
}

LootManagerTesting::Release LootManagerTesting::pinnedRelease()
{
  const LootRelease release = pinnedLootRelease();
  return {release.version, release.assetName, release.assetUrl,
          release.sha256Hex};
}
#endif
