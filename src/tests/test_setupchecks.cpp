#include "setupchecks.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace
{
const SetupCheckRow& rowFor(const QList<SetupCheckRow>& rows, SetupCheckItem item)
{
  for (const SetupCheckRow& row : rows) {
    if (row.item == item) return row;
  }
  ADD_FAILURE() << "Expected setup check row was not returned";
  return rows.first();
}

bool writeFile(const QString& path, const QByteArray& contents)
{
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

SetupCheckInputs nativeInputs(const QString& gameFolder, const QString& executable)
{
  SetupCheckInputs inputs;
  inputs.gameFolderPath = gameFolder;
  inputs.selectedExecutableName = QFileInfo(executable).fileName();
  inputs.selectedExecutablePath = executable;
  inputs.useProton = false;
  return inputs;
}
} // namespace

TEST(SetupCheckModel, MissingSelectionsAndFilesNeedAttention)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());

  SetupCheckInputs inputs;
  inputs.gameFolderPath = temp.filePath(QStringLiteral("missing-game"));
  inputs.selectedExecutableName = QStringLiteral("Game.x86_64");
  inputs.selectedExecutablePath = temp.filePath(QStringLiteral("missing-game/Game.x86_64"));
  inputs.useProton = false;

  const auto rows = SetupCheckModel::evaluate(inputs);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::GameFolder).state,
            SetupCheckState::NeedsAttention);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::Executable).state,
            SetupCheckState::NeedsAttention);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::Executable).action,
            SetupCheckAction::Executables);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::ProtonLauncher).state,
            SetupCheckState::NotRequired);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::WinePrefix).state,
            SetupCheckState::NotRequired);
}

TEST(SetupCheckModel, DisplayNameDoesNotStandInForMissingExecutablePath)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString game = temp.filePath(QStringLiteral("game"));
  ASSERT_TRUE(QDir().mkpath(game));
  ASSERT_TRUE(writeFile(QDir(game).filePath(QStringLiteral("Game")),
                        QByteArrayLiteral("native file")));

  SetupCheckInputs inputs;
  inputs.gameFolderPath = game;
  inputs.selectedExecutableName = QStringLiteral("Game");
  inputs.useProton = false;

  const auto rows = SetupCheckModel::evaluate(inputs);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::Executable).state,
            SetupCheckState::NeedsAttention);
  EXPECT_NE(rowFor(rows, SetupCheckItem::Executable).detail.indexOf(
                QStringLiteral("No executable file path")), -1);
}

TEST(SetupCheckModel, NativeExecutableChecksExecutePermissionWithoutRequiringProton)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString game = temp.filePath(QStringLiteral("game"));
  ASSERT_TRUE(QDir().mkpath(game));
  const QString executable = QDir(game).filePath(QStringLiteral("game.x86_64"));
  ASSERT_TRUE(writeFile(executable, QByteArrayLiteral("#!/bin/sh\n")));

  SetupCheckInputs inputs = nativeInputs(game, executable);
  inputs.effectiveBackendDescription = QStringLiteral("Native backend");

  QFile::setPermissions(executable, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                        QFileDevice::ExeOwner);
  auto rows = SetupCheckModel::evaluate(inputs);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::GameFolder).state, SetupCheckState::Passing);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::Executable).state, SetupCheckState::Passing);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::ProtonLauncher).state,
            SetupCheckState::NotRequired);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::SteamLinuxRuntime).state,
            SetupCheckState::NotRequired);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::VirtualFilesystem).state,
            SetupCheckState::NotRequired);

  QFile::setPermissions(executable, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  rows = SetupCheckModel::evaluate(inputs);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::Executable).state,
            SetupCheckState::NeedsAttention);
  EXPECT_NE(rowFor(rows, SetupCheckItem::Executable).detail.indexOf(
                QStringLiteral("execute permission")), -1);
}

