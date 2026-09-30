#include <uibase/utility.h>

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTemporaryDir>
#include <cerrno>
#include <system_error>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace
{
void writeFile(const QString& path, const QByteArray& contents)
{
  ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(contents), contents.size());
}

QByteArray readFile(const QString& path)
{
  QFile file(path);
  EXPECT_TRUE(file.open(QIODevice::ReadOnly));
  return file.readAll();
}
}  // namespace

TEST(NativeErrors, KeepsErrnoSeparateFromWindowsErrorCodes)
{
  EXPECT_EQ(MOBase::nativeErrorString(EIO).toStdString(),
            std::error_code(EIO, std::generic_category()).message());
  EXPECT_NE(MOBase::nativeErrorString(EIO).toStdWString(),
            MOBase::formatSystemMessage(static_cast<DWORD>(5)));
}

TEST(ShellCopy, MissingInputPreservesEveryDestination)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto first = temporary.filePath("plugins.txt");
  const auto second = temporary.filePath("loadorder.txt");
  const auto backup = temporary.filePath("plugins.txt.backup");
  writeFile(first, "live plugins");
  writeFile(second, "live load order");
  writeFile(backup, "backup plugins");
  EXPECT_FALSE(MOBase::shellCopy(
      QStringList{backup, temporary.filePath("missing.backup")},
      QStringList{first, second}));
  EXPECT_EQ(readFile(first), "live plugins");
  EXPECT_EQ(readFile(second), "live load order");
}

TEST(ShellCopy, ReplacesCompleteSetAndAllowsSameFile)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto first = temporary.filePath("plugins.txt");
  const auto second = temporary.filePath("loadorder.txt");
  writeFile(first, "old");
  writeFile(second, "old");
  writeFile(first + ".backup", "new plugins");
  writeFile(second + ".backup", "new order");
  ASSERT_TRUE(MOBase::shellCopy(QStringList{first + ".backup", second + ".backup"},
                              QStringList{first, second}));
  EXPECT_EQ(readFile(first), "new plugins");
  EXPECT_EQ(readFile(second), "new order");
  EXPECT_TRUE(MOBase::shellCopy(first, first));
  EXPECT_EQ(readFile(first), "new plugins");
}

TEST(DirectoryCopy, IncludesHiddenFilesAndCopiesLinksWithoutRecursion)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto source = temporary.filePath("source");
  const auto destination = temporary.filePath("backup");
  writeFile(source + "/.45 Auto Pistol/.hidden", "content");
  ASSERT_TRUE(QFile::link(source, source + "/loop"));
  ASSERT_TRUE(MOBase::copyDir(source, destination, false));
  EXPECT_EQ(readFile(destination + "/.45 Auto Pistol/.hidden"), "content");
  EXPECT_TRUE(QFileInfo(destination + "/loop").isSymLink());
  EXPECT_FALSE(MOBase::copyDir(source, destination, false));
}

TEST(DirectoryCopy, FailedBackupIsNotPublishedAndMergePreservesDestination)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const auto source = temporary.filePath("source");
  const auto destination = temporary.filePath("backup");
  writeFile(source + "/nested/file", "new");
  writeFile(destination + "/nested", "existing file blocks directory");
  writeFile(destination + "/keep", "keep");
  EXPECT_FALSE(MOBase::copyDir(source, destination, true));
  EXPECT_EQ(readFile(destination + "/nested"), "existing file blocks directory");
  EXPECT_EQ(readFile(destination + "/keep"), "keep");
  EXPECT_FALSE(MOBase::copyDir(source, source + "/child", false));
  EXPECT_FALSE(QFileInfo::exists(source + "/child"));
  const auto blocked = temporary.filePath("file");
  writeFile(blocked, "blocked");
  EXPECT_FALSE(MOBase::copyDir(source, blocked + "/backup", false));
}

TEST(DirectoryCopy, RejectsRelativeDestinationInsideSource)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  writeFile(temporary.filePath("original"), "keep");
  struct RestoreDirectory {
    QString original = QDir::currentPath();
    ~RestoreDirectory() { QDir::setCurrent(original); }
  } restore;
  ASSERT_TRUE(QDir::setCurrent(temporary.path()));
  EXPECT_FALSE(MOBase::copyDir(temporary.path(), "nested/backup", false));
  EXPECT_FALSE(QFileInfo::exists("nested"));
  EXPECT_EQ(readFile("original"), "keep");
}

