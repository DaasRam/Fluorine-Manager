#include "fileconflictinspector.h"

#include <QApplication>
#include <QDir>
#include <QFrame>
#include <QFont>
#include <QLabel>
#include <QPixmap>
#include <QScrollArea>
#include <gtest/gtest.h>

#include <utility>

namespace
{
QLabel* label(FileConflictInspector& inspector, const QString& objectName)
{
  return inspector.findChild<QLabel*>(objectName);
}

FileConflictProviderSnapshot provider(QString modName,
                                      FileConflictSourceKind sourceKind,
                                      bool isCurrentMod, bool isWinner,
                                      QString archiveName = {},
                                      std::optional<int> modPriority = {},
                                      std::optional<int> archiveOrder = {})
{
  FileConflictProviderSnapshot result;
  result.modName = std::move(modName);
  result.sourceKind = sourceKind;
  result.archiveName = std::move(archiveName);
  result.modPriority = modPriority;
  result.archiveOrder = archiveOrder;
  result.isCurrentMod = isCurrentMod;
  result.isWinner = isWinner;
  return result;
}

FileConflictSnapshot mixedConflictSnapshot()
{
  FileConflictSnapshot snapshot;
  snapshot.state = FileConflictInspectorState::Conflict;
  snapshot.relativePath = QStringLiteral("textures/interface/long_example_banner.dds");
  snapshot.currentModName = QStringLiteral("Current Retexture Collection");
  snapshot.providers = {
      provider(QStringLiteral("Current Retexture Collection"),
               FileConflictSourceKind::Archive, true, false,
               QStringLiteral("Current Retexture Collection - Textures.bsa"), {}, 18),
      provider(QStringLiteral("Community Interface Improvements"),
               FileConflictSourceKind::Loose, false, true, {}, 42),
      provider(QStringLiteral("Current Retexture Collection"),
               FileConflictSourceKind::Archive, true, false,
               QStringLiteral("Current Retexture Collection - Patch.bsa"), {}, 25),
  };
  return snapshot;
}

bool renderReviewImages(const QString& outputDirectory, QApplication& app)
{
  if (!QDir().mkpath(outputDirectory)) {
    qCritical("Could not create render review output directory");
    return false;
  }

  FileConflictInspector inspector;
  QFont reviewFont = app.font();
  reviewFont.setPointSize(16);
  inspector.setFont(reviewFont);
  inspector.setSnapshot(mixedConflictSnapshot());
  inspector.show();

  for (const QSize size : {QSize(900, 650), QSize(600, 650)}) {
    inspector.resize(size);
    app.processEvents();
    const QString fileName = QStringLiteral("file-conflict-inspector-%1x%2.png")
                                 .arg(size.width())
                                 .arg(size.height());
    if (!inspector.grab().save(QDir(outputDirectory).filePath(fileName))) {
      qCritical("Could not save render review image");
      return false;
    }
  }
  return true;
}
} // namespace

TEST(FileConflictInspector, EmptyAndMultipleSelectionsHaveExplicitStates)
{
  FileConflictInspector inspector;
  FileConflictSnapshot snapshot;

  inspector.setSnapshot(snapshot);
  ASSERT_NE(label(inspector, QStringLiteral("fileConflictState")), nullptr);
  EXPECT_EQ(label(inspector, QStringLiteral("fileConflictState"))->text(),
            QStringLiteral("No file selected"));
  ASSERT_NE(inspector.findChild<QScrollArea*>(
                QStringLiteral("fileConflictProviderScrollArea")), nullptr);
  EXPECT_TRUE(inspector.findChild<QScrollArea*>(
                  QStringLiteral("fileConflictProviderScrollArea"))->isHidden());

  snapshot.state = FileConflictInspectorState::MultipleSelection;
  inspector.setSnapshot(snapshot);
  EXPECT_EQ(label(inspector, QStringLiteral("fileConflictState"))->text(),
            QStringLiteral("Select one file to inspect its conflicts"));
  EXPECT_TRUE(inspector.findChild<QScrollArea*>(
                  QStringLiteral("fileConflictProviderScrollArea"))->isHidden());
}