TEST(SetupCheckModel, WindowsExecutableWithProtonChecksLauncherPrefixAndRuntime)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString game = temp.filePath(QStringLiteral("game"));
  ASSERT_TRUE(QDir().mkpath(game));
  const QString executable = QDir(game).filePath(QStringLiteral("Game.exe"));
  ASSERT_TRUE(writeFile(executable, QByteArrayLiteral("MZtest")));

  const QString protonDir = temp.filePath(QStringLiteral("compatibility"));
  ASSERT_TRUE(QDir().mkpath(protonDir));
  const QString protonScript = QDir(protonDir).filePath(QStringLiteral("proton"));
  ASSERT_TRUE(writeFile(protonScript, QByteArrayLiteral("#!/bin/sh\n")));
  QFile::setPermissions(protonScript, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                         QFileDevice::ExeOwner);

  const QString prefix = temp.filePath(QStringLiteral("prefix"));
  ASSERT_TRUE(QDir().mkpath(QDir(prefix).filePath(QStringLiteral("drive_c"))));
  ASSERT_TRUE(writeFile(QDir(prefix).filePath(QStringLiteral("system.reg")),
                        QByteArrayLiteral("registry placeholder\n")));
  ASSERT_TRUE(writeFile(QDir(prefix).filePath(QStringLiteral("user.reg")),
                        QByteArrayLiteral("registry placeholder\n")));

  SetupCheckInputs inputs;
  inputs.gameFolderPath = game;
  inputs.selectedExecutableName = QStringLiteral("Game.exe");
  inputs.selectedExecutablePath = executable;
  inputs.useProton = true;
  inputs.protonInstallationPath = protonDir;
  inputs.prefixPath = prefix;
  inputs.steamRuntimeAvailable = true;
  inputs.effectiveBackendDescription = QStringLiteral("FUSE backend");
  inputs.backendRequired = true;
  // The model does not equate a configured device path with runtime success.
  inputs.backendPrerequisitePath = QStringLiteral("/dev/fuse");

  auto rows = SetupCheckModel::evaluate(inputs);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::Executable).state, SetupCheckState::Passing);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::ProtonLauncher).state,
            SetupCheckState::Passing);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::WinePrefix).state, SetupCheckState::Passing);
  EXPECT_NE(rowFor(rows, SetupCheckItem::WinePrefix).detail.indexOf(
                QStringLiteral("not tested")), -1);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::SteamLinuxRuntime).state,
            SetupCheckState::Passing);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::VirtualFilesystem).state,
            SetupCheckState::Unchecked);
  EXPECT_NE(rowFor(rows, SetupCheckItem::VirtualFilesystem).detail.indexOf(
                QStringLiteral("not checked")), -1);
}

TEST(SetupCheckModel, ProtonSettingControlsRequirementsRegardlessOfFileHeader)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString game = temp.filePath(QStringLiteral("game"));
  ASSERT_TRUE(QDir().mkpath(game));
  const QString executable = QDir(game).filePath(QStringLiteral("launcher"));
  ASSERT_TRUE(writeFile(executable, QByteArrayLiteral("not an MZ file")));
  QFile::setPermissions(executable, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

  SetupCheckInputs inputs = nativeInputs(game, executable);
  inputs.useProton = true;
  inputs.steamRuntimeAvailable = false;

  auto rows = SetupCheckModel::evaluate(inputs);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::Executable).state, SetupCheckState::Passing);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::ProtonLauncher).state,
            SetupCheckState::NeedsAttention);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::WinePrefix).state,
            SetupCheckState::NeedsAttention);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::SteamLinuxRuntime).state,
            SetupCheckState::NeedsAttention);

  inputs.useProton.reset();
  rows = SetupCheckModel::evaluate(inputs);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::Executable).state, SetupCheckState::Passing);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::ProtonLauncher).state,
            SetupCheckState::Unchecked);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::WinePrefix).state,
            SetupCheckState::Unchecked);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::SteamLinuxRuntime).state,
            SetupCheckState::Unchecked);
}

TEST(SetupCheckModel, NativeExecutableCanHaveExeNameAndBackendFailureRoutesToCompatibility)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString game = temp.filePath(QStringLiteral("game"));
  ASSERT_TRUE(QDir().mkpath(game));
  const QString executable = QDir(game).filePath(QStringLiteral("Native.exe"));
  ASSERT_TRUE(writeFile(executable, QByteArrayLiteral("native executable")));
  QFile::setPermissions(executable, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                        QFileDevice::ExeOwner);

  SetupCheckInputs inputs = nativeInputs(game, executable);
  inputs.effectiveBackendDescription = QStringLiteral("FUSE backend");
  inputs.backendRequired = true;
  inputs.backendPrerequisiteAvailable = false;
  inputs.backendPrerequisitePath = QStringLiteral("/dev/fuse");

  auto rows = SetupCheckModel::evaluate(inputs);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::Executable).state, SetupCheckState::Passing);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::ProtonLauncher).state,
            SetupCheckState::NotRequired);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::VirtualFilesystem).state,
            SetupCheckState::NeedsAttention);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::VirtualFilesystem).action,
            SetupCheckAction::Compatibility);

  ASSERT_TRUE(writeFile(executable, QByteArrayLiteral("MZheader")));
  rows = SetupCheckModel::evaluate(inputs);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::Executable).state,
            SetupCheckState::NeedsAttention);
  EXPECT_NE(rowFor(rows, SetupCheckItem::Executable).detail.indexOf(
                QStringLiteral("Windows executable header")), -1);
}

