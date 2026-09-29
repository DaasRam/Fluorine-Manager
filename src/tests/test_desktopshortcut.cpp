#include "envshortcut.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>

namespace
{
QByteArray readFile(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

void writeFile(const QString& path, const QByteArray& contents)
{
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  ASSERT_EQ(file.write(contents), contents.size());
}

class DesktopShortcutTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    ASSERT_TRUE(m_temporary.isValid());
    // Even a regression containing two parent components stays in this
    // temporary fixture. The real desktop and application menu are never used.
    m_dataHome = m_temporary.filePath("sandbox/data");
    ASSERT_TRUE(QDir().mkpath(m_dataHome));
    qputenv("XDG_DATA_HOME", m_dataHome.toUtf8());
    ASSERT_EQ(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation),
              m_dataHome);
  }

  void TearDown() override
  {
    if (m_hadDataHome) {
      qputenv("XDG_DATA_HOME", m_originalDataHome);
    } else {
      qunsetenv("XDG_DATA_HOME");
    }
  }

  env::Shortcut shortcut(const QString& name = QStringLiteral("Example"))
  {
    env::Shortcut result;
    result.name(name).target(QStringLiteral("/usr/bin/printf"))
        .arguments(QStringLiteral("\"%s\" \"literal value\""));
    return result;
  }

  QStringList files(const QString& pattern) const
  {
    return QDir(m_dataHome + "/applications")
        .entryList({pattern}, QDir::Files | QDir::Hidden | QDir::System);
  }

  QString soleFile(const QString& pattern) const
  {
    const QStringList matches = files(pattern);
    EXPECT_EQ(matches.size(), 1);
    return matches.size() == 1 ? m_dataHome + "/applications/" + matches.front()
                               : QString();
  }

  static constexpr auto Menu = env::Shortcut::ApplicationMenu;
  QTemporaryDir m_temporary;
  QString m_dataHome;
  const bool m_hadDataHome = qEnvironmentVariableIsSet("XDG_DATA_HOME");
  const QByteArray m_originalDataHome = qgetenv("XDG_DATA_HOME");
};
}  // namespace

TEST_F(DesktopShortcutTest, PublishesAndRemovesOnlyItsOwnPair)
{
  auto item = shortcut();
  ASSERT_TRUE(item.add(Menu));
  EXPECT_TRUE(item.exists(Menu));
  EXPECT_EQ(files("*.desktop").size(), 1);
  EXPECT_EQ(files("*.sh").size(), 1);
  ASSERT_TRUE(item.toggle(Menu));
  EXPECT_FALSE(item.exists(Menu));
  EXPECT_TRUE(files("*.desktop").isEmpty());
  EXPECT_TRUE(files("*.sh").isEmpty());
}

TEST_F(DesktopShortcutTest, DistinguishesNamesThatSanitizeToTheSameLeaf)
{
  auto first = shortcut(QStringLiteral("a/b"));
  auto second = shortcut(QStringLiteral("ab"));
  ASSERT_TRUE(first.add(Menu));
  ASSERT_TRUE(second.add(Menu));
  EXPECT_EQ(files("*.desktop").size(), 2);
  ASSERT_TRUE(first.remove(Menu));
  EXPECT_TRUE(second.exists(Menu));
}

TEST_F(DesktopShortcutTest, ParentComponentsStayInsideTheMenuDirectory)
{
  auto item = shortcut(QStringLiteral("../../escaped"));
  ASSERT_TRUE(item.add(Menu));
  EXPECT_EQ(files("*.desktop").size(), 1);
  EXPECT_EQ(files("*.sh").size(), 1);
  EXPECT_FALSE(QFileInfo::exists(m_temporary.filePath("sandbox/escaped.desktop")));
  EXPECT_FALSE(QFileInfo::exists(m_temporary.filePath("sandbox/escaped.sh")));
}

