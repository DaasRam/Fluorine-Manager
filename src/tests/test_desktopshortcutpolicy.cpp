#include "desktopshortcutpolicy.h"
#include "desktopportalpolicy.h"

#include <gtest/gtest.h>

#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>

using namespace env::desktopshortcut;

TEST(DesktopPortalPolicy, ExplicitThemeAndMissingPluginArePreserved)
{
  EXPECT_FALSE(env::desktopportal::shouldSelectBundledPortalTheme(
      QByteArrayLiteral("gtk3"), true));
  EXPECT_FALSE(env::desktopportal::shouldSelectBundledPortalTheme({}, false));
  EXPECT_TRUE(env::desktopportal::shouldSelectBundledPortalTheme({}, true));
}

TEST(DesktopShortcutPolicy, FilenameIsSafeAndIdentityScoped)
{
  const QString first =
      filenameStem(QStringLiteral("../A/B"), QStringLiteral("../Game:Tool"),
                   QStringLiteral("instance-one"));
  const QString second =
      filenameStem(QStringLiteral("../A/B"), QStringLiteral("../Game:Tool"),
                   QStringLiteral("instance-two"));

  EXPECT_NE(first, second);
  EXPECT_FALSE(first.contains('/'));
  EXPECT_FALSE(first.contains('\\'));
  EXPECT_FALSE(first.contains(':'));
  EXPECT_LT(first.size(), 100);
  EXPECT_EQ(first, filenameStem(QStringLiteral("../A/B"),
                                QStringLiteral("../Game:Tool"),
                                QStringLiteral("instance-one")));
}

TEST(DesktopShortcutPolicy, OwnershipRequiresExactMarkerInExpectedFileSection)
{
  const QString owner = ownerId(QStringLiteral("my instance\nmy program"));
  const QByteArray desktop =
      QByteArrayLiteral("[Desktop Entry]\nName=Fluorine\nX-Fluorine-Shortcut-Id=") +
      owner.toLatin1() + QByteArrayLiteral("\n");
  EXPECT_TRUE(desktopFileHasOwner(desktop, owner));
  EXPECT_FALSE(desktopFileHasOwner(desktop, ownerId(QStringLiteral("other"))));
  EXPECT_FALSE(desktopFileHasOwner(
      QByteArrayLiteral("[Desktop Entry]\nComment=X-Fluorine-Shortcut-Id=") +
          owner.toLatin1() + QByteArrayLiteral("\n"),
      owner));
  EXPECT_FALSE(desktopFileHasOwner(
      QByteArrayLiteral("[Other Group]\nX-Fluorine-Shortcut-Id=") +
          owner.toLatin1() + QByteArrayLiteral("\n"),
      owner));

  EXPECT_TRUE(scriptFileHasOwner(
      QByteArrayLiteral("#!/usr/bin/env bash\n# fluorine-shortcut-owner=") +
          owner.toLatin1() + QByteArrayLiteral("\n"),
      owner));
  EXPECT_FALSE(scriptFileHasOwner(QByteArrayLiteral("# ordinary user script\n"),
                                 owner));
}

TEST(DesktopShortcutPolicy, QuotedShortcutUriSurvivesArgumentParsing)
{
  const QString uri =
      QStringLiteral(R"(moshortcut://instance:My "quoted" app \path)");
  const QString commandLine = commandLineToken(uri);
  EXPECT_EQ(QProcess::splitCommand(commandLine), QStringList{uri});
}

TEST(DesktopShortcutPolicy, ShellExpansionsInArgumentsArePassedLiterally)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString marker = temp.filePath(QStringLiteral("command-was-evaluated"));
  const QString payload = QStringLiteral("$(touch %1); `id` 'quoted'").arg(marker);
  const QString arguments = commandLineToken(QStringLiteral("[%s]")) +
                            QLatin1Char(' ') + commandLineToken(payload);
  const QString command = shellCommand(QStringLiteral("/usr/bin/printf"), arguments);

  QProcess process;
  process.start(QStringLiteral("/bin/bash"), {QStringLiteral("-c"), command});
  ASSERT_TRUE(process.waitForFinished());
  ASSERT_EQ(process.exitStatus(), QProcess::NormalExit);
  ASSERT_EQ(process.exitCode(), 0);
  EXPECT_EQ(process.readAllStandardOutput(), QByteArray("[") + payload.toUtf8() + "]");
  EXPECT_FALSE(QFileInfo::exists(marker));
}

TEST(DesktopShortcutPolicy, DesktopExecEscapingKeepsOneFieldCodeFreeArgument)
{
  const QString path = QStringLiteral("/tmp/a \"b\" $c `d` %e\\f");
  const QString quoted = desktopExecArgument(path);
  const QString encoded = desktopExecEntryValue(path);
  const QString command = desktopExecCommandLine(
      {QStringLiteral("/usr/bin/env"), QStringLiteral("bash"),
       QStringLiteral("--"), path});
  EXPECT_FALSE(quoted.isEmpty());
  EXPECT_TRUE(quoted.startsWith('"'));
  EXPECT_TRUE(quoted.endsWith('"'));
  EXPECT_NE(quoted.indexOf(QStringLiteral("%%e")), -1);
  EXPECT_NE(quoted.indexOf(QStringLiteral("\\$c")), -1);
  EXPECT_NE(quoted.indexOf(QStringLiteral("\\`d\\`")), -1);
  EXPECT_NE(encoded.indexOf(QStringLiteral("\\\\")), -1);
  EXPECT_EQ(encoded, desktopEntryValue(quoted));
  EXPECT_TRUE(command.startsWith(
      QStringLiteral("\"/usr/bin/env\" \"bash\" \"--\" ")));
  EXPECT_NE(command.indexOf(QStringLiteral("%%e")), -1);
  EXPECT_TRUE(desktopExecCommandLine({QStringLiteral("/usr/bin/env"),
                                      QStringLiteral("bad\npath")})
                  .isEmpty());
  EXPECT_TRUE(desktopExecEntryValue(QStringLiteral("bad\npath")).isEmpty());
}
