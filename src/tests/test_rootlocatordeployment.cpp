#include "vfs/rootlocatordeployment.h"

#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>

namespace {
namespace fs = std::filesystem;

constexpr char kGeneration[] = "123e4567-e89b-12d3-a456-426614174000";

void writeFile(const fs::path &path, const std::string &contents) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  ASSERT_TRUE(output.is_open());
  output << contents;
  ASSERT_TRUE(output.good());
}

std::string readFile(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

class DeploymentFixture : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_TRUE(temp.isValid());
    outputBase = fs::path(temp.path().toStdString()) / "instance";
    gameDirectory = fs::path(temp.path().toStdString()) / "game";
    fs::create_directories(outputBase);
    fs::create_directories(gameDirectory);
    source = outputBase / kVfsIndexLocatorName;
    target = gameDirectory / kVfsIndexLocatorName;
    writeFile(source, "generated locator contents\n");
    publication.success = true;
    publication.generation = kGeneration;
    publication.locator_path = source;
  }

  fs::path storage() const { return outputBase / ".vfs-indexer"; }
  fs::path manifest() const { return storage() / "root-deployment.json"; }
  fs::path backup() const {
    return storage() / (std::string("root-locator-backup-") + kGeneration);
  }

  QTemporaryDir temp;
  fs::path outputBase;
  fs::path gameDirectory;
  fs::path source;
  fs::path target;
  VfsIndexPublicationResult publication;
};

TEST_F(DeploymentFixture, RestoresPriorRegularLocator) {
  writeFile(target, "user locator bytes\n");
  std::error_code error;
  fs::permissions(target,
                  fs::perms::owner_read | fs::perms::owner_write |
                      fs::perms::group_read,
                  fs::perm_options::replace, error);
  ASSERT_FALSE(error) << error.message();
  const auto originalPermissions = fs::status(target).permissions();
  ASSERT_TRUE(
      VfsRootLocatorDeployment::deploy(outputBase, gameDirectory, publication))
      << publication.error;
  EXPECT_EQ(readFile(target), "generated locator contents\n");
  ASSERT_TRUE(fs::is_regular_file(backup()));
  ASSERT_TRUE(fs::is_regular_file(manifest()));

  EXPECT_TRUE(VfsRootLocatorDeployment::clear(outputBase, gameDirectory));
  EXPECT_EQ(readFile(target), "user locator bytes\n");
  EXPECT_EQ(fs::status(target).permissions(), originalPermissions);
  EXPECT_FALSE(fs::exists(manifest()));
  EXPECT_FALSE(fs::exists(backup()));
}

TEST_F(DeploymentFixture, RejectsDanglingAndDirectorySymlinkTargets) {
  const fs::path dangling = gameDirectory / "dangling-target";
  std::error_code error;
  fs::create_symlink("missing-target", target, error);
  ASSERT_FALSE(error) << error.message();
  const fs::path oldTarget = fs::read_symlink(target);
  EXPECT_FALSE(
      VfsRootLocatorDeployment::deploy(outputBase, gameDirectory, publication));
  EXPECT_TRUE(fs::is_symlink(target));
  EXPECT_EQ(fs::read_symlink(target), oldTarget);
  EXPECT_FALSE(fs::exists(storage()));

  fs::remove(target);
  fs::create_directories(dangling);
  fs::create_directory_symlink(dangling, target, error);
  ASSERT_FALSE(error) << error.message();
  const fs::path directoryTarget = fs::read_symlink(target);
  EXPECT_FALSE(
      VfsRootLocatorDeployment::deploy(outputBase, gameDirectory, publication));
  EXPECT_TRUE(fs::is_symlink(target));
  EXPECT_EQ(fs::read_symlink(target), directoryTarget);
  EXPECT_FALSE(fs::exists(storage()));
}

TEST_F(DeploymentFixture, RejectsFifoWithoutCreatingRecoveryArtifacts) {
  ASSERT_EQ(::mkfifo(target.c_str(), 0600), 0);
  EXPECT_FALSE(
      VfsRootLocatorDeployment::deploy(outputBase, gameDirectory, publication));
  struct stat status{};
  ASSERT_EQ(::lstat(target.c_str(), &status), 0);
  EXPECT_TRUE(S_ISFIFO(status.st_mode));
  EXPECT_FALSE(fs::exists(storage()));
}

TEST_F(DeploymentFixture, ClearLeavesUserReplacementSymlinkUntouched) {
  writeFile(target, "user locator bytes\n");
  ASSERT_TRUE(
      VfsRootLocatorDeployment::deploy(outputBase, gameDirectory, publication))
      << publication.error;
  ASSERT_TRUE(fs::remove(target));
  const fs::path protectedTarget = temp.path().toStdString() + "/protected.txt";
  writeFile(protectedTarget, "must remain untouched\n");
  std::error_code error;
  fs::create_symlink(protectedTarget, target, error);
  ASSERT_FALSE(error) << error.message();

  EXPECT_FALSE(VfsRootLocatorDeployment::clear(outputBase, gameDirectory));
  EXPECT_TRUE(fs::is_symlink(target));
  EXPECT_EQ(readFile(protectedTarget), "must remain untouched\n");
  EXPECT_TRUE(fs::exists(manifest()));
  EXPECT_TRUE(fs::exists(backup()));
}

TEST_F(DeploymentFixture, ClearLeavesFifoReplacementUntouched) {
  ASSERT_TRUE(
      VfsRootLocatorDeployment::deploy(outputBase, gameDirectory, publication))
      << publication.error;
  ASSERT_TRUE(fs::remove(target));
  ASSERT_EQ(::mkfifo(target.c_str(), 0600), 0);

  EXPECT_FALSE(VfsRootLocatorDeployment::clear(outputBase, gameDirectory));
  struct stat status{};
  ASSERT_EQ(::lstat(target.c_str(), &status), 0);
  EXPECT_TRUE(S_ISFIFO(status.st_mode));
  EXPECT_TRUE(fs::exists(manifest()));
}

TEST_F(DeploymentFixture, MissingPriorBackupDoesNotDeleteGeneratedTarget) {
  writeFile(target, "user locator bytes\n");
  ASSERT_TRUE(
      VfsRootLocatorDeployment::deploy(outputBase, gameDirectory, publication))
      << publication.error;
  ASSERT_TRUE(fs::remove(backup()));

  EXPECT_FALSE(VfsRootLocatorDeployment::clear(outputBase, gameDirectory));
  EXPECT_EQ(readFile(target), "generated locator contents\n");
  EXPECT_TRUE(fs::exists(manifest()));
}

TEST_F(DeploymentFixture, SameLocationPortableLocatorIsNeverRemoved) {
  fs::remove(source);
  source = gameDirectory / kVfsIndexLocatorName;
  writeFile(source, "persistent portable locator\n");
  publication.locator_path = source;
  EXPECT_TRUE(VfsRootLocatorDeployment::deploy(gameDirectory, gameDirectory,
                                               publication));
  EXPECT_EQ(publication.root_locator_path, source);
  EXPECT_TRUE(VfsRootLocatorDeployment::clear(gameDirectory, gameDirectory));
  EXPECT_EQ(readFile(source), "persistent portable locator\n");
  EXPECT_FALSE(
      fs::exists(gameDirectory / ".vfs-indexer" / "root-deployment.json"));
}
} // namespace