TEST_F(DesktopShortcutTest, RefusesToReplaceOrRemoveAnUnmarkedDesktopFile)
{
  auto item = shortcut();
  ASSERT_TRUE(item.add(Menu));
  const QString desktop = soleFile("*.desktop");
  const QString script = soleFile("*.sh");
  const QByteArray scriptBefore = readFile(script);
  const QByteArray foreign("[Desktop Entry]\nName=Other app\nExec=/bin/true\n");
  writeFile(desktop, foreign);

  EXPECT_FALSE(item.exists(Menu));
  EXPECT_FALSE(item.add(Menu));
  EXPECT_FALSE(item.remove(Menu));
  EXPECT_FALSE(item.toggle(Menu));
  EXPECT_EQ(readFile(desktop), foreign);
  EXPECT_EQ(readFile(script), scriptBefore);
}

TEST_F(DesktopShortcutTest, PreservesAnUnmarkedScriptWhenRemovingItsOwnShortcut)
{
  auto item = shortcut();
  ASSERT_TRUE(item.add(Menu));
  const QString desktop = soleFile("*.desktop");
  const QString script = soleFile("*.sh");
  const QByteArray desktopBefore = readFile(desktop);
  const QByteArray foreign("#!/bin/sh\n# unrelated user script\n");
  writeFile(script, foreign);

  EXPECT_FALSE(item.add(Menu));
  EXPECT_EQ(readFile(desktop), desktopBefore);
  EXPECT_EQ(readFile(script), foreign);
  EXPECT_TRUE(item.remove(Menu));
  EXPECT_FALSE(item.toggle(Menu));
  EXPECT_FALSE(QFileInfo::exists(desktop));
  EXPECT_EQ(readFile(script), foreign);
}

TEST_F(DesktopShortcutTest, RefusesSymlinkLeavesWithoutTouchingTheirTargets)
{
  auto item = shortcut();
  ASSERT_TRUE(item.add(Menu));
  const QString desktop = soleFile("*.desktop");
  const QString script = soleFile("*.sh");
  const QByteArray scriptBefore = readFile(script);
  const QString external = m_temporary.filePath("unrelated.desktop");
  const QByteArray foreign("[Desktop Entry]\nName=Unrelated\n");
  writeFile(external, foreign);
  ASSERT_TRUE(QFile::remove(desktop));
  ASSERT_TRUE(QFile::link(external, desktop));

  EXPECT_FALSE(item.add(Menu));
  EXPECT_FALSE(item.remove(Menu));
  EXPECT_FALSE(item.toggle(Menu));
  EXPECT_TRUE(QFileInfo(desktop).isSymLink());
  EXPECT_EQ(readFile(external), foreign);
  EXPECT_EQ(readFile(script), scriptBefore);
}

TEST_F(DesktopShortcutTest, GioLaunchesFromADataPathContainingReservedCharacters)
{
  const QString gio = QStandardPaths::findExecutable(QStringLiteral("gio"));
  if (gio.isEmpty()) {
    GTEST_SKIP() << "gio is unavailable for Desktop Entry launch validation";
  }

  m_dataHome = m_temporary.filePath("data with \"quotes\" \\ $cash `literal` %u");
  ASSERT_TRUE(QDir().mkpath(m_dataHome));
  qputenv("XDG_DATA_HOME", m_dataHome.toUtf8());
  const QString capture = m_temporary.filePath("captured-argument");
  const QString target = m_temporary.filePath("capture argument.sh");
  writeFile(target, "#!/bin/sh\nprintf '%s' \"$1\" > \"$FLUORINE_TEST_SHORTCUT_CAPTURE\"\n");
  ASSERT_TRUE(QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
  auto item = shortcut();
  item.target(target).arguments(QStringLiteral("\"expected value\""));
  ASSERT_TRUE(item.add(Menu));

  QProcess process;
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("FLUORINE_TEST_SHORTCUT_CAPTURE"), capture);
  process.setProcessEnvironment(environment);
  process.start(gio, {QStringLiteral("launch"), soleFile("*.desktop")});
  ASSERT_TRUE(process.waitForFinished(10000));
  ASSERT_EQ(process.exitCode(), 0) << process.readAllStandardError().constData();

  // GIO starts the application asynchronously. Wait only for this fixture's
  // output, without involving a desktop session or launching a real game.
  QElapsedTimer deadline;
  deadline.start();
  while (readFile(capture) != QByteArray("expected value") && deadline.elapsed() < 3000) {
    QThread::msleep(10);
  }
  EXPECT_EQ(readFile(capture), QByteArray("expected value"));
}

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QApplication application(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
