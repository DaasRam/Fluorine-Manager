#include "rootlocatordeployment.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string_view>

namespace {
namespace fs = std::filesystem;

enum class EntryKind { Missing, Regular, Symlink, Other, Error };

EntryKind inspect(const fs::path &path) {
  std::error_code error;
  const fs::file_status status = fs::symlink_status(path, error);
  if (error == std::errc::no_such_file_or_directory)
    return EntryKind::Missing;
  if (error)
    return EntryKind::Error;
  if (!fs::exists(status))
    return EntryKind::Missing;
  if (fs::is_symlink(status))
    return EntryKind::Symlink;
  if (fs::is_regular_file(status))
    return EntryKind::Regular;
  return EntryKind::Other;
}

bool generationIsSafe(const std::string &generation) {
  if (generation.size() != 36)
    return false;
  for (std::size_t index = 0; index < generation.size(); ++index) {
    if (index == 8 || index == 13 || index == 18 || index == 23) {
      if (generation[index] != '-')
        return false;
    } else if (!std::isxdigit(static_cast<unsigned char>(generation[index]))) {
      return false;
    }
  }
  return true;
}

bool filesEqual(const fs::path &left, const fs::path &right) {
  if (inspect(left) != EntryKind::Regular ||
      inspect(right) != EntryKind::Regular) {
    return false;
  }
  std::error_code error;
  const auto leftSize = fs::file_size(left, error);
  if (error)
    return false;
  const auto rightSize = fs::file_size(right, error);
  if (error || leftSize != rightSize)
    return false;

  std::ifstream leftStream(left, std::ios::binary);
  std::ifstream rightStream(right, std::ios::binary);
  if (!leftStream || !rightStream)
    return false;
  std::array<char, 4096> leftBuffer{};
  std::array<char, 4096> rightBuffer{};
  for (;;) {
    leftStream.read(leftBuffer.data(), leftBuffer.size());
    rightStream.read(rightBuffer.data(), rightBuffer.size());
    if (leftStream.gcount() != rightStream.gcount() ||
        !std::equal(leftBuffer.begin(),
                    leftBuffer.begin() + leftStream.gcount(),
                    rightBuffer.begin())) {
      return false;
    }
    if (leftStream.eof() && rightStream.eof())
      return true;
    if (!leftStream || !rightStream)
      return false;
  }
}

struct Manifest {
  QString state;
  std::string generation;
  fs::path target;
  fs::path source;
  fs::path temporary;
  fs::path backup;
  bool hadBackup = false;
};

fs::path storagePath(const fs::path &outputBase) {
  return outputBase / ".vfs-indexer";
}

fs::path manifestPath(const fs::path &outputBase) {
  return storagePath(outputBase) / "root-deployment.json";
}

fs::path expectedTarget(const fs::path &gameDirectory) {
  return gameDirectory / kVfsIndexLocatorName;
}

fs::path expectedSource(const fs::path &outputBase) {
  return outputBase / kVfsIndexLocatorName;
}

fs::path expectedTemporary(const fs::path &target,
                           const std::string &generation) {
  return target.parent_path() / (".vfs-index-" + generation + ".tmp");
}

fs::path expectedBackup(const fs::path &outputBase,
                        const std::string &generation) {
  return storagePath(outputBase) / ("root-locator-backup-" + generation);
}

bool pathsMatch(const Manifest &parsed, const fs::path &outputBase,
                const fs::path &gameDirectory) {
  if (!generationIsSafe(parsed.generation))
    return false;
  return parsed.target.lexically_normal() ==
             expectedTarget(gameDirectory).lexically_normal() &&
         parsed.source.lexically_normal() ==
             expectedSource(outputBase).lexically_normal() &&
         parsed.temporary.lexically_normal() ==
             expectedTemporary(parsed.target, parsed.generation)
                 .lexically_normal() &&
         parsed.backup.lexically_normal() ==
             expectedBackup(outputBase, parsed.generation).lexically_normal();
}

bool readManifest(const fs::path &path, Manifest &manifest) {
  if (inspect(path) != EntryKind::Regular)
    return false;
  std::error_code error;
  const auto size = fs::file_size(path, error);
  if (error || size > 64 * 1024)
    return false;
  QFile input(QString::fromStdString(path.string()));
  if (!input.open(QIODevice::ReadOnly))
    return false;
  const QByteArray contents = input.readAll();
  if (input.error() != QFileDevice::NoError ||
      static_cast<std::uint64_t>(contents.size()) != size) {
    return false;
  }
  const QJsonDocument document = QJsonDocument::fromJson(contents);
  if (!document.isObject())
    return false;
  const QJsonObject json = document.object();
  if (!json.value(QStringLiteral("state")).isString() ||
      !json.value(QStringLiteral("generation")).isString() ||
      !json.value(QStringLiteral("target")).isString() ||
      !json.value(QStringLiteral("source")).isString() ||
      !json.value(QStringLiteral("temporary")).isString() ||
      !json.value(QStringLiteral("backup")).isString() ||
      !json.value(QStringLiteral("had_backup")).isBool()) {
    return false;
  }
  manifest.state = json.value(QStringLiteral("state")).toString();
  manifest.generation =
      json.value(QStringLiteral("generation")).toString().toStdString();
  manifest.target =
      json.value(QStringLiteral("target")).toString().toStdString();
  manifest.source =
      json.value(QStringLiteral("source")).toString().toStdString();
  manifest.temporary =
      json.value(QStringLiteral("temporary")).toString().toStdString();
  manifest.backup =
      json.value(QStringLiteral("backup")).toString().toStdString();
  manifest.hadBackup = json.value(QStringLiteral("had_backup")).toBool();
  return manifest.state == QStringLiteral("deploying") ||
         manifest.state == QStringLiteral("complete");
}

bool writeManifest(const fs::path &path, const Manifest &manifest) {
  QJsonObject json;
  json.insert(QStringLiteral("state"), manifest.state);
  json.insert(QStringLiteral("generation"),
              QString::fromStdString(manifest.generation));
  json.insert(QStringLiteral("target"),
              QString::fromStdString(manifest.target.string()));
  json.insert(QStringLiteral("source"),
              QString::fromStdString(manifest.source.string()));
  json.insert(QStringLiteral("temporary"),
              QString::fromStdString(manifest.temporary.string()));
  json.insert(QStringLiteral("backup"),
              QString::fromStdString(manifest.backup.string()));
  json.insert(QStringLiteral("had_backup"), manifest.hadBackup);
  QSaveFile output(QString::fromStdString(path.string()));
  const QByteArray contents =
      QJsonDocument(json).toJson(QJsonDocument::Compact);
  return output.open(QIODevice::WriteOnly) &&
         output.write(contents) == contents.size() && output.commit();
}

bool saveBackupToTarget(const fs::path &backup, const fs::path &target) {
  const QString backupName = QString::fromStdString(backup.string());
  QFile input(backupName);
  if (!input.open(QIODevice::ReadOnly))
    return false;
  QSaveFile output(QString::fromStdString(target.string()));
  if (!output.open(QIODevice::WriteOnly))
    return false;
  if (!output.setPermissions(QFile::permissions(backupName))) {
    output.cancelWriting();
    return false;
  }
  std::array<char, 16 * 1024> buffer{};
  while (!input.atEnd()) {
    const qint64 count = input.read(buffer.data(), buffer.size());
    if (count < 0 ||
        (count > 0 && output.write(buffer.data(), count) != count)) {
      output.cancelWriting();
      return false;
    }
  }
  return input.error() == QFileDevice::NoError && output.commit();
}

void removeRegular(const fs::path &path) {
  if (inspect(path) != EntryKind::Regular)
    return;
  std::error_code ignored;
  fs::remove(path, ignored);
}
} // namespace

