#ifndef LOOTMANAGER_TEST_H
#define LOOTMANAGER_TEST_H

#include <QByteArray>
#include <QString>

#include <atomic>
#include <functional>

// Test-only dependency seam for exercising the managed install transaction
// without network access or a host 7z executable. Compile the test target with
// FLUORINE_LOOTMANAGER_TESTING so this interface is absent from the app build.
namespace LootManagerTesting
{

struct Release
{
  QString version;
  QString assetName;
  QString assetUrl;
  QByteArray sha256Hex;
};

struct Dependencies
{
  std::function<QByteArray(const QString&, const std::atomic_bool*,
                           const std::function<void(float)>&,
                           const QString&)> httpGet;
  std::function<bool(const QString&, const QString&)> extractArchive;
};

bool isInstalledAt(const QString& installDir, const QString& requiredVersion);
QString downloadAt(const QString& dataDir, const Release& release,
                  const Dependencies& dependencies,
                  const std::function<void(float)>& progressCb = nullptr,
                  const std::function<void(const QString&)>& statusCb = nullptr,
                  const std::atomic_bool* cancelFlag = nullptr);
Release pinnedRelease();

}  // namespace LootManagerTesting

#endif  // LOOTMANAGER_TEST_H