TEST(FileConflictInspector, ShowsSnapshotWinnerWhenCurrentModLoses)
{
  FileConflictInspector inspector;
  FileConflictSnapshot snapshot;
  snapshot.state = FileConflictInspectorState::Conflict;
  snapshot.relativePath = QStringLiteral("textures/<banner>.dds");
  snapshot.currentModName = QStringLiteral("<Current Mod>");
  snapshot.providers = {
      provider(QStringLiteral("<Current Mod>"), FileConflictSourceKind::Archive,
               true, false, QStringLiteral("<current>.bsa"), {}, 8),
      provider(QStringLiteral("Texture Fix"), FileConflictSourceKind::Loose,
               false, true, {}, 23),
      provider(QStringLiteral("<Current Mod>"), FileConflictSourceKind::Archive,
               true, false, QStringLiteral("<current-patch>.bsa"), {}, 11),
  };

  inspector.setSnapshot(snapshot);

  EXPECT_EQ(label(inspector, QStringLiteral("fileConflictState"))->text(),
            QStringLiteral("Conflict reported"));
  EXPECT_EQ(label(inspector, QStringLiteral("fileConflictPath"))->text(),
            QStringLiteral("File: textures/<banner>.dds"));
  EXPECT_EQ(label(inspector, QStringLiteral("fileConflictPath"))->textFormat(),
            Qt::PlainText);
  EXPECT_EQ(label(inspector, QStringLiteral("fileConflictCurrentMod"))->text(),
            QStringLiteral("Current mod: <Current Mod>"));
  EXPECT_EQ(label(inspector, QStringLiteral("fileConflictProviderMarkers_0"))->text(),
            QStringLiteral("Current mod · Does not currently win"));
  EXPECT_EQ(label(inspector, QStringLiteral("fileConflictProviderMarkers_1"))->text(),
            QStringLiteral("Current winner"));
  EXPECT_EQ(label(inspector, QStringLiteral("fileConflictProviderName_0"))->text(),
            QStringLiteral("<Current Mod>"));
  EXPECT_EQ(label(inspector, QStringLiteral("fileConflictProviderName_0"))->textFormat(),
            Qt::PlainText);
  EXPECT_EQ(label(inspector, QStringLiteral("fileConflictProviderSource_0"))->text(),
            QStringLiteral("Archive entry · <current>.bsa"));
  EXPECT_EQ(label(inspector, QStringLiteral("fileConflictProviderSource_2"))->text(),
            QStringLiteral("Archive entry · <current-patch>.bsa"));
  EXPECT_EQ(label(inspector, QStringLiteral("fileConflictProviderOrder_1"))->text(),
            QStringLiteral("Mod priority: 23"));

  const QString rule = label(inspector, QStringLiteral("fileConflictWinnerRule"))->text();
  EXPECT_NE(rule.indexOf(QStringLiteral("Loose files take precedence over archive contents")),
            -1);
  EXPECT_NE(rule.indexOf(QStringLiteral("Between two loose files, the higher mod priority wins")),
            -1);
  EXPECT_NE(rule.indexOf(QStringLiteral("archive order follows plugin load order")), -1);
  EXPECT_NE(rule.indexOf(QStringLiteral("Changing mod priority can change loose-file winners")),
            -1);
}

TEST(FileConflictInspector, ArchiveOnlyRuleUsesArchiveOrderAndKeepsArchiveIdentity)
{
  FileConflictInspector inspector;
  FileConflictSnapshot snapshot;
  snapshot.state = FileConflictInspectorState::Conflict;
  snapshot.relativePath = QStringLiteral("meshes/example.nif");
  snapshot.currentModName = QStringLiteral("Mesh Pack");
  snapshot.providers = {
      provider(QStringLiteral("Mesh Pack"), FileConflictSourceKind::Archive,
               true, true, QStringLiteral("Meshes.bsa"), {}, 15),
      provider(QStringLiteral("Alternate Meshes"), FileConflictSourceKind::Archive,
               false, false, QStringLiteral("Alternate.bsa"), {}, 12),
  };

  inspector.setSnapshot(snapshot);

  const QString rule = label(inspector, QStringLiteral("fileConflictWinnerRule"))->text();
  EXPECT_NE(rule.indexOf(QStringLiteral("Archive order follows plugin load order")), -1);
  EXPECT_NE(rule.indexOf(QStringLiteral("plugin load order")), -1);
  EXPECT_EQ(label(inspector, QStringLiteral("fileConflictProviderSource_0"))->text(),
            QStringLiteral("Archive entry · Meshes.bsa"));
  EXPECT_EQ(label(inspector, QStringLiteral("fileConflictProviderOrder_0"))->text(),
            QStringLiteral("Archive order: 15"));
}

TEST(FileConflictInspector, NoConflictStateComesFromSnapshotAndUnknownStaysUnknown)
{
  FileConflictInspector inspector;
  FileConflictSnapshot snapshot;
  snapshot.state = FileConflictInspectorState::NoConflict;
  snapshot.relativePath = QStringLiteral("interface/icon.png");
  snapshot.currentModName = QStringLiteral("Current Mod");
  // Two source records for the same mod still follow the parent-supplied state.
  snapshot.providers = {
      provider(QStringLiteral("Current Mod"), FileConflictSourceKind::Archive,
               true, true, QStringLiteral("Base.bsa"), {}, 2),
      provider(QStringLiteral("Current Mod"), FileConflictSourceKind::Archive,
               true, false, QStringLiteral("Patch.bsa"), {}, 4),
  };

  inspector.setSnapshot(snapshot);
  EXPECT_EQ(label(inspector, QStringLiteral("fileConflictState"))->text(),
            QStringLiteral("No conflict reported"));
  EXPECT_NE(label(inspector, QStringLiteral("fileConflictWinnerRule"))->text().indexOf(
                QStringLiteral("reports no conflict")), -1);
  EXPECT_NE(label(inspector, QStringLiteral("fileConflictProviderSource_1"))->text()
                .indexOf(QStringLiteral("Patch.bsa")), -1);

  snapshot.state = FileConflictInspectorState::Unknown;
  snapshot.detail = QStringLiteral("<snapshot unavailable>");
  snapshot.providers.clear();
  inspector.setSnapshot(snapshot);
  EXPECT_EQ(label(inspector, QStringLiteral("fileConflictState"))->text(),
            QStringLiteral("Conflict status is unavailable"));
  EXPECT_EQ(label(inspector, QStringLiteral("fileConflictDetail"))->text(),
            QStringLiteral("<snapshot unavailable>"));
  EXPECT_EQ(label(inspector, QStringLiteral("fileConflictDetail"))->textFormat(),
            Qt::PlainText);
  EXPECT_NE(label(inspector, QStringLiteral("fileConflictWinnerRule"))->text().indexOf(
                QStringLiteral("Some source details are unavailable")), -1);
  EXPECT_NE(label(inspector, QStringLiteral("fileConflictNoProviders")), nullptr);
}

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QApplication app(argc, argv);

  for (int i = 1; i < argc; ++i) {
    if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--render-review")) {
      if (i + 1 >= argc) {
        qCritical("--render-review requires an output directory");
        return 2;
      }
      return renderReviewImages(QString::fromLocal8Bit(argv[i + 1]), app) ? 0 : 1;
    }
  }

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