TEST(SetupCheckModel, PrefixRequiresOnlyDriveCAndRegistryMarkers)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const QString game = temp.filePath(QStringLiteral("game"));
  ASSERT_TRUE(QDir().mkpath(game));
  const QString executable = QDir(game).filePath(QStringLiteral("Game.exe"));
  ASSERT_TRUE(writeFile(executable, QByteArrayLiteral("MZtest")));
  const QString proton = temp.filePath(QStringLiteral("proton"));
  ASSERT_TRUE(writeFile(proton, QByteArrayLiteral("#!/bin/sh\n")));
  QFile::setPermissions(proton, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
  const QString prefix = temp.filePath(QStringLiteral("prefix"));
  ASSERT_TRUE(QDir().mkpath(QDir(prefix).filePath(QStringLiteral("drive_c"))));
  ASSERT_TRUE(writeFile(QDir(prefix).filePath(QStringLiteral("system.reg")),
                        QByteArrayLiteral("registry placeholder\n")));

  SetupCheckInputs inputs;
  inputs.gameFolderPath = game;
  inputs.selectedExecutableName = QStringLiteral("Game.exe");
  inputs.selectedExecutablePath = executable;
  inputs.useProton = true;
  inputs.protonInstallationPath = proton;
  inputs.prefixPath = prefix;
  inputs.steamRuntimeAvailable = false;

  const auto rows = SetupCheckModel::evaluate(inputs);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::WinePrefix).state,
            SetupCheckState::NeedsAttention);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::WinePrefix).action,
            SetupCheckAction::Compatibility);
  EXPECT_NE(rowFor(rows, SetupCheckItem::WinePrefix).detail.indexOf(
                QStringLiteral("user.reg")), -1);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::SteamLinuxRuntime).state,
            SetupCheckState::NeedsAttention);
  EXPECT_EQ(rowFor(rows, SetupCheckItem::SteamLinuxRuntime).action,
            SetupCheckAction::Compatibility);
}

TEST(SetupChecksDialog, RendersTextStatesAndForwardsActionsAndRecheck)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  SetupCheckInputs inputs;
  inputs.gameFolderPath = temp.filePath(QStringLiteral("missing-game"));
  inputs.selectedExecutableName = QStringLiteral("Example <Program>");

  SetupChecksDialog dialog;
  dialog.setInputs(inputs);
  dialog.show();
  QApplication::processEvents();

  auto* summary = dialog.findChild<QLabel*>(QStringLiteral("setupChecksSummary"));
  ASSERT_NE(summary, nullptr);
  EXPECT_NE(summary->text().indexOf(QStringLiteral("Basic setup checks")), -1);
  EXPECT_EQ(summary->text().contains(QStringLiteral("ready"), Qt::CaseInsensitive),
            false);
  auto* program = dialog.findChild<QLabel*>(QStringLiteral("setupChecksProgram"));
  ASSERT_NE(program, nullptr);
  EXPECT_EQ(program->text(), QStringLiteral("Program: Example <Program>"));
  auto* gameState = dialog.findChild<QLabel*>(
      QStringLiteral("setupCheckState_gameFolder"));
  ASSERT_NE(gameState, nullptr);
  EXPECT_EQ(gameState->text(), QStringLiteral("Needs attention"));

  int requestedAction = -1;
  QObject::connect(&dialog, &SetupChecksDialog::actionRequested,
                   [&requestedAction](SetupCheckAction action) {
                     requestedAction = static_cast<int>(action);
                   });
  auto* action = dialog.findChild<QPushButton*>(
      QStringLiteral("setupCheckAction_gameFolder"));
  ASSERT_NE(action, nullptr);
  action->click();
  EXPECT_EQ(requestedAction, static_cast<int>(SetupCheckAction::Paths));

  int rechecks = 0;
  QObject::connect(&dialog, &SetupChecksDialog::recheckRequested,
                   [&rechecks] { ++rechecks; });
  auto* recheck = dialog.findChild<QPushButton*>(
      QStringLiteral("setupChecksRecheck"));
  ASSERT_NE(recheck, nullptr);
  recheck->click();
  EXPECT_EQ(rechecks, 1);
}

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
