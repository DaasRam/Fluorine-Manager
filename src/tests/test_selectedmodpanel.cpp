#include "selectedmodpanel.h"

#include <QApplication>
#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>
#include <QWidget>
#include <gtest/gtest.h>

namespace
{
class SelectedModPanelTest : public ::testing::Test
{
protected:
  SelectedModPanel panel;

  void SetUp() override
  {
    panel.resize(360, 560);
    panel.show();
    QApplication::processEvents();
  }

  template <class T> T* child(const char* name)
  {
    return panel.findChild<T*>(name);
  }

  SelectedModSummary summary()
  {
    SelectedModSummary result;
    result.name = QStringLiteral("Texture Fix");
    result.version = QStringLiteral("2.4");
    result.category = QStringLiteral("Textures");
    result.author = QStringLiteral("Example author");
    result.description = QStringLiteral("Improves several textures.");
    result.notes = QStringLiteral("Installed for this profile.");
    result.profile = QStringLiteral("Current profile");
    result.priority = 7;
    result.enabled = true;
    return result;
  }
};

TEST_F(SelectedModPanelTest, UserTextIsShownLiterally)
{
  auto mod = summary();
  mod.name = QStringLiteral("<b>Texture & Fix</b>");
  mod.notes = QStringLiteral("<img src=x onerror=alert(1)>");
  panel.setSummary(mod);

  ASSERT_NE(child<QLabel>("selectedModPanelName"), nullptr);
  EXPECT_EQ(child<QLabel>("selectedModPanelName")->text(), mod.name);
  EXPECT_EQ(child<QLabel>("selectedModPanelName")->textFormat(), Qt::PlainText);
  ASSERT_NE(child<QLabel>("selectedModPanelNotesValue"), nullptr);
  EXPECT_EQ(child<QLabel>("selectedModPanelNotesValue")->text(), mod.notes);
  EXPECT_EQ(child<QLabel>("selectedModPanelNotesValue")->textFormat(), Qt::PlainText);
}

TEST_F(SelectedModPanelTest, GameContentShowsItsIdentityWithoutUnavailableActions)
{
  auto mod = summary();
  mod.name = QStringLiteral("DLC: Contraptions Workshop");
  mod.external = true;
  panel.setSummary(mod);
  EXPECT_EQ(child<QLabel>("selectedModPanelHeading")->text(), QStringLiteral("Game content"));
  EXPECT_EQ(child<QLabel>("selectedModPanelName")->text(), mod.name);
  EXPECT_EQ(child<QLabel>("selectedModPanelStatus")->text(), QStringLiteral("Managed outside Fluorine"));
  EXPECT_TRUE(child<QLabel>("selectedModPanelEmpty")->isHidden());
  EXPECT_TRUE(child<QPushButton>("selectedModPanelDetails")->isHidden());
  EXPECT_TRUE(child<QPushButton>("selectedModPanelInspectConflicts")->isHidden());

  panel.setSummary(summary());
  EXPECT_EQ(child<QLabel>("selectedModPanelHeading")->text(), QStringLiteral("Selected mod"));
  EXPECT_FALSE(child<QPushButton>("selectedModPanelDetails")->isHidden());
  EXPECT_FALSE(child<QPushButton>("selectedModPanelInspectConflicts")->isHidden());
}

TEST_F(SelectedModPanelTest, UnknownConflictCountsStayOutOfSummary)
{
  auto mod = summary();
  mod.winningMods = 0;
  panel.setSummary(mod);

  auto* row = child<QWidget>("selectedModPanelConflictsRow");
  auto* value = child<QLabel>("selectedModPanelConflictsValue");
  auto* inspect = child<QPushButton>("selectedModPanelInspectConflicts");
  ASSERT_NE(row, nullptr);
  ASSERT_NE(value, nullptr);
  ASSERT_NE(inspect, nullptr);
  EXPECT_TRUE(row->isHidden());
  EXPECT_TRUE(value->text().isEmpty());
  EXPECT_EQ(value->text().indexOf(QStringLiteral("No loose-file conflicts")), -1);
  EXPECT_EQ(inspect->text(), QStringLiteral("Inspect file conflicts"));
  EXPECT_FALSE(inspect->isHidden());
}

TEST_F(SelectedModPanelTest, SuppliedLooseFileCountsAreShown)
{
  auto mod = summary();
  mod.winningMods = 2;
  mod.losingMods = 1;
  panel.setSummary(mod);

  auto* row = child<QWidget>("selectedModPanelConflictsRow");
  auto* value = child<QLabel>("selectedModPanelConflictsValue");
  ASSERT_NE(row, nullptr);
  ASSERT_NE(value, nullptr);
  EXPECT_FALSE(row->isHidden());
  EXPECT_NE(value->text().indexOf(QStringLiteral("This mod overwrites 2")), -1);
  EXPECT_NE(value->text().indexOf(QStringLiteral("Files from 1")), -1);
}

TEST_F(SelectedModPanelTest, MultipleSelectionClearsStaleDetailsAndActions)
{
  panel.setSummary(summary());
  ASSERT_NE(child<QLabel>("selectedModPanelName"), nullptr);
  EXPECT_EQ(child<QLabel>("selectedModPanelName")->text(), QStringLiteral("Texture Fix"));
  ASSERT_NE(child<QWidget>("selectedModPanelPriorityRow"), nullptr);
  EXPECT_FALSE(child<QWidget>("selectedModPanelPriorityRow")->isHidden());

  panel.setMultipleSelection(3);
  QApplication::processEvents();

  EXPECT_TRUE(child<QLabel>("selectedModPanelName")->isHidden());
  EXPECT_TRUE(child<QLabel>("selectedModPanelStatus")->isHidden());
  EXPECT_TRUE(child<QWidget>("selectedModPanelPriorityRow")->isHidden());
  EXPECT_TRUE(child<QWidget>("selectedModPanelConflictsRow")->isHidden());
  EXPECT_TRUE(child<QLabel>("selectedModPanelPriorityValue")->text().isEmpty());
  EXPECT_TRUE(child<QLabel>("selectedModPanelNotesValue")->text().isEmpty());
  EXPECT_EQ(child<QLabel>("selectedModPanelName")->text(), QString());
  EXPECT_NE(child<QLabel>("selectedModPanelEmpty")->text().indexOf(
                QStringLiteral("3 mods selected")), -1);
  EXPECT_TRUE(child<QPushButton>("selectedModPanelDetails")->isHidden());
  EXPECT_TRUE(child<QPushButton>("selectedModPanelInspectConflicts")->isHidden());

  auto selectedAgain = summary();
  selectedAgain.name = QStringLiteral("Replacement mod");
  panel.setSummary(selectedAgain);
  EXPECT_EQ(child<QLabel>("selectedModPanelName")->text(),
            QStringLiteral("Replacement mod"));
  EXPECT_FALSE(child<QPushButton>("selectedModPanelDetails")->isHidden());

  panel.setSummary({});
  EXPECT_TRUE(child<QLabel>("selectedModPanelName")->isHidden());
  EXPECT_EQ(child<QLabel>("selectedModPanelName")->text(), QString());
  EXPECT_TRUE(child<QWidget>("selectedModPanelPriorityRow")->isHidden());
  EXPECT_NE(child<QLabel>("selectedModPanelEmpty")->text().indexOf(
                QStringLiteral("Select a mod")), -1);
  EXPECT_TRUE(child<QPushButton>("selectedModPanelDetails")->isHidden());
}

TEST_F(SelectedModPanelTest, SeparatorHasNoModActionsOrStaleFields)
{
  panel.setSummary(summary());
  auto mod = summary();
  mod.separator = true;
  panel.setSummary(mod);

  EXPECT_EQ(child<QLabel>("selectedModPanelHeading")->text(), QStringLiteral("Separator"));
  EXPECT_EQ(child<QLabel>("selectedModPanelName")->text(), QStringLiteral("Texture Fix"));
  EXPECT_NE(child<QLabel>("selectedModPanelStatus")->text().indexOf(
                QStringLiteral("Organizes the mod list")), -1);
  EXPECT_TRUE(child<QWidget>("selectedModPanelProfileRow")->isHidden());
  EXPECT_TRUE(child<QWidget>("selectedModPanelPriorityRow")->isHidden());
  EXPECT_TRUE(child<QWidget>("selectedModPanelConflictsRow")->isHidden());
  EXPECT_TRUE(child<QPushButton>("selectedModPanelDetails")->isHidden());
  EXPECT_TRUE(child<QPushButton>("selectedModPanelInspectConflicts")->isHidden());
}

TEST_F(SelectedModPanelTest, ActionButtonsEmitTheirNavigationSignals)
{
  panel.setSummary(summary());
  QSignalSpy details(&panel, &SelectedModPanel::detailsRequested);
  QSignalSpy conflicts(&panel, &SelectedModPanel::conflictsRequested);
  QSignalSpy close(&panel, &SelectedModPanel::closeRequested);
  ASSERT_TRUE(details.isValid());
  ASSERT_TRUE(conflicts.isValid());
  ASSERT_TRUE(close.isValid());

  child<QPushButton>("selectedModPanelDetails")->click();
  child<QPushButton>("selectedModPanelInspectConflicts")->click();
  child<QToolButton>("selectedModPanelClose")->click();

  EXPECT_EQ(details.count(), 1);
  EXPECT_EQ(conflicts.count(), 1);
  EXPECT_EQ(close.count(), 1);
}

TEST_F(SelectedModPanelTest, LargeTextFitsNarrowPanelAndWrapsInScrollArea)
{
  auto mod = summary();
  mod.description = QString(700, QLatin1Char('W'));
  QFont largeFont = panel.font();
  largeFont.setPointSize(18);
  panel.setFont(largeFont);
  panel.setSummary(mod);
  panel.resize(260, 420);
  QApplication::processEvents();

  auto* scroll = child<QScrollArea>("selectedModPanelScrollArea");
  auto* body = child<QWidget>("selectedModPanelBody");
  auto* description = child<QLabel>("selectedModPanelDescriptionValue");
  ASSERT_NE(scroll, nullptr);
  ASSERT_NE(body, nullptr);
  ASSERT_NE(description, nullptr);
  EXPECT_GE(panel.width(), panel.minimumWidth());
  EXPECT_EQ(scroll->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);
  EXPECT_GT(scroll->viewport()->width(), 0);
  EXPECT_LE(body->width(), scroll->viewport()->width());
  EXPECT_TRUE(description->wordWrap());
  EXPECT_LE(description->width(), description->parentWidget()->width());
  EXPECT_LE(child<QPushButton>("selectedModPanelDetails")->geometry().right(),
            panel.width());
  EXPECT_LE(child<QPushButton>("selectedModPanelInspectConflicts")->geometry().right(),
            panel.width());
}
} // namespace

int main(int argc, char** argv)
{
  QTemporaryDir cache;
  if (!cache.isValid()) return 1;
  qputenv("XDG_CACHE_HOME", cache.path().toUtf8());
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QApplication app(argc, argv);
  app.setApplicationName("FluorineSelectedModPanelTests");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
