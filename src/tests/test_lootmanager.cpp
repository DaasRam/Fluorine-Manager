#include "lootmanager_test.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

#include <gtest/gtest.h>
#include <uibase/log.h>

namespace
{

QString TestDataDir;

bool writeFile(const QString& path, const QByteArray& contents)
{
  if (!QDir().mkpath(QFileInfo(path).dir().absolutePath()))
    return false;
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

QByteArray readFile(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return {};
  return file.readAll();
}

QString markerPath(const QString& installDir)
{
  return QDir(installDir).filePath(".fluorine-loot-version");
}

LootManagerTesting::Release fixtureRelease(const QByteArray& archiveBytes)
{
  return {QStringLiteral("test-1.2.3"), QStringLiteral("loot-test-win64.7z"),
          QStringLiteral("https://downloads.example.test/loot-test-win64.7z"),
          QCryptographicHash::hash(archiveBytes, QCryptographicHash::Sha256)
              .toHex()};
}

struct ExistingManagedTree
{
  QString dataDir;
  QString installDir;

  explicit ExistingManagedTree(const QString& root)
      : dataDir(root), installDir(QDir(root).filePath("tools/loot"))
  {
    EXPECT_TRUE(writeFile(QDir(installDir).filePath("LOOT.exe"),
                          QByteArrayLiteral("old 0.29.2 executable")));
    EXPECT_TRUE(writeFile(markerPath(installDir), QByteArrayLiteral("0.29.2\n")));
    EXPECT_TRUE(writeFile(QDir(root).filePath("tools/my-custom-tool/keep.txt"),
                          QByteArrayLiteral("keep tool")));
    EXPECT_TRUE(writeFile(QDir(root).filePath("profiles/default/plugins.txt"),
                          QByteArrayLiteral("keep profile")));
  }
};

LootManagerTesting::Dependencies dependenciesFor(
    const LootManagerTesting::Release& release,
    const QByteArray& archiveBytes)
{
  LootManagerTesting::Dependencies dependencies;
  dependencies.httpGet = [release, archiveBytes](
      const QString& url, const std::atomic_bool*, const std::function<void(float)>&,
      const QString& destination) {
    EXPECT_EQ(url, release.assetUrl);
    EXPECT_TRUE(writeFile(destination, archiveBytes));
    return QByteArray{};
  };
  dependencies.extractArchive = [](const QString&, const QString& destination) {
    const QString appDir = QDir(destination).filePath("LOOT");
    return writeFile(QDir(appDir).filePath("LOOT.exe"),
                     QByteArrayLiteral("new compatible executable")) &&
           writeFile(QDir(appDir).filePath("icuuc.dll"),
                     QByteArrayLiteral("bundled ICU")) &&
           writeFile(QDir(appDir).filePath("resources/data.txt"),
                     QByteArrayLiteral("release files"));
  };
  return dependencies;
}

}  // namespace

// lootmanager.cpp normally gets this from fluorinepaths.cpp. Keeping the test
// data root local makes these tests independent of the user's XDG directories.
QString fluorineDataDir()
{
  return TestDataDir;
}

TEST(LootManager, PinnedReleaseIsWineCompatibleAndImmutable)
{
  const auto release = LootManagerTesting::pinnedRelease();
  EXPECT_EQ(release.version, QStringLiteral("0.29.1"));
  EXPECT_EQ(release.assetName, QStringLiteral("loot_0.29.1-win64.7z"));
  EXPECT_EQ(release.assetUrl,
            QStringLiteral("https://github.com/loot/loot/releases/download/"
                           "0.29.1/loot_0.29.1-win64.7z"));
  EXPECT_EQ(release.sha256Hex,
            QByteArrayLiteral(
                "699dbb1157e26cbd8b8758632b8370bbb372759c9a00ffd9a4300a05f3409837"));
}

TEST(LootManager, MarkerlessAnd0292TreesAreScheduledForRepair)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  const QString install = QDir(temporary.path()).filePath("tools/loot");
  ASSERT_TRUE(writeFile(QDir(install).filePath("LOOT.exe"),
                        QByteArrayLiteral("old executable")));
  EXPECT_FALSE(LootManagerTesting::isInstalledAt(install,
                                                QStringLiteral("0.29.1")));

  ASSERT_TRUE(writeFile(markerPath(install), QByteArrayLiteral("0.29.2\n")));
  EXPECT_FALSE(LootManagerTesting::isInstalledAt(install,
                                                QStringLiteral("0.29.1")));

  ASSERT_TRUE(writeFile(markerPath(install), QByteArrayLiteral("0.29.1\n")));
  EXPECT_TRUE(LootManagerTesting::isInstalledAt(install,
                                               QStringLiteral("0.29.1")));
}

