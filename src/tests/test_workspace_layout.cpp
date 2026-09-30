#include "workspacelayout.h"

#include <QApplication>
#include <QEvent>
#include <QListWidget>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <gtest/gtest.h>

namespace
{
class LayoutEvents final : public QObject
{
public:
  int visibilityChanges = 0;
  int resizes = 0;

  void watch(QWidget* widget) { widget->installEventFilter(this); }

  bool eventFilter(QObject*, QEvent* event) override
  {
    if (event->type() == QEvent::Show || event->type() == QEvent::Hide)
      ++visibilityChanges;
    if (event->type() == QEvent::Resize) ++resizes;
    return false;
  }
};

class WorkspaceLayoutTest : public ::testing::Test
{
protected:
  QWidget root;
  QSplitter* categories;
  QSplitter* panes;
  QWidget *filters, *mods, *right, *inspector, *tools, *plugins, *downloads;
  QTabWidget* tabs;
  QListWidget *modList, *pluginList;
  WorkspaceLayout* layout;

  void SetUp() override
  {
    auto* outer = new QVBoxLayout(&root);
    tools = new QWidget(&root);
    outer->addWidget(tools);
    categories = new QSplitter(&root);
    outer->addWidget(categories);
    filters = new QWidget(categories);
    panes = new QSplitter(categories);
    mods = new QWidget(panes);
    right = new QWidget(panes);
    auto* ml = new QVBoxLayout(mods);
    modList = new QListWidget;
    modList->addItems({"A", "B", "C"});
    ml->addWidget(modList);
    modList->setCurrentRow(1);
    auto* rl = new QVBoxLayout(right);
    tabs = new QTabWidget;
    rl->addWidget(tabs);
    inspector = new QWidget;
    rl->addWidget(inspector);
    inspector->setMinimumWidth(240);
    inspector->hide();
    plugins = new QWidget;
    auto* pl = new QVBoxLayout(plugins);
    pluginList = new QListWidget;
    pluginList->addItems({"One.esp", "Two.esp"});
    pl->addWidget(pluginList);
    pluginList->setCurrentRow(1);
    downloads = new QWidget;
    tabs->addTab(plugins, "Plugins");
    tabs->addTab(downloads, "Downloads");
    root.resize(1300, 760);
    root.show();
    QApplication::processEvents();
    categories->setSizes({200, 1000});
    panes->setSizes({600, 400});
    layout = new WorkspaceLayout(categories, panes, mods, filters, right, tabs,
                                 plugins, inspector, tools, &root);
    layout->initialize(WorkspaceLayout::Mode::Split, true);
  }
};

TEST_F(WorkspaceLayoutTest, TasksPreserveModelsSelectionsAndBothSplitterWidths)
{
  const auto widths = panes->sizes();
  const auto filterWidths = categories->sizes();
  auto* model = modList->model();
  auto* selected = modList->currentItem();
  auto* plugin = pluginList->currentItem();
  auto* pluginParent = plugins->parentWidget();
  ASSERT_TRUE(layout->showPage(downloads));
  EXPECT_FALSE(mods->isHidden());
  EXPECT_FALSE(filters->isHidden());
  EXPECT_FALSE(tools->isHidden());
  EXPECT_FALSE(tabs->tabBar()->isHidden());
  EXPECT_EQ(tabs->currentWidget(), downloads);
  ASSERT_TRUE(layout->showPage(plugins));
  layout->showMods();
  QApplication::processEvents();
  EXPECT_EQ(modList->model(), model);
  EXPECT_EQ(modList->currentItem(), selected);
  EXPECT_EQ(pluginList->currentItem(), plugin);
  EXPECT_EQ(plugins->parentWidget(), pluginParent);
  EXPECT_EQ(panes->sizes(), widths);
  EXPECT_EQ(categories->sizes(), filterWidths);
  EXPECT_FALSE(mods->isHidden());
  EXPECT_FALSE(filters->isHidden());
  EXPECT_FALSE(tools->isHidden());
}

TEST_F(WorkspaceLayoutTest, DetailsSplitAndFullRetainIndependentWidths)
{
  const auto split = panes->sizes();
  layout->setMode(WorkspaceLayout::Mode::Details);
  panes->setSizes({700, 260});
  const auto details = panes->sizes();
  EXPECT_FALSE(inspector->isHidden());
  EXPECT_TRUE(tabs->isHidden());
  layout->setMode(WorkspaceLayout::Mode::Full);
  EXPECT_TRUE(right->isHidden());
  layout->showPage(downloads);
  EXPECT_FALSE(right->isHidden());
  layout->showMods();
  EXPECT_EQ(layout->mode(), WorkspaceLayout::Mode::Split);
  EXPECT_EQ(tabs->currentWidget(), downloads);
  layout->setMode(WorkspaceLayout::Mode::Full);
  EXPECT_TRUE(right->isHidden());
  layout->setMode(WorkspaceLayout::Mode::Details);
  EXPECT_EQ(panes->sizes(), details);
  layout->setMode(WorkspaceLayout::Mode::Split);
  EXPECT_EQ(panes->sizes(), split);
  EXPECT_FALSE(tabs->isHidden());
  EXPECT_TRUE(inspector->isHidden());
  EXPECT_EQ(tabs->currentWidget(), downloads);
}

TEST_F(WorkspaceLayoutTest, TabChangesDoNotRevealFiltersOrResizeThePaneShells)
{
  layout->setFiltersVisible(false);
  QApplication::processEvents();
  LayoutEvents events;
  for (auto* widget : {filters, mods, right, tools, static_cast<QWidget*>(tabs)})
    events.watch(widget);
  QObject::connect(tabs, &QTabWidget::currentChanged, &root, [this](int) {
    if (!layout->changing()) layout->showPage(tabs->currentWidget());
  });
  const auto widths = panes->sizes();
  for (int i = 0; i < 12; ++i) {
    tabs->setCurrentWidget(i % 2 == 0 ? downloads : plugins);
    QApplication::processEvents();
  }
  EXPECT_EQ(events.visibilityChanges, 0);
  EXPECT_EQ(events.resizes, 0);
  EXPECT_EQ(panes->sizes(), widths);
  EXPECT_FALSE(filters->isVisible());
}

TEST_F(WorkspaceLayoutTest, SavingTabbedLayoutLeavesVisibleGeometryAlone)
{
  layout->setFiltersVisible(false);
  layout->showPage(downloads);
  QApplication::processEvents();
  LayoutEvents events;
  for (auto* widget : {filters, mods, right, tools, static_cast<QWidget*>(tabs)})
    events.watch(widget);
  layout->withStoredLayout([&] {
    EXPECT_TRUE(categories->updatesEnabled());
    EXPECT_EQ(tabs->currentWidget(), downloads);
  });
  QApplication::processEvents();
  EXPECT_EQ(events.visibilityChanges, 0);
  EXPECT_EQ(events.resizes, 0);
}

TEST_F(WorkspaceLayoutTest, SavingDetailsNeverPaintsTheTemporaryTabbedLayout)
{
  layout->setMode(WorkspaceLayout::Mode::Details);
  const auto widths = panes->sizes();
  layout->withStoredLayout([&] {
    EXPECT_FALSE(categories->updatesEnabled());
    QApplication::processEvents();
  });
  EXPECT_TRUE(categories->updatesEnabled());
  EXPECT_EQ(layout->mode(), WorkspaceLayout::Mode::Details);
  EXPECT_EQ(panes->sizes(), widths);
  EXPECT_TRUE(inspector->isVisible());
  EXPECT_FALSE(tabs->isVisible());
}

TEST_F(WorkspaceLayoutTest, FilterPreferenceAppliesWhileUsingAnyTaskTab)
{
  layout->showPage(downloads);
  layout->setFiltersVisible(false);
  layout->setFiltersVisible(true);
  EXPECT_FALSE(filters->isHidden());
  EXPECT_TRUE(layout->filtersVisible());
  layout->showMods();
  EXPECT_FALSE(filters->isHidden());
  layout->showPage(downloads);
  layout->setFiltersVisible(false);
  layout->showMods();
  EXPECT_TRUE(filters->isHidden());
}

TEST_F(WorkspaceLayoutTest, SavingFromHiddenLibraryPreservesTaskAndCanonicalLayout)
{
  const auto splitState = panes->saveState();
  layout->showPage(downloads);
  layout->setMode(WorkspaceLayout::Mode::Details);
  const auto details = layout->detailsState();
  root.hide(); // MainWindow's Library stack hides the entire workspace.
  int tabSignals = 0;
  QObject::connect(tabs, &QTabWidget::currentChanged, [&] { ++tabSignals; });
  QByteArray saved;
  layout->withStoredLayout([&] {
    saved = panes->saveState();
    EXPECT_TRUE(layout->filtersVisible());
    EXPECT_EQ(tabs->currentWidget(), downloads);
  });
  EXPECT_EQ(tabSignals, 0);
  EXPECT_EQ(saved, splitState);
  EXPECT_EQ(tabs->currentWidget(), downloads);
  EXPECT_EQ(layout->mode(), WorkspaceLayout::Mode::Details);
  EXPECT_EQ(layout->detailsState(), details);
  root.show();
  layout->showMods();
  EXPECT_FALSE(inspector->isHidden());
  EXPECT_FALSE(filters->isHidden());
}

TEST_F(WorkspaceLayoutTest, MissingPageDoesNotReplaceDestination)
{
  layout->showPage(downloads);
  QWidget unavailable;
  EXPECT_FALSE(layout->showPage(&unavailable));
  EXPECT_EQ(tabs->currentWidget(), downloads);
  tabs->removeTab(tabs->indexOf(plugins));
  layout->setMode(WorkspaceLayout::Mode::Split);
  EXPECT_EQ(layout->mode(), WorkspaceLayout::Mode::Split);
  EXPECT_EQ(tabs->currentWidget(), downloads);
  EXPECT_FALSE(mods->isHidden());
  tabs->removeTab(tabs->indexOf(downloads));
  layout->setMode(WorkspaceLayout::Mode::Split);
  EXPECT_EQ(layout->mode(), WorkspaceLayout::Mode::Full);
  EXPECT_TRUE(inspector->isHidden());
}
}

int main(int argc, char** argv)
{
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
