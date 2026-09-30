// Opt-in probe for the packaged application, loaded only by the isolated native
// UI smoke run. It exercises the real MainWindow, models and LibraryView. It is
// never linked into or shipped with Fluorine.
#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QHeaderView>
#include <QTreeView>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QStackedWidget>
#include <QSplitter>
#include <QTabBar>
#include <QTest>
#include <QTimer>
#include <QToolButton>

namespace
{
QString output;
QJsonArray checks;
QHash<QString, QByteArray> manualHeaders;

class PaneEvents final : public QObject
{
public:
  int visibilityChanges = 0;
  int resizes = 0;

  void watch(QWidget* widget) { if (widget) widget->installEventFilter(this); }

  bool eventFilter(QObject*, QEvent* event) override
  {
    if (event->type() == QEvent::Show || event->type() == QEvent::Hide)
      ++visibilityChanges;
    if (event->type() == QEvent::Resize) ++resizes;
    return false;
  }
};

void check(bool pass, const QString& description)
{
  checks.append(QJsonObject{{"pass", pass}, {"check", description}});
}

void finish()
{
  QFile results(output + (qEnvironmentVariableIsSet("FLUORINE_UI_SMOKE_RESTORE_COLUMNS")
                              ? "/checks-restore.json" : "/checks.json"));
  if (results.open(QIODevice::WriteOnly))
    results.write(QJsonDocument(QJsonObject{{"checks", checks}}).toJson());
  bool success = true;
  for (const auto& value : checks) success &= value.toObject()["pass"].toBool();
  qApp->exit(success ? 0 : 91);
}

void capture(QMainWindow* window, const QString& name)
{
  QApplication::processEvents();
  check(window->grab().save(output + "/" + name + ".png"), "Capture " + name);
}

void saveManualColumns(QMainWindow* window)
{
  // Let the real application save the tested widths on shutdown, then check
  // them in a separate process after MainWindow's first-paint initialization.
  QJsonObject expected;
  for (auto it = manualHeaders.cbegin(); it != manualHeaders.cend(); ++it) {
    auto* tree = window->findChild<QTreeView*>(it.key());
    auto* header = tree->header();
    check(header->restoreState(it.value()), "Restore tested manual widths for " + it.key());
    QJsonArray widths;
    for (int i = 0; i < header->count(); ++i) widths.append(header->sectionSize(i));
    expected.insert(it.key(), widths);
  }
  QFile file(output + "/manual-columns.json");
  check(file.open(QIODevice::WriteOnly), "Save expected widths for a real application restart");
  file.write(QJsonDocument(expected).toJson());
}

void checkRestoredColumns(QMainWindow* window)
{
  QFile file(output + "/manual-columns.json");
  check(file.open(QIODevice::ReadOnly), "Read expected widths from previous application run");
  const auto expected = QJsonDocument::fromJson(file.readAll()).object();
  check(expected.size() == 2, "Both real lists have saved manual column widths");
  for (auto it = expected.begin(); it != expected.end(); ++it) {
    auto* tree = window->findChild<QTreeView*>(it.key());
    if (!tree) { check(false, "Restored list exists: " + it.key()); continue; }
    const auto* header = tree->header();
    const auto widths = it.value().toArray();
    bool equal = widths.size() == header->count();
    for (int i = 0; i < header->count() && i < widths.size(); ++i)
      equal &= header->sectionSize(i) == widths[i].toInt();
    check(equal, it.key() + " retains every manual column width after reopening");
    check(!header->stretchLastSection(), it.key() + " keeps final-column stretch disabled after reopening");
  }
  finish();
}

void exercise(QMainWindow* window)
{
  if (qEnvironmentVariableIsSet("FLUORINE_UI_SMOKE_RESTORE_COLUMNS")) {
    checkRestoredColumns(window);
    return;
  }
  window->resize(1300, 800);
  auto* tabs = window->findChild<QTabBar*>("workspaceTaskTabs");
  auto* mods = window->findChild<QAbstractItemView*>("modList");
  auto* pages = window->findChild<QStackedWidget*>("workspacePages");
  if (!tabs || !mods || !pages) {
    check(false, "Actual MainWindow workspace is present");
    finish();
    return;
  }
  auto* inspector = window->findChild<QWidget*>("selectedModPanel");
  check(inspector && !inspector->isVisible(), "Mods open without the optional details pane");
  auto* globalNavigation = window->findChild<QWidget*>("workspaceNavigation");
  check(globalNavigation && !globalNavigation->isVisible() && tabs->isVisible() && mods->isVisible(),
        "Task tabs share the right pane beside the visible mod list");
  auto* profileButton = window->findChild<QToolButton*>("profileActionsButton");
  auto* profileSelector = window->findChild<QComboBox*>("profileBox");
  auto* profileLabel = window->findChild<QLabel*>("label_3");
  check(profileButton && profileButton->isVisible() && profileButton->text() == "Profiles: Everyday" &&
            profileSelector && !profileSelector->isVisible() &&
            profileLabel && !profileLabel->isVisible(),
        "One Profiles button shows the active profile");
  check(!window->findChild<QToolButton*>("workspaceViewButton"),
        "The workspace header has no redundant View button");
  auto* viewMenu = window->findChild<QMenu*>("menuView");
  int layoutChoices = 0;
  if (viewMenu) {
    for (auto* action : viewMenu->actions())
      if (action->property("workspaceMode").toBool()) ++layoutChoices;
  }
  check(viewMenu && layoutChoices == 3 &&
            viewMenu->findChild<QMenu*>("workspaceGroupingMenu"),
        "Layout and grouping controls remain in the main View menu");
  check(profileButton && profileButton->toolTip().contains("Everyday"),
        "Profiles tooltip identifies the active profile");
  check(profileButton && !profileButton->menu(), "Profiles has no intermediate Manage menu");
  if (profileButton && !profileButton->menu()) {
    QTimer::singleShot(100, window, [] {
      auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      check(dialog && QByteArray(dialog->metaObject()->className()) == "ProfilesDialog",
            "Profiles button opens the actual profile manager directly");
      if (dialog) dialog->reject();
    });
    profileButton->click();
    // Closing Profiles refreshes the mod model asynchronously and briefly
    // disables MainWindow. Allow its queued refresh to start before checking
    // readiness; checking immediately can see the pre-refresh enabled state.
    QTest::qWait(100);
    check(QTest::qWaitFor([window, mods] {
      return window->isEnabled() && mods->isEnabled() &&
             !QApplication::activeModalWidget();
    }, 5000), "Workspace is interactive after closing Profiles");
  }
  auto* grouping = window->findChild<QMenu*>("workspaceGroupingMenu");
  auto* groupCombo = window->findChild<QComboBox*>("groupCombo");
  check(groupCombo && !groupCombo->isVisible() && grouping && grouping->actions().size() == 3,
        "Grouping is available in View without a toolbar dropdown");
  if (groupCombo && grouping && grouping->actions().size() == 3) {
    grouping->actions()[1]->trigger();
    check(groupCombo->currentIndex() == 1, "View grouping changes the existing grouping model");
    grouping->actions()[0]->trigger();
  }
  auto* modActions = window->findChild<QPushButton*>("listOptionsBtn");
  auto* backup = window->findChild<QAction*>("actionBackupModList");
  auto* restore = window->findChild<QAction*>("actionRestoreModList");
  if (modActions && modActions->menu()) {
    // The global menu rebuilds on every opening. Check the second opening too.
    modActions->menu()->popup(window->mapToGlobal(QPoint(30, 180)));
    modActions->menu()->hide();
    modActions->menu()->popup(window->mapToGlobal(QPoint(30, 180)));
    modActions->menu()->hide();
  }
  check(modActions && modActions->menu() && backup && restore &&
            modActions->menu()->actions().contains(backup) &&
            modActions->menu()->actions().contains(restore),
        "Mod-list backups remain available in Mod actions");
  capture(window, "native-mods");
  auto navigate = [&](const QString& id) {
    for (int i = 0; i < tabs->count(); ++i) {
      if (tabs->tabData(i).toString() == id) {
        tabs->setCurrentIndex(i);
        QApplication::processEvents();
        return true;
      }
    }
    return false;
  };
  auto mode = [&](int value) {
    for (auto* action : window->findChildren<QAction*>()) {
      if (action->property("workspaceMode").toBool() && action->data().toInt() == value) {
        action->trigger();
        QApplication::processEvents();
        return true;
      }
    }
    return false;
  };
  auto* model = mods->model();
  check(model && model->rowCount() >= 3, "Real mod model loaded temporary fixture mods");
  navigate("plugins");
  for (const auto& listName : {"modList", "espList"}) {
    auto* tree = window->findChild<QTreeView*>(listName);
    if (!tree) { check(false, "Header available for resize check"); continue; }
    auto* header = tree->header();
    const auto saved = header->saveState();
    auto shrinkName = [&](const QString& layout) {
      check(QTest::qWaitFor([tree] { return tree->isEnabled(); }, 5000),
            QString("%1 is interactive before dragging").arg(listName));
      const int before = header->sectionSize(0);
      const QPoint edge(header->sectionViewportPosition(0) + before - 1, header->height() / 2);
      QTest::mouseMove(header->viewport(), edge);
      QTest::mousePress(header->viewport(), Qt::LeftButton, Qt::NoModifier, edge);
      QTest::mouseMove(header->viewport(), edge - QPoint(70, 0));
      QTest::mouseRelease(header->viewport(), Qt::LeftButton, Qt::NoModifier, edge - QPoint(70, 0));
      QApplication::processEvents();
      check(header->sectionSize(0) < before - 45,
            QString("%1 name divider shrinks left in %2 (%3 → %4 px)")
                .arg(listName, layout).arg(before).arg(header->sectionSize(0)));
    };
    shrinkName("default layout");
    int priority = -1;
    for (int i = 0; i < header->count(); ++i)
      if (tree->model()->headerData(i, Qt::Horizontal).toString() == "Priority") priority = i;
    check(priority >= 0, QString("%1 uses the Priority column label").arg(listName));
    if (priority >= 0) {
      header->restoreState(saved);
      for (int i = 0; i < header->count(); ++i)
        header->setSectionHidden(i, i != 0 && i != priority);
      QApplication::processEvents();
      shrinkName("Name / Priority layout");

      const auto scrollMode = tree->horizontalScrollMode();
      const auto scrollPolicy = tree->verticalScrollBarPolicy();
      tree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
      for (const auto scrolling : {QAbstractItemView::ScrollPerItem,
                                   QAbstractItemView::ScrollPerPixel}) {
        tree->setHorizontalScrollMode(scrolling);
        header->setStretchLastSection(false);
        header->setSectionResizeMode(QHeaderView::Interactive);
        header->resizeSection(0, tree->viewport()->width() + 180);
        header->resizeSection(priority, 260);
        header->setStretchLastSection(true);
        QApplication::processEvents();
        check(tree->horizontalScrollBar()->maximum() > 0,
              QString("%1 final-column fixture scrolls horizontally (%2)").arg(listName).arg(scrolling));
        tree->horizontalScrollBar()->setValue(tree->horizontalScrollBar()->maximum());
        QApplication::processEvents();
        const int before = header->sectionSize(priority);
        const int nameWidth = header->sectionSize(0);
        const int edge = header->sectionViewportPosition(priority) + before - 1;
        const QPoint from(qMin(edge, header->viewport()->rect().right()) - 5,
                          header->height() / 2);
        const bool reachable = header->viewport()->rect().contains(from);
        check(reachable, QString("%1 final-column grip is inside its visible header").arg(listName));
        if (!reachable) continue;
        if (scrolling == QAbstractItemView::ScrollPerPixel && QString(listName) == "modList")
          capture(window, "native-final-column-before");
        QTest::mouseMove(header->viewport(), from);
        check(header->viewport()->cursor().shape() == Qt::SplitHCursor,
              QString("%1 final-column grip shows a resize cursor").arg(listName));
        QTest::mousePress(header->viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(header->viewport(), from - QPoint(70, 0));
        QTest::mouseRelease(header->viewport(), Qt::LeftButton, Qt::NoModifier, from - QPoint(70, 0));
        QApplication::processEvents();
        check(header->sectionSize(priority) == before - 70 &&
                  header->sectionSize(0) == nameWidth && !header->stretchLastSection(),
              QString("%1 final Priority shrinks without resizing Name (%2 → %3 px, scroll mode %4)")
                  .arg(listName).arg(before).arg(header->sectionSize(priority)).arg(scrolling));
        if (scrolling == QAbstractItemView::ScrollPerPixel && QString(listName) == "modList")
          capture(window, "native-final-column-after");
        manualHeaders.insert(listName, header->saveState());
      }
      tree->setHorizontalScrollMode(scrollMode);
      tree->setVerticalScrollBarPolicy(scrollPolicy);
    }
    header->restoreState(saved);
  }
  if (auto* splitter = window->findChild<QSplitter*>("splitter")) {
    const auto saved = splitter->saveState();
    const int before = splitter->sizes().front();
    auto* handle = splitter->handle(1);
    const QPoint start = handle->rect().center();
    QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(handle, start - QPoint(90, 0));
    QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, start);
    QApplication::processEvents();
    check(splitter->sizes().front() < before - 50,
          "The divider between mods and the task pane can move left");
    splitter->restoreState(saved);
  }
  mode(0);
  QModelIndex selected;
  for (int row = 0; model && row < model->rowCount(); ++row) {
    const auto index = model->index(row, 0);
    if (index.data().toString().contains("Weather")) { selected = index; break; }
  }
  check(selected.isValid(), "Fixture mod found through actual proxy model");
  if (selected.isValid())
    mods->selectionModel()->select(selected, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  // The production inspector coalesces selection changes for 40ms.
  QTimer::singleShot(150, window, [window, mods, model, selected, tabs, pages] {
    auto* name = window->findChild<QLabel*>("selectedModPanelName");
    check(name && name->isVisible() && name->text().contains("Weather"),
          "Inspector reads actual selected-mod metadata");
    capture(window, "native-mods-details");
    auto navigate = [tabs](const QString& id) {
      for (int i = 0; i < tabs->count(); ++i)
        if (tabs->tabData(i).toString() == id) tabs->setCurrentIndex(i);
      QApplication::processEvents();
    };
    auto mode = [window](int value) {
      for (auto* action : window->findChildren<QAction*>())
        if (action->property("workspaceMode").toBool() && action->data().toInt() == value)
          action->trigger();
      QApplication::processEvents();
    };
    mode(1);
    check(tabs->isVisible(), "Split view presents the task tab bar");
    check(mods->isVisible(), "Split view retains mods");
    auto* plugins = window->findChild<QWidget*>("espList");
    check(plugins && plugins->isVisible(), "Split view shows real plugins");
    if (auto* tree = qobject_cast<QTreeView*>(mods)) {
      const auto* header = tree->header();
      const int priority = 11; // ModList::COL_PRIORITY, preserved model schema.
      check(header->sectionViewportPosition(priority) + header->sectionSize(priority)
                <= tree->viewport()->width(),
            "Priority remains visible in the default expert split");
    }
    capture(window, "native-mods-plugins");
    {
      // Watch the surrounding panes, not the individual pages that a tab
      // switch intentionally hides. Process frames during rapid switching so
      // a temporary collapse cannot hide behind a final-geometry assertion.
      QTest::qWait(50);
      PaneEvents events;
      for (const auto* object : {"categoriesGroup", "layoutWidget", "layoutWidget_2",
                                "tabWidget", "workspaceTaskTabs"})
        events.watch(window->findChild<QWidget*>(object));
      for (int cycle = 0; cycle < 6; ++cycle) {
        for (const auto* destination : {"downloads", "files", "archives", "saves", "plugins"}) {
          navigate(destination);
          QTest::qWait(20);
        }
      }
      check(events.visibilityChanges == 0 && events.resizes == 0,
            QString("30 tab switches leave pane shells stable (%1 visibility changes, %2 resizes)")
                .arg(events.visibilityChanges).arg(events.resizes));
    }
    navigate("downloads");
    check(mods->isVisible() && tabs->isVisible(), "Downloads stays beside the mod list");
    capture(window, "native-downloads");
    navigate("files");
    check(mods->isVisible(), "Game files stays beside the mod list");
    capture(window, "native-files");
    navigate("archives");
    check(mods->isVisible(), "Archives stays beside the mod list");
    capture(window, "native-archives");
    navigate("plugins");
    check(mods->model() == model, "Navigation preserves the actual model");
    check(!selected.isValid() || mods->selectionModel()->isSelected(selected),
          "Navigation preserves mod selection");
    mode(2);
    check(mods->isVisible() && (!plugins || !plugins->isVisible()), "Mods-only view");
    check(!tabs->isVisible(), "Mods-only view hides the task tabs");
    mode(1);
    navigate("downloads");
    mode(2);
    mode(1);
    check(tabs->tabData(tabs->currentIndex()).toString() == "downloads" && mods->isVisible(),
          "Reopening task tabs retains the last selected tab");
    window->resize(1000, 700);
    capture(window, "native-tabs-1000");
    check(window->width() == 1000 && tabs->isVisible() && mods->isVisible(),
          "Tabbed workspace can shrink to 1000px with both panes available");
    mode(0);
    window->resize(1000, 700);
    capture(window, "native-mods-1000");
    check(window->width() == 1000, "MainWindow can shrink to 1000px without header forcing overflow");
    window->resize(1300, 800);
    auto* library = window->findChild<QAction*>("actionChange_Game");
    check(library && library->isVisible(), "Library action available in unlocked setup");
    if (library && library->isVisible()) library->trigger();
    QApplication::processEvents();
    check(pages->currentWidget()->objectName() == "fluorineLibraryView", "Library is a native stack page");
    capture(window, "native-library");
    auto* open = window->findChild<QPushButton*>("openSelectedSetup");
    if (open) open->click();
    QApplication::processEvents();
    check(pages->currentWidget()->objectName() == "fluorineWorkspaceFrame", "Opening active setup returns without restart");
    check(mods->model() == model, "Library return preserves model identity");
    capture(window, "native-return");
    QModelIndex dlc;
    for (int row = 0; row < model->rowCount(); ++row) {
      auto index = model->index(row, 0);
      if (index.data().toString().contains("DeadMoney")) { dlc = index; break; }
    }
    check(dlc.isValid(), "Synthetic DLC loaded through the real game-content model");
    if (dlc.isValid())
      mods->selectionModel()->select(dlc, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    QTimer::singleShot(150, window, [window] {
      auto* name = window->findChild<QLabel*>("selectedModPanelName");
      auto* status = window->findChild<QLabel*>("selectedModPanelStatus");
      auto* empty = window->findChild<QLabel*>("selectedModPanelEmpty");
      check(name && name->isVisible() && name->text().contains("DeadMoney") &&
                status && status->text() == "Managed outside Fluorine" &&
                empty && !empty->isVisible(),
            "Selected DLC shows its identity instead of an empty-selection prompt");
      capture(window, "native-dlc-details");
      saveManualColumns(window);
      finish();
    });
  });
}

void installProbe()
{
  output = qEnvironmentVariable("FLUORINE_UI_SMOKE_DIR");
  if (output.isEmpty() || !QFile::exists(output + "/isolated-test-fixture")) return;
  QTimer::singleShot(0, qApp, [] {
    auto* timer = new QTimer(qApp);
    timer->setInterval(250);
    QObject::connect(timer, &QTimer::timeout, qApp, [timer, attempts = 0]() mutable {
      for (auto* widget : QApplication::topLevelWidgets()) {
        if (QByteArray(widget->metaObject()->className()) == "MainWindow" && widget->isVisible()) {
          timer->stop();
          auto* window = qobject_cast<QMainWindow*>(widget);
          QTimer::singleShot(1500, window, [window] { exercise(window); });
          return;
        }
      }
      if (++attempts == 160) {
        timer->stop();
        for (auto* widget : QApplication::topLevelWidgets()) {
          if (widget->isVisible()) widget->grab().save(output + "/blocked-" + widget->objectName() + ".png");
        }
        check(false, "MainWindow appeared within 40 seconds");
        finish();
      }
    });
    timer->start();
  });
}
}
Q_COREAPP_STARTUP_FUNCTION(installProbe)