TEST(LootManager, UpdateReplacesOnlyManagedLootTree)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  ExistingManagedTree oldTree(temporary.path());
  const QByteArray archiveBytes = QByteArrayLiteral("verified fixture archive");
  const auto release = fixtureRelease(archiveBytes);
  const auto dependencies = dependenciesFor(release, archiveBytes);

  const QString error = LootManagerTesting::downloadAt(
      oldTree.dataDir, release, dependencies);

  EXPECT_TRUE(error.isEmpty()) << error.toStdString();
  EXPECT_EQ(readFile(QDir(oldTree.installDir).filePath("LOOT.exe")),
            QByteArrayLiteral("new compatible executable"));
  EXPECT_EQ(readFile(QDir(oldTree.installDir).filePath("icuuc.dll")),
            QByteArrayLiteral("bundled ICU"));
  EXPECT_EQ(readFile(markerPath(oldTree.installDir)), QByteArrayLiteral("test-1.2.3\n"));
  EXPECT_TRUE(LootManagerTesting::isInstalledAt(oldTree.installDir,
                                                QStringLiteral("test-1.2.3")));
  EXPECT_EQ(readFile(QDir(oldTree.dataDir)
                         .filePath("tools/my-custom-tool/keep.txt")),
            QByteArrayLiteral("keep tool"));
  EXPECT_EQ(readFile(QDir(oldTree.dataDir)
                         .filePath("profiles/default/plugins.txt")),
            QByteArrayLiteral("keep profile"));
}

TEST(LootManager, ChecksumFailurePreservesPreviousInstall)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  ExistingManagedTree oldTree(temporary.path());
  const QByteArray archiveBytes = QByteArrayLiteral("corrupt fixture archive");
  auto release = fixtureRelease(QByteArrayLiteral("different expected bytes"));
  auto dependencies = dependenciesFor(release, archiveBytes);
  int extractionCalls = 0;
  dependencies.extractArchive = [&extractionCalls](const QString&,
                                                    const QString&) {
    ++extractionCalls;
    return false;
  };

  const QString error = LootManagerTesting::downloadAt(
      oldTree.dataDir, release, dependencies);

  EXPECT_NE(error.indexOf(QStringLiteral("SHA-256")), -1);
  EXPECT_EQ(extractionCalls, 0);
  EXPECT_EQ(readFile(QDir(oldTree.installDir).filePath("LOOT.exe")),
            QByteArrayLiteral("old 0.29.2 executable"));
  EXPECT_EQ(readFile(markerPath(oldTree.installDir)), QByteArrayLiteral("0.29.2\n"));
}

TEST(LootManager, ExtractionFailurePreservesPreviousInstall)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  ExistingManagedTree oldTree(temporary.path());
  const QByteArray archiveBytes = QByteArrayLiteral("verified fixture archive");
  const auto release = fixtureRelease(archiveBytes);
  auto dependencies = dependenciesFor(release, archiveBytes);
  dependencies.extractArchive = [](const QString&, const QString&) {
    return false;
  };

  const QString error = LootManagerTesting::downloadAt(
      oldTree.dataDir, release, dependencies);

  EXPECT_FALSE(error.isEmpty());
  EXPECT_EQ(readFile(QDir(oldTree.installDir).filePath("LOOT.exe")),
            QByteArrayLiteral("old 0.29.2 executable"));
  EXPECT_EQ(readFile(markerPath(oldTree.installDir)), QByteArrayLiteral("0.29.2\n"));
}

TEST(LootManager, CancellationPreservesPreviousInstall)
{
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  ExistingManagedTree oldTree(temporary.path());
  const QByteArray archiveBytes = QByteArrayLiteral("fixture archive");
  const auto release = fixtureRelease(archiveBytes);
  auto dependencies = dependenciesFor(release, archiveBytes);
  std::atomic_bool cancelFlag{false};
  std::promise<void> downloaderStarted;
  dependencies.httpGet = [&downloaderStarted](
      const QString&, const std::atomic_bool* cancellation,
      const std::function<void(float)>&,
      const QString&) {
    downloaderStarted.set_value();
    while (!cancellation->load(std::memory_order_relaxed))
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return QByteArray{};
  };
  int extractionCalls = 0;
  dependencies.extractArchive = [&extractionCalls](const QString&,
                                                    const QString&) {
    ++extractionCalls;
    return true;
  };

  auto started = downloaderStarted.get_future();
  auto download = std::async(std::launch::async, [&] {
    return LootManagerTesting::downloadAt(
        oldTree.dataDir, release, dependencies, nullptr, nullptr, &cancelFlag);
  });
  const auto startResult = started.wait_for(std::chrono::seconds(2));
  cancelFlag.store(true, std::memory_order_relaxed);
  ASSERT_EQ(startResult, std::future_status::ready)
      << "download worker did not reach the cancellable phase";
  ASSERT_EQ(download.wait_for(std::chrono::seconds(2)), std::future_status::ready)
      << "download did not observe cancellation promptly";
  const QString error = download.get();

  EXPECT_NE(error.indexOf(QStringLiteral("cancelled")), -1);
  EXPECT_EQ(extractionCalls, 0);
  EXPECT_EQ(readFile(QDir(oldTree.installDir).filePath("LOOT.exe")),
            QByteArrayLiteral("old 0.29.2 executable"));
}

int main(int argc, char** argv)
{
  QTemporaryDir bus;
  qputenv("DBUS_SESSION_BUS_ADDRESS",
          ("unix:path=" + bus.filePath("absent")).toUtf8());
  QCoreApplication app(argc, argv);
  MOBase::log::LoggerConfiguration configuration;
  configuration.name = "test_lootmanager";
  MOBase::log::createDefault(configuration);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
