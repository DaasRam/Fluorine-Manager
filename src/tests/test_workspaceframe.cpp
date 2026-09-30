#include "fluorinetheme.h"
#include "selectedmodpanel.h"
#include "workspaceframe.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QColor>
#include <QDir>
#include <QFrame>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QSignalSpy>
#include <QTabBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <gtest/gtest.h>

namespace
{
struct HeaderControls
{
  QWidget* profile = nullptr;
  QWidget* launch = nullptr;
  QComboBox* profileSelector = nullptr;
  QComboBox* programSelector = nullptr;
  QPushButton* runButton = nullptr;
  QToolButton* checksButton = nullptr;
  QToolButton* optionsButton = nullptr;
};

HeaderControls makeHeaderControls()
{
  HeaderControls controls;
  controls.profile = new QWidget;
  controls.profile->setObjectName(QStringLiteral("fixtureProfileControls"));
  controls.profile->setMinimumWidth(255);
  auto* profileLayout = new QHBoxLayout(controls.profile);
  profileLayout->setContentsMargins(0, 0, 0, 0);
  profileLayout->setSpacing(6);

  auto* profileLabel = new QLabel(QObject::tr("Profile"), controls.profile);
  profileLabel->setObjectName(QStringLiteral("fixtureProfileLabel"));
  controls.profileSelector = new QComboBox(controls.profile);
  controls.profileSelector->setObjectName(QStringLiteral("fixtureProfileSelector"));
  controls.profileSelector->addItems({QStringLiteral("Everyday"),
                                     QStringLiteral("Testing"),
                                     QStringLiteral("Clean install")});
  controls.profileSelector->setMinimumWidth(124);
  auto* manageButton = new QToolButton(controls.profile);
  manageButton->setObjectName(QStringLiteral("fixtureManageProfile"));
  manageButton->setText(QObject::tr("Profiles…"));
  manageButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
  profileLayout->addWidget(profileLabel);
  profileLayout->addWidget(controls.profileSelector, 1);
  profileLayout->addWidget(manageButton);

  controls.launch = new QWidget;
  controls.launch->setObjectName(QStringLiteral("fixtureLaunchControls"));
  controls.launch->setMinimumWidth(430);
  auto* launchLayout = new QHBoxLayout(controls.launch);
  launchLayout->setContentsMargins(0, 0, 0, 0);
  launchLayout->setSpacing(6);

  auto* programLabel = new QLabel(QObject::tr("Program"), controls.launch);
  programLabel->setObjectName(QStringLiteral("fixtureProgramLabel"));
  controls.programSelector = new QComboBox(controls.launch);
  controls.programSelector->setObjectName(QStringLiteral("fixtureProgramSelector"));
  controls.programSelector->addItems({QStringLiteral("Skyrim Script Extender"),
                                    QStringLiteral("Skyrim Special Edition"),
                                    QStringLiteral("Mod Organizer Shell")});
  controls.programSelector->setMinimumWidth(132);
  controls.runButton = new QPushButton(QObject::tr("Run"), controls.launch);
  controls.runButton->setObjectName(QStringLiteral("fixtureRun"));
  controls.runButton->setProperty("primary", true);
  controls.checksButton = new QToolButton(controls.launch);
  controls.checksButton->setObjectName(QStringLiteral("fixtureChecks"));
  controls.checksButton->setText(QObject::tr("Checks"));
  controls.checksButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
  controls.optionsButton = new QToolButton(controls.launch);
  controls.optionsButton->setObjectName(QStringLiteral("fixtureOptions"));
  controls.optionsButton->setText(QObject::tr("Options"));
  controls.optionsButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
  launchLayout->addWidget(programLabel);
  launchLayout->addWidget(controls.programSelector, 1);
  launchLayout->addWidget(controls.runButton);
  launchLayout->addWidget(controls.checksButton);
  launchLayout->addWidget(controls.optionsButton);

  return controls;
}

QRect rectIn(QWidget* widget, QWidget* ancestor)
{
  return QRect(widget->mapTo(ancestor, QPoint(0, 0)), widget->size());
}

QPalette fixturePalette(bool dark)
{
  QPalette palette;
  if (dark) {
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#20242b")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#e3e7ee")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#171a20")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#1d2128")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#e3e7ee")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#2d333d")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#e3e7ee")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#6f9fe8")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#141820")));
  } else {
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#f1f3f6")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#20252d")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#ffffff")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#f4f6f9")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#20252d")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#e8ebf0")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#20252d")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#356fb9")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#ffffff")));
  }
  return palette;
}

bool renderFixture(const QString& outputDirectory, bool dark, int width,
                   const QPalette& originalPalette)
{
  QApplication::setPalette(fixturePalette(dark));

  WorkspaceFrame frame;
  FluorineTheme::apply(&frame);
  frame.setContext(QStringLiteral("Northern Lights - Stability & Visuals"),
                   QStringLiteral("The Elder Scrolls V: Skyrim Special Edition"));
  auto controls = makeHeaderControls();
  frame.setHeaderControls(controls.profile, controls.launch);

  auto* libraryAction = new QAction(QObject::tr("Manage game setups"), &frame);
  libraryAction->setToolTip(QObject::tr("Return to all installed games and setups"));
  frame.setLibraryAction(libraryAction);
  frame.addDestination(QStringLiteral("mods"), QObject::tr("Mods"));
  frame.addDestination(QStringLiteral("plugins"), QObject::tr("Plugins"));
  frame.addDestination(QStringLiteral("downloads"), QObject::tr("Downloads"));
  frame.addDestination(QStringLiteral("files"), QObject::tr("Files"));
  frame.addDestination(QStringLiteral("saves"), QObject::tr("Saves"));
  frame.setDestination(QStringLiteral("mods"));

  auto* density = new QToolButton;
  density->setObjectName(QStringLiteral("fixtureViewDensity"));
  density->setText(QObject::tr("View"));
  density->setToolButtonStyle(Qt::ToolButtonTextOnly);
  frame.addNavigationWidget(density);

  auto* body = new QWidget;
  auto* bodyLayout = new QHBoxLayout(body);
  bodyLayout->setContentsMargins(0, 0, 0, 0);
  bodyLayout->setSpacing(0);
  auto* mods = new QTreeWidget(body);
  mods->setObjectName(QStringLiteral("fixtureModList"));
  mods->setAlternatingRowColors(true);
  mods->setRootIsDecorated(false);
  mods->setUniformRowHeights(true);
  mods->setHeaderLabels({QObject::tr("Mod"), QObject::tr("Enabled"),
                         QObject::tr("Version")});
  mods->setColumnWidth(0, qMax(420, width - 780));
  mods->setColumnWidth(1, 96);
  mods->setColumnWidth(2, 96);
  for (int i = 0; i < 58; ++i) {
    const QString index = QStringLiteral("%1").arg(i + 1, 3, 10, QLatin1Char('0'));
    const QString name = (i == 6)
                             ? QStringLiteral("Northern Lights - Terrain and Weather Rework")
                             : QStringLiteral("Community Mod %1").arg(index);
    auto* row = new QTreeWidgetItem(mods, {name,
                                          i % 7 == 3 ? QObject::tr("Off")
                                                     : QObject::tr("On"),
                                          QStringLiteral("%1.%2")
                                              .arg(1 + (i % 4))
                                              .arg(i % 10)});
    row->setData(0, Qt::UserRole, i);
  }
  mods->setCurrentItem(mods->topLevelItem(6));
  bodyLayout->addWidget(mods, 1);

  auto* selected = new SelectedModPanel(body);
  SelectedModSummary summary;
  summary.name = QStringLiteral("Northern Lights - Terrain and Weather Rework");
  summary.version = QStringLiteral("3.8.1");
  summary.category = QStringLiteral("Environment");
  summary.author = QStringLiteral("Fluorine community");
  summary.description = QStringLiteral(
      "Reworks the landscape, weather transitions, and distant terrain. "
      "Installed alongside a large, curated setup.");
  summary.notes = QStringLiteral("Keep above the seasonal texture patch.");
  summary.profile = QStringLiteral("Northern Lights - Stability & Visuals");
  summary.priority = 143;
  summary.enabled = true;
  summary.winningMods = 4;
  summary.losingMods = 2;
  selected->setSummary(summary);
  bodyLayout->addWidget(selected);
  frame.setContent(body);

  frame.resize(width, 820);
  frame.show();
  QApplication::processEvents();
  const QString theme = dark ? QStringLiteral("dark") : QStringLiteral("light");
  const QString imagePath = QDir(outputDirectory).filePath(
      QStringLiteral("workspaceframe-fixture-%1-%2x820.png")
          .arg(theme)
          .arg(width));
  const bool saved = frame.grab().save(imagePath);
  QApplication::setPalette(originalPalette);
  return saved;
}

TEST(WorkspaceFrame, NavigationUsesStableIdsAndRelabelsExistingDestination)
{
  WorkspaceFrame frame;
  auto* tabs = frame.findChild<QTabBar*>(QStringLiteral("workspaceDestinations"));
  ASSERT_NE(tabs, nullptr);

  QSignalSpy changed(&frame, &WorkspaceFrame::destinationChanged);
  ASSERT_TRUE(changed.isValid());
  frame.addDestination(QStringLiteral("mods"), QStringLiteral("Mods"));
  ASSERT_EQ(frame.destination(), QStringLiteral("mods"));
  ASSERT_EQ(tabs->count(), 1);
  EXPECT_EQ(changed.count(), 1);
  EXPECT_EQ(changed.takeFirst().at(0).toString(), QStringLiteral("mods"));

  frame.addDestination(QStringLiteral("plugins"), QStringLiteral("Plugins"));
  frame.addDestination(QStringLiteral("downloads"), QStringLiteral("Downloads"));
  EXPECT_EQ(tabs->count(), 3);
  EXPECT_EQ(changed.count(), 0);

  frame.setDestination(QStringLiteral("downloads"));
  EXPECT_EQ(frame.destination(), QStringLiteral("downloads"));
  ASSERT_EQ(changed.count(), 1);
  EXPECT_EQ(changed.takeFirst().at(0).toString(), QStringLiteral("downloads"));

  frame.addDestination(QStringLiteral("downloads"), QStringLiteral("Transfers"));
  EXPECT_EQ(tabs->count(), 3);
  EXPECT_EQ(tabs->tabText(tabs->currentIndex()), QStringLiteral("Transfers"));
  EXPECT_EQ(tabs->tabToolTip(tabs->currentIndex()), QStringLiteral("Transfers"));
  EXPECT_EQ(frame.destination(), QStringLiteral("downloads"));
  EXPECT_EQ(changed.count(), 0);

  frame.addDestination(QString(), QStringLiteral("Ignored"));
  frame.setDestination(QStringLiteral("missing"));
  EXPECT_EQ(tabs->count(), 3);
  EXPECT_EQ(frame.destination(), QStringLiteral("downloads"));
  EXPECT_EQ(changed.count(), 0);
}

TEST(WorkspaceFrame, HiddenDestinationCannotStealFocusAndCanBeRestored)
{
  WorkspaceFrame frame;
  frame.addDestination("mods", "Mods");
  frame.addDestination("plugins", "Plugins");
  frame.addDestination("downloads", "Downloads");
  auto* tabs = frame.findChild<QTabBar*>("workspaceDestinations");
  ASSERT_NE(tabs, nullptr);
  frame.setDestination("downloads");
  QSignalSpy changed(&frame, &WorkspaceFrame::destinationChanged);
  frame.addDestination("mods", "Mods + Plugins");
  frame.setDestinationVisible("plugins", false);
  frame.setDestination("plugins");
  EXPECT_EQ(frame.destination(), QStringLiteral("downloads"));
  EXPECT_EQ(changed.count(), 0);
  EXPECT_FALSE(tabs->isTabVisible(1));
  EXPECT_EQ(tabs->tabText(0), QStringLiteral("Mods + Plugins"));

  frame.setDestinationVisible("plugins", true);
  frame.addDestination("mods", "Mods");
  frame.setDestination("plugins");
  EXPECT_TRUE(tabs->isTabVisible(1));
  EXPECT_EQ(frame.destination(), QStringLiteral("plugins"));
  EXPECT_EQ(changed.count(), 1);
  EXPECT_EQ(tabs->count(), 3);
}

TEST(WorkspaceFrame, LibraryButtonRetainsActionStateAndInvokesTheRealAction)
{
  WorkspaceFrame frame;
  QAction action(QStringLiteral("Manage game setups"), &frame);
  action.setToolTip(QStringLiteral("Return to installed games and setups"));
  QSignalSpy triggered(&action, &QAction::triggered);
  ASSERT_TRUE(triggered.isValid());

  frame.setLibraryAction(&action);
  auto* button = frame.findChild<QToolButton*>(QStringLiteral("workspaceLibraryButton"));
  auto* divider = frame.findChild<QFrame*>(QStringLiteral("workspaceHeaderDivider"));
  ASSERT_NE(button, nullptr);
  ASSERT_NE(divider, nullptr);
  EXPECT_EQ(button->text(), QStringLiteral("Library"));
  EXPECT_EQ(button->accessibleName(), QStringLiteral("Library"));
  EXPECT_EQ(button->accessibleDescription(), QStringLiteral("Manage game setups"));
  EXPECT_EQ(button->toolTip(), QStringLiteral("Return to installed games and setups"));
  EXPECT_TRUE(button->isEnabled());
  EXPECT_FALSE(button->isHidden());
  EXPECT_FALSE(divider->isHidden());

  button->click();
  ASSERT_EQ(triggered.count(), 1);
  EXPECT_FALSE(triggered.takeFirst().at(0).toBool());

  action.setEnabled(false);
  EXPECT_FALSE(button->isEnabled());
  button->click();
  EXPECT_EQ(triggered.count(), 0);

  action.setEnabled(true);
  action.setVisible(false);
  EXPECT_TRUE(button->isHidden());
  EXPECT_TRUE(divider->isHidden());
  action.setVisible(true);
  EXPECT_FALSE(button->isHidden());
  EXPECT_FALSE(divider->isHidden());
}

TEST(WorkspaceFrame, FrameCanDestroyAnOwnedLibraryActionDuringTeardown)
{
  auto* frame = new WorkspaceFrame;
  auto* action = new QAction(QStringLiteral("Manage game setups"), frame);
  frame->setLibraryAction(action);

  auto* button = frame->findChild<QToolButton*>(QStringLiteral("workspaceLibraryButton"));
  ASSERT_NE(button, nullptr);
  EXPECT_FALSE(button->isHidden());

  // The action is a QObject child of the frame. Destroying the frame must
  // disconnect its destroyed callback before deleting the header widgets.
  delete frame;
}

TEST(WorkspaceFrame, ContextStaysLiteralAndAccessibleWhenItsDisplayTextIsElided)
{
  WorkspaceFrame frame;
  auto controls = makeHeaderControls();
  frame.setHeaderControls(controls.profile, controls.launch);
  frame.setContext(
      QStringLiteral("A&B <Curated> Northern Lights - Stability and Visual Rebalance"),
      QStringLiteral("Skyrim & Special Edition - The Elder Scrolls V Anniversary Edition"));
  frame.resize(1050, 650);
  frame.show();
  QApplication::processEvents();

  auto* context = frame.findChild<QWidget*>(QStringLiteral("workspaceContext"));
  auto* setup = frame.findChild<QLabel*>(QStringLiteral("workspaceSetupName"));
  auto* game = frame.findChild<QLabel*>(QStringLiteral("workspaceGameName"));
  ASSERT_NE(context, nullptr);
  ASSERT_NE(setup, nullptr);
  ASSERT_NE(game, nullptr);
  EXPECT_EQ(setup->textFormat(), Qt::PlainText);
  EXPECT_EQ(game->textFormat(), Qt::PlainText);
  EXPECT_EQ(setup->accessibleName(),
            QStringLiteral("A&B <Curated> Northern Lights - Stability and Visual Rebalance"));
  EXPECT_EQ(game->accessibleName(),
            QStringLiteral("Skyrim & Special Edition - The Elder Scrolls V Anniversary Edition"));
  EXPECT_EQ(context->accessibleName(),
            QStringLiteral("A&B <Curated> Northern Lights - Stability and Visual Rebalance, "
                           "Skyrim & Special Edition - The Elder Scrolls V Anniversary Edition"));
  EXPECT_EQ(setup->toolTip(), setup->accessibleName());
  EXPECT_EQ(game->toolTip(), game->accessibleName());
  EXPECT_NE(setup->text().indexOf(QLatin1Char('&')), -1);
  EXPECT_NE(game->text().indexOf(QLatin1Char('&')), -1);
  EXPECT_TRUE(setup->text().endsWith(QChar(0x2026)));
  EXPECT_TRUE(game->text().endsWith(QChar(0x2026)));
  EXPECT_LT(setup->text().size(), setup->accessibleName().size());
  EXPECT_LT(game->text().size(), game->accessibleName().size());
}

TEST(WorkspaceFrame, RealisticHeaderControlsReflowWithoutClippingOrOverlap)
{
  WorkspaceFrame frame;
  auto controls = makeHeaderControls();
  frame.setHeaderControls(controls.profile, controls.launch);
  frame.setContext(QStringLiteral("Northern Lights"),
                   QStringLiteral("Skyrim Special Edition"));
  frame.addDestination(QStringLiteral("mods"), QStringLiteral("Mods"));
  frame.addDestination(QStringLiteral("plugins"), QStringLiteral("Plugins"));
  frame.resize(1300, 720);
  frame.show();
  QApplication::processEvents();

  auto* header = frame.findChild<QWidget*>(QStringLiteral("workspaceHeader"));
  auto* identity = controls.profile->parentWidget();
  auto* controlsRow = frame.findChild<QWidget*>(QStringLiteral("workspaceHeaderControls"));
  ASSERT_NE(header, nullptr);
  ASSERT_NE(controlsRow, nullptr);
  EXPECT_TRUE(controlsRow->isHidden());
  EXPECT_EQ(controls.profile->parentWidget(), identity);
  EXPECT_EQ(controls.launch->parentWidget(), identity);
  EXPECT_TRUE(rectIn(controls.profile, header).right() < rectIn(controls.launch, header).left() ||
              rectIn(controls.launch, header).right() < rectIn(controls.profile, header).left());
  EXPECT_TRUE(header->rect().contains(rectIn(controls.profile, header)));
  EXPECT_TRUE(header->rect().contains(rectIn(controls.launch, header)));

  QFont largeFont = frame.font();
  largeFont.setPointSize(20);
  frame.setFont(largeFont);
  frame.resize(780, 720);
  QApplication::processEvents();
  EXPECT_FALSE(controlsRow->isHidden());
  EXPECT_TRUE(controls.profile->isVisibleTo(&frame));
  EXPECT_TRUE(controls.launch->isVisibleTo(&frame));
  EXPECT_TRUE(controls.profileSelector->isVisibleTo(&frame));
  EXPECT_TRUE(controls.programSelector->isVisibleTo(&frame));
  EXPECT_TRUE(controls.runButton->isVisibleTo(&frame));
  EXPECT_TRUE(controls.checksButton->isVisibleTo(&frame));
  EXPECT_TRUE(controls.optionsButton->isVisibleTo(&frame));
  EXPECT_TRUE(header->rect().contains(rectIn(controls.profile, header)));
  EXPECT_TRUE(header->rect().contains(rectIn(controls.launch, header)));
  EXPECT_FALSE(rectIn(controls.profile, header).intersects(rectIn(controls.launch, header)));

  frame.resize(600, 720);
  QApplication::processEvents();
  EXPECT_FALSE(controlsRow->isHidden());
  EXPECT_TRUE(controls.profile->isVisibleTo(&frame));
  EXPECT_TRUE(controls.launch->isVisibleTo(&frame));
  EXPECT_TRUE(header->rect().contains(rectIn(controls.profile, header)));
  EXPECT_TRUE(header->rect().contains(rectIn(controls.launch, header)));
  EXPECT_FALSE(rectIn(controls.profile, header).intersects(rectIn(controls.launch, header)));
  EXPECT_EQ(frame.destination(), QStringLiteral("mods"));
}

TEST(WorkspaceFrame, OptionalCaptureRendersThemedModListFixture)
{
  const QString outputDirectory = qEnvironmentVariable("FLUORINE_UI_CAPTURE_DIR");
  if (outputDirectory.isEmpty()) {
    GTEST_SKIP() << "Set FLUORINE_UI_CAPTURE_DIR to write labeled WorkspaceFrame fixture images.";
  }

  ASSERT_TRUE(QDir().mkpath(outputDirectory));
  const QPalette originalPalette = QApplication::palette();
  for (const bool dark : {false, true}) {
    for (const int width : {1300, 1000}) {
      ASSERT_TRUE(renderFixture(outputDirectory, dark, width, originalPalette))
          << "Could not save WorkspaceFrame fixture at " << width << "px";
    }
  }
  QApplication::setPalette(originalPalette);
}
} // namespace

int main(int argc, char** argv)
{
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("FluorineWorkspaceFrameTests"));
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
