#include "moddirectorydiscovery.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

TEST(ModDirectoryDiscovery, IncludesLegitimateLeadingDotModNames)
{
  QTemporaryDir instance;
  ASSERT_TRUE(instance.isValid());

  QDir root(instance.path());
  ASSERT_TRUE(root.mkpath("mods/.45 Auto Pistol (Colt M1911)"));
  ASSERT_TRUE(root.mkpath("mods/Ordinary Mod"));
  // These workspace internals are siblings of the configured mods path.
  ASSERT_TRUE(root.mkpath("applicationcache"));
  ASSERT_TRUE(root.mkpath("staging"));

  const QStringList discovered =
      ModDirectoryDiscovery::directModDirectories(root.filePath("mods"));

  EXPECT_TRUE(discovered.contains(".45 Auto Pistol (Colt M1911)"));
  EXPECT_TRUE(discovered.contains("Ordinary Mod"));
  EXPECT_FALSE(discovered.contains("applicationcache"));
  EXPECT_FALSE(discovered.contains("staging"));
  EXPECT_FALSE(discovered.contains("."));
  EXPECT_FALSE(discovered.contains(".."));
}
