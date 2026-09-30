#include <archive/archive.h>

#include <QTemporaryDir>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

#ifndef FLUORINE_ARCHIVE_FIXTURE
#error "FLUORINE_ARCHIVE_FIXTURE must name the stored payload ZIP fixture"
#endif

namespace fs = std::filesystem;

namespace
{

class TempTree
{
public:
  bool isValid() const { return temp.isValid(); }
  fs::path path() const { return fs::path(temp.path().toStdString()); }

private:
  QTemporaryDir temp;
};

std::unique_ptr<Archive> openFixture()
{
  auto archive = CreateArchive();
  if (!archive->isValid() ||
      !archive->open(fs::path(FLUORINE_ARCHIVE_FIXTURE).wstring(), {})) {
    return nullptr;
  }
  return archive;
}

bool extractTo(Archive& archive, const fs::path& output,
               const std::vector<std::wstring>& mappedPaths)
{
  const auto& files = archive.getFileList();
  if (files.size() != 1) {
    return false;
  }
  for (const auto& path : mappedPaths) {
    files.front()->addOutputFilePath(path);
  }
  return archive.extract(output.wstring(), {}, {}, [](const std::wstring&) {});
}

bool extractTo(Archive& archive, const fs::path& output, const std::wstring& mappedPath)
{
  return extractTo(archive, output, std::vector<std::wstring>{mappedPath});
}

std::string readText(const fs::path& path)
{
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

}  // namespace

TEST(ArchiveExtractionContainment, RejectsParentTraversalBeforeWritingOutsideRoot)
{
  TempTree temp;
  ASSERT_TRUE(temp.isValid());
  auto archive = openFixture();
  ASSERT_NE(archive, nullptr);
  const fs::path root = temp.path();
  const fs::path output = root / "output";
  fs::create_directories(output);

  EXPECT_FALSE(extractTo(*archive, output, L"../escaped.txt"));
  EXPECT_FALSE(fs::exists(root / "escaped.txt"));
}

TEST(ArchiveExtractionContainment, ValidatesEveryMappedPathBeforeWriting)
{
  TempTree temp;
  ASSERT_TRUE(temp.isValid());
  auto archive = openFixture();
  ASSERT_NE(archive, nullptr);
  const fs::path output = temp.path() / "output";
  fs::create_directories(output);

  EXPECT_FALSE(extractTo(*archive, output,
                         std::vector<std::wstring>{L"safe.txt", L"nested/../../escaped.txt"}));
  EXPECT_FALSE(fs::exists(output / "safe.txt"));
  EXPECT_FALSE(fs::exists(temp.path() / "escaped.txt"));
}

TEST(ArchiveExtractionContainment, RejectsRootedDriveAndUncMappedPaths)
{
  const std::vector<std::wstring> unsafePaths = {
      L"/absolute.txt", L"C:/drive.txt", L"//server/share.txt"};

  for (const auto& unsafePath : unsafePaths) {
    TempTree temp;
    ASSERT_TRUE(temp.isValid());
    auto archive = openFixture();
    ASSERT_NE(archive, nullptr);
    const fs::path root = temp.path();
    const fs::path output = root / "output";
    fs::create_directories(output);

    EXPECT_FALSE(extractTo(*archive, output, unsafePath)) << "path: " << unsafePath;
    EXPECT_FALSE(fs::exists(root / "absolute.txt"));
    EXPECT_FALSE(fs::exists(root / "drive.txt"));
  }
}

#ifndef _WIN32
TEST(ArchiveExtractionContainment, RejectsSymlinkedDirectoryAndFinalFile)
{
  {
    TempTree temp;
    ASSERT_TRUE(temp.isValid());
    auto archive = openFixture();
    ASSERT_NE(archive, nullptr);
    const fs::path root = temp.path();
    const fs::path output = root / "output";
    const fs::path outside = root / "outside";
    fs::create_directories(output);
    fs::create_directories(outside);
    fs::create_directory_symlink(outside, output / "link");

    EXPECT_FALSE(extractTo(*archive, output, L"link/escaped.txt"));
    EXPECT_FALSE(fs::exists(outside / "escaped.txt"));
  }

  {
    TempTree temp;
    ASSERT_TRUE(temp.isValid());
    auto archive = openFixture();
    ASSERT_NE(archive, nullptr);
    const fs::path root = temp.path();
    const fs::path output = root / "output";
    const fs::path target = root / "target.txt";
    fs::create_directories(output);
    {
      std::ofstream initial(target);
      initial << "preserve me";
    }
    fs::create_symlink(target, output / "payload.txt");

    EXPECT_FALSE(extractTo(*archive, output, L"payload.txt"));
    EXPECT_EQ(readText(target), "preserve me");
  }
}

TEST(ArchiveExtractionContainment, ReplacesHardLinksWithoutChangingTheirTarget)
{
  TempTree temp;
  ASSERT_TRUE(temp.isValid());
  auto archive = openFixture();
  ASSERT_NE(archive, nullptr);
  const fs::path root = temp.path();
  const fs::path output = root / "output";
  const fs::path target = root / "target.txt";
  fs::create_directories(output);
  {
    std::ofstream initial(target);
    initial << "preserve me";
  }
  fs::create_hard_link(target, output / "payload.txt");

  EXPECT_TRUE(extractTo(*archive, output, L"payload.txt"));
  EXPECT_EQ(readText(target), "preserve me");
  EXPECT_EQ(readText(output / "payload.txt"), "fixture payload\n");
}

TEST(ArchiveExtractionContainment, RejectsFifoOutputWithoutBlocking)
{
  TempTree temp;
  ASSERT_TRUE(temp.isValid());
  auto archive = openFixture();
  ASSERT_NE(archive, nullptr);
  const fs::path output = temp.path() / "output";
  fs::create_directories(output);
  ASSERT_EQ(::mkfifo((output / "payload.txt").c_str(), 0600), 0);

  EXPECT_FALSE(extractTo(*archive, output, L"payload.txt"));
  EXPECT_TRUE(fs::is_fifo(output / "payload.txt"));
}
#endif

TEST(ArchiveExtractionContainment, KeepsUnicodeAndBackslashMappedNames)
{
  TempTree temp;
  ASSERT_TRUE(temp.isValid());
  auto archive = openFixture();
  ASSERT_NE(archive, nullptr);
  const fs::path output = temp.path() / "output";
  fs::create_directories(output);

  ASSERT_TRUE(extractTo(*archive, output, L"mods\\naïve\\folder\\layout.txt"));
  const fs::path extracted = output / fs::path(u8"mods/naïve/folder/layout.txt");
  EXPECT_EQ(readText(extracted), "fixture payload\n");
}
