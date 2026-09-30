#include <uibase/utility.h>
#include "envfs.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

TEST(DirectoryWalker, SkipsSymlinkFilesDirectoriesAndCycles)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  ASSERT_TRUE(QDir().mkpath(temp.filePath("root/nested")));
  QFile file(temp.filePath("root/nested/file.txt"));
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write("data");
  file.close();
  ASSERT_TRUE(QFile::link(temp.filePath("root"), temp.filePath("root/loop")));
  ASSERT_TRUE(QFile::link(file.fileName(), temp.filePath("root/file-link")));
  ASSERT_TRUE(QFile::link(temp.filePath("root/nested"), temp.filePath("root/dir-link")));
  struct Counts { unsigned directories = 0; unsigned files = 0; } counts;
  env::forEachEntry(temp.filePath("root").toStdWString(), &counts,
      [](void* context, std::wstring_view) { ++static_cast<Counts*>(context)->directories; },
      nullptr,
      [](void* context, std::wstring_view, FILETIME, uint64_t) {
        ++static_cast<Counts*>(context)->files;
      });
  EXPECT_EQ(counts.directories, 1u);
  EXPECT_EQ(counts.files, 1u);
}