TEST(ShellMove, ReplacesFilesAndRecursivelyMergesDirectories)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = QDir(temporary.path()).filePath("overwrite");
  const QString destination = QDir(temporary.path()).filePath("mod");
  writeFile(QDir(source).filePath("nested/replaced.txt"), "new");
  writeFile(QDir(source).filePath("nested/added.txt"), "added");
  writeFile(QDir(destination).filePath("nested/replaced.txt"), "old");
  writeFile(QDir(destination).filePath("keep.txt"), "keep");

  ASSERT_TRUE(MOBase::shellMove(QStringList{source}, QStringList{destination}));
  EXPECT_FALSE(QFileInfo::exists(source));
  EXPECT_EQ(readFile(QDir(destination).filePath("nested/replaced.txt")), "new");
  EXPECT_EQ(readFile(QDir(destination).filePath("nested/added.txt")), "added");
  EXPECT_EQ(readFile(QDir(destination).filePath("keep.txt")), "keep");
}

TEST(ShellMove, MovesDirectoryContentsWithoutRemovingContainer)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = QDir(temporary.path()).filePath("overwrite");
  const QString destination = QDir(temporary.path()).filePath("mod");
  writeFile(QDir(source).filePath("nested/file.txt"), "contents");
  writeFile(QDir(destination).filePath("existing.txt"), "keep");

  const QFileInfoList entries =
      QDir(source).entryInfoList(QDir::AllEntries | QDir::Hidden |
                                  QDir::System | QDir::NoDotAndDotDot);
  QStringList sourcePaths;
  QStringList destinationPaths;
  for (const QFileInfo &entry : entries) {
    sourcePaths.append(entry.absoluteFilePath());
    destinationPaths.append(QDir(destination).filePath(entry.fileName()));
  }

  ASSERT_TRUE(MOBase::shellMove(sourcePaths, destinationPaths));
  EXPECT_TRUE(QFileInfo(source).isDir());
  EXPECT_TRUE(QDir(source).entryInfoList(QDir::AllEntries | QDir::Hidden |
                                         QDir::System | QDir::NoDotAndDotDot)
                  .isEmpty());
  EXPECT_EQ(readFile(QDir(destination).filePath("nested/file.txt")),
            "contents");
  EXPECT_EQ(readFile(QDir(destination).filePath("existing.txt")), "keep");
}

TEST(ShellMove, ReplacesSingleExistingFile)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = QDir(temporary.path()).filePath("source.txt");
  const QString destination = QDir(temporary.path()).filePath("destination.txt");
  writeFile(source, "new");
  writeFile(destination, "old");

  ASSERT_TRUE(MOBase::shellMove(source, destination));
  EXPECT_FALSE(QFileInfo::exists(source));
  EXPECT_EQ(readFile(destination), "new");
}

TEST(ShellMove, ReportsFailureWithoutClaimingSuccess)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString source = QDir(temporary.path()).filePath("source.txt");
  const QString blockedParent = QDir(temporary.path()).filePath("blocked");
  writeFile(source, "contents");
  writeFile(blockedParent, "not a directory");

  EXPECT_FALSE(MOBase::shellMove(
      source, QDir(blockedParent).filePath("destination.txt")));
  EXPECT_TRUE(QFileInfo::exists(source));
  EXPECT_EQ(readFile(source), "contents");
}

#ifndef _WIN32
TEST(ShellMove, FallsBackAcrossFilesystemsWhenAvailable)
{
  if (!QFileInfo::exists("/dev/shm")) {
    GTEST_SKIP() << "/dev/shm is unavailable";
  }
  QTemporaryDir sourceDir("/dev/shm/fluorine-shell-move-src-XXXXXX");
  QTemporaryDir destinationDir;
  ASSERT_TRUE(sourceDir.isValid());
  ASSERT_TRUE(destinationDir.isValid());

  struct stat sourceStat = {};
  struct stat destinationStat = {};
  ASSERT_EQ(::stat(sourceDir.path().toLocal8Bit().constData(), &sourceStat), 0);
  ASSERT_EQ(::stat(destinationDir.path().toLocal8Bit().constData(), &destinationStat), 0);
  if (sourceStat.st_dev == destinationStat.st_dev) {
    GTEST_SKIP() << "temporary directories share a filesystem";
  }

  const QString source = QDir(sourceDir.path()).filePath("overwrite");
  const QString destination = QDir(destinationDir.path()).filePath("mod");
  writeFile(QDir(source).filePath("nested/replaced.txt"), "new");
  writeFile(QDir(source).filePath("fresh/deep/added.txt"), "added");
  writeFile(QDir(destination).filePath("nested/replaced.txt"), "old");
  writeFile(QDir(destination).filePath("keep.txt"), "keep");

  ASSERT_TRUE(MOBase::shellMove(source, destination));
  EXPECT_FALSE(QFileInfo::exists(source));
  EXPECT_EQ(readFile(QDir(destination).filePath("nested/replaced.txt")),
            "new");
  EXPECT_EQ(readFile(QDir(destination).filePath("fresh/deep/added.txt")),
            "added");
  EXPECT_EQ(readFile(QDir(destination).filePath("keep.txt")), "keep");
}
#endif
