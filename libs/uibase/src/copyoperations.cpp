#include <uibase/utility.h>
#include <QDebug>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>

#include <filesystem>
#include <memory>
#include <vector>

namespace MOBase
{
namespace
{
namespace fs = std::filesystem;

fs::path nativePath(const QString& path)
{
#ifdef _WIN32
  return fs::path(path.toStdWString());
#else
  return fs::path(QFile::encodeName(path).constData());
#endif
}

// Copy symlinks themselves, including dangling links. Never walk them, or write
// through a link already present in a destination directory.
bool copyTree(const fs::path& source, const fs::path& destination)
{
  std::error_code error;
  fs::directory_iterator entries(source, error);
  if (error) return false;
  const fs::directory_iterator end;
  while (entries != end) {
    const auto entry = *entries;
    const auto target = destination / entry.path().filename();
    const auto status = entry.symlink_status(error);
    if (error) return false;
    const auto targetStatus = fs::symlink_status(target, error);
    if (error && error != std::errc::no_such_file_or_directory) return false;
    error.clear();
    if (fs::is_directory(status)) {
      if (fs::is_symlink(targetStatus) ||
          (fs::exists(targetStatus) && !fs::is_directory(targetStatus))) return false;
      fs::create_directories(target, error);
      if (error || !copyTree(entry.path(), target)) return false;
    } else if (fs::is_regular_file(status) || fs::is_symlink(status)) {
      if (fs::is_directory(targetStatus)) return false;
      // The caller uses a private staging tree; removing its previous entry is
      // safe and avoids following a destination symlink during a merge.
      if (fs::exists(targetStatus) || fs::is_symlink(targetStatus)) {
        fs::remove(target, error);
        if (error) return false;
      }
      fs::copy(entry.path(), target, fs::copy_options::copy_symlinks, error);
      if (error) return false;
    } else {
      return false;  // sockets, devices and FIFOs cannot form a restorable backup
    }
    entries.increment(error);
    if (error) return false;
  }
  return true;
}

bool isWithin(const fs::path& path, const fs::path& parent)
{
  auto entry = path.begin();
  for (auto component = parent.begin(); component != parent.end(); ++component, ++entry) {
    if (entry == path.end() || *entry != *component) return false;
  }
  return true;
}

struct PreparedCopy
{
  QString destination;
  std::unique_ptr<QTemporaryDir> staging;
  bool existed;
};
}  // namespace

bool copyDir(const QString& sourceName, const QString& destinationName, bool merge)
{
  std::error_code error;
  const auto source = fs::canonical(nativePath(sourceName), error);
  if (error || !fs::is_directory(source, error) || error) return false;
  const QFileInfo target(destinationName);
  const auto destination = fs::weakly_canonical(nativePath(target.absoluteFilePath()), error);
  if (error || isWithin(destination, source) || isWithin(source, destination)) return false;
  const bool existed = target.exists();
  if (target.isSymLink() || (existed && (!target.isDir() || !merge))) return false;
  if (!QDir().mkpath(target.absolutePath())) return false;

  QTemporaryDir staging(target.absolutePath() + "/.fluorine-copy-XXXXXX");
  if (!staging.isValid()) return false;
  const auto payload = nativePath(staging.filePath("contents"));
  fs::create_directory(payload, error);
  if (error) return false;
  if (existed && !copyTree(destination, payload)) return false;
  if (!copyTree(source, payload)) return false;
  const auto permissions = fs::status(existed ? destination : source, error).permissions();
  if (error) return false;
  fs::permissions(payload, permissions, error);
  if (error) return false;

  const auto original = nativePath(staging.filePath("original"));
  if (existed) {
    fs::rename(destination, original, error);
    if (error) return false;
  }
  fs::rename(payload, destination, error);
  if (!error) return true;
  if (existed) {
    fs::rename(original, destination, error);
    if (error) {
      staging.setAutoRemove(false);
      qWarning().noquote() << "Could not restore directory; original retained at"
                           << staging.filePath("original");
    }
  }
  return false;
}

bool shellCopy(const QStringList& sourceNames, const QStringList& destinationNames,
               QWidget*)
{
  QStringList destinations = destinationNames;
  if (destinations.size() == 1 && sourceNames.size() > 1) {
    const QString directory = destinations.first();
    destinations.clear();
    for (const auto& source : sourceNames) {
      destinations.append(QDir(directory).filePath(QFileInfo(source).fileName()));
    }
  }
  if (destinations.size() != sourceNames.size()) return false;

  std::vector<PreparedCopy> prepared;
  QSet<QString> seen;
  for (qsizetype i = 0; i < sourceNames.size(); ++i) {
    const QFileInfo source(sourceNames[i]);
    const QFileInfo target(destinations[i]);
    if (!source.isFile() || target.isDir()) return false;
    if (source.canonicalFilePath() == target.canonicalFilePath()) continue;
    if (!QDir().mkpath(target.absolutePath())) return false;
    const QString destination = QDir(target.absolutePath()).canonicalPath() +
                                '/' + target.fileName();
    if (seen.contains(destination)) return false;
    seen.insert(destination);
    auto staging = std::make_unique<QTemporaryDir>(
        target.absolutePath() + "/.fluorine-copy-XXXXXX");
    if (!staging->isValid() || !QFile::copy(source.absoluteFilePath(),
                                         staging->filePath("contents"))) return false;
    const bool existed = target.exists() || target.isSymLink();
    if (existed) {
      std::error_code error;
      fs::copy(nativePath(destination), nativePath(staging->filePath("original")),
               fs::copy_options::copy_symlinks, error);
      if (error) return false;
    }
    prepared.push_back({destination, std::move(staging), existed});
  }

  for (size_t i = 0; i < prepared.size(); ++i) {
    auto& item = prepared[i];
    std::error_code error;
    fs::rename(nativePath(item.staging->filePath("contents")),
               nativePath(item.destination), error);
    if (!error) continue;
    // All input and backup copies are complete before the first replacement.
    // If publication fails, restore each destination already committed.
    while (i > 0) {
      auto& previous = prepared[--i];
      if (previous.existed) {
        fs::rename(nativePath(previous.staging->filePath("original")),
                   nativePath(previous.destination), error);
      } else {
        fs::remove(nativePath(previous.destination), error);
      }
      if (error) {
        previous.staging->setAutoRemove(false);
        qWarning().noquote() << "Could not roll back copy to" << previous.destination
                             << "; backup retained at" << previous.staging->path();
      }
    }
    return false;
  }
  return true;
}

bool shellCopy(const QString& source, const QString& destination, bool, QWidget* parent)
{
  return shellCopy(QStringList{source}, QStringList{destination}, parent);
}
}  // namespace MOBase