namespace VfsRootLocatorDeployment {
bool deploy(const fs::path &outputBase, const fs::path &gameDirectory,
            VfsIndexPublicationResult &publication) {
  try {
    const fs::path target = expectedTarget(gameDirectory);
    const fs::path source = publication.locator_path;
    if (target.lexically_normal() == source.lexically_normal()) {
      publication.root_locator_path = target;
      publication.root_locator_deployed = true;
      return true;
    }
    if (!generationIsSafe(publication.generation) ||
        !outputBase.is_absolute() || !gameDirectory.is_absolute() ||
        !source.is_absolute() ||
        source.lexically_normal() !=
            expectedSource(outputBase).lexically_normal()) {
      publication.error = "invalid root locator deployment paths or generation";
      return false;
    }

    const EntryKind targetKind = inspect(target);
    if (targetKind == EntryKind::Symlink || targetKind == EntryKind::Other) {
      publication.error =
          "refusing to replace a symlink or non-regular game locator";
      return false;
    }
    if (targetKind == EntryKind::Error ||
        inspect(source) != EntryKind::Regular) {
      publication.error = "unable to inspect regular locator files";
      return false;
    }

    const fs::path storage = storagePath(outputBase);
    const fs::path manifestPathValue = manifestPath(outputBase);
    const fs::path temporary =
        expectedTemporary(target, publication.generation);
    const fs::path backup = expectedBackup(outputBase, publication.generation);
    if (inspect(manifestPathValue) != EntryKind::Missing ||
        inspect(temporary) != EntryKind::Missing ||
        inspect(backup) != EntryKind::Missing) {
      publication.error = "previous root locator deployment needs recovery";
      return false;
    }

    std::error_code error;
    fs::create_directories(storage, error);
    if (error) {
      publication.error = error.message();
      return false;
    }

    Manifest manifest{QStringLiteral("deploying"),
                      publication.generation,
                      target,
                      source,
                      temporary,
                      backup,
                      targetKind == EntryKind::Regular};
    bool backupCreated = false;
    if (manifest.hadBackup) {
      fs::copy_file(target, backup, fs::copy_options::none, error);
      if (error || inspect(backup) != EntryKind::Regular ||
          !filesEqual(target, backup)) {
        removeRegular(backup);
        publication.error =
            error ? error.message() : "unable to verify locator backup";
        return false;
      }
      backupCreated = true;
    }

    if (!writeManifest(manifestPathValue, manifest)) {
      if (backupCreated)
        removeRegular(backup);
      publication.error = "unable to save root locator manifest";
      return false;
    }

    fs::copy_file(source, temporary, fs::copy_options::none, error);
    if (error || inspect(temporary) != EntryKind::Regular) {
      removeRegular(temporary);
      (void)clear(outputBase, gameDirectory);
      publication.error =
          error ? error.message() : "unable to create locator copy";
      return false;
    }

    // Re-check the no-follow entry immediately before replacement. In addition
    // to the initial check, this prevents replacing an entry changed while the
    // backup and temporary locator were being prepared.
    const EntryKind currentTargetKind = inspect(target);
    if ((manifest.hadBackup && (currentTargetKind != EntryKind::Regular ||
                                !filesEqual(target, backup))) ||
        (!manifest.hadBackup && currentTargetKind != EntryKind::Missing)) {
      publication.error = "game locator changed during deployment";
      return false;
    }

    // Both paths are siblings, so rename replaces an existing regular file
    // atomically on the supported filesystems. Keeping the old file in place
    // until this point avoids a crash window with no locator at all.
    fs::rename(temporary, target, error);
    if (error) {
      (void)clear(outputBase, gameDirectory);
      publication.error = error.message();
      return false;
    }

    manifest.state = QStringLiteral("complete");
    if (!writeManifest(manifestPathValue, manifest)) {
      (void)clear(outputBase, gameDirectory);
      publication.error = "unable to complete root locator manifest";
      return false;
    }

    publication.root_locator_path = target;
    publication.root_locator_deployed = true;
    return true;
  } catch (const std::exception &error) {
    publication.error = error.what();
    return false;
  } catch (...) {
    publication.error = "unknown root locator deployment failure";
    return false;
  }
}

bool clear(const fs::path &outputBase, const fs::path &gameDirectory) {
  try {
    const fs::path targetExpected = expectedTarget(gameDirectory);
    const fs::path sourceExpected = expectedSource(outputBase);
    if (targetExpected.lexically_normal() ==
        sourceExpected.lexically_normal()) {
      return true;
    }
    const fs::path manifestFile = manifestPath(outputBase);
    if (inspect(manifestFile) == EntryKind::Missing)
      return true;

    Manifest manifest;
    if (!readManifest(manifestFile, manifest) ||
        !pathsMatch(manifest, outputBase, gameDirectory)) {
      return false;
    }

    // Never clean up around a target that has become a symlink or special file.
    // In particular, do not remove the target or discard its recovery manifest.
    const EntryKind targetKind = inspect(manifest.target);
    if (targetKind == EntryKind::Symlink || targetKind == EntryKind::Other ||
        targetKind == EntryKind::Error) {
      return false;
    }
    const EntryKind temporaryKind = inspect(manifest.temporary);
    const EntryKind backupKind = inspect(manifest.backup);
    if ((temporaryKind != EntryKind::Missing &&
         temporaryKind != EntryKind::Regular) ||
        (backupKind != EntryKind::Missing &&
         backupKind != EntryKind::Regular)) {
      return false;
    }

    if (manifest.state == QStringLiteral("deploying") && manifest.hadBackup &&
        targetKind == EntryKind::Regular && backupKind == EntryKind::Regular &&
        filesEqual(manifest.target, manifest.backup)) {
      removeRegular(manifest.temporary);
      removeRegular(manifest.backup);
      removeRegular(manifestFile);
      return true;
    }

    const bool generatedUnchanged =
        targetKind == EntryKind::Missing ||
        (targetKind == EntryKind::Regular &&
         filesEqual(manifest.target, manifest.source));
    if (!generatedUnchanged)
      return false;

    removeRegular(manifest.temporary);
    if (manifest.hadBackup) {
      if (backupKind != EntryKind::Regular)
        return false;
      if (!saveBackupToTarget(manifest.backup, manifest.target))
        return false;
    } else if (targetKind == EntryKind::Regular) {
      std::error_code error;
      fs::remove(manifest.target, error);
      if (error)
        return false;
    }

    removeRegular(manifest.backup);
    removeRegular(manifestFile);
    return true;
  } catch (...) {
    return false;
  }
}
} // namespace VfsRootLocatorDeployment
