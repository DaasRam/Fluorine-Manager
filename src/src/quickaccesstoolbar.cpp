#include "quickaccesstoolbar.h"

#include <QAction>
#include <QActionGroup>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMetaType>
#include <QPushButton>
#include <QSet>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>

#include <utility>

QuickAccessToolbar::QuickAccessToolbar(QWidget* parent)
    : QToolBar(parent), m_actionOrder(defaultActionOrder())
{
  setAllowedAreas(Qt::TopToolBarArea);
  setMovable(false);
  setFloatable(false);
  setIconSize(QSize(20, 20));
  setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  setWindowTitle(tr("Quick Access Toolbar"));
  setContextMenuPolicy(Qt::CustomContextMenu);

  m_pinSeparator = new QAction(this);
  m_pinSeparator->setSeparator(true);

  connect(this, &QWidget::customContextMenuRequested, this,
          [this](const QPoint& position) {
            showCustomizationMenu(mapToGlobal(position));
          });
}

void QuickAccessToolbar::setAvailableActions(QList<QAction*> actions)
{
  m_availableActions.clear();

  QSet<QString> names;
  for (QAction* action : actions) {
    if (action == nullptr || action->objectName().isEmpty() ||
        names.contains(action->objectName())) {
      continue;
    }

    names.insert(action->objectName());
    m_availableActions.append(action);
  }

  rebuild();
}

void QuickAccessToolbar::setActionOrder(QStringList actionNames)
{
  QStringList uniqueNames;
  QSet<QString> seen;

  for (const QString& name : actionNames) {
    if (name.isEmpty() || seen.contains(name)) {
      continue;
    }

    seen.insert(name);
    uniqueNames.append(name);
  }

  if (uniqueNames == m_actionOrder) {
    return;
  }

  m_actionOrder = std::move(uniqueNames);
  rebuild();
  emit actionOrderChanged();
}

QStringList QuickAccessToolbar::actionOrder() const
{
  return m_actionOrder;
}

void QuickAccessToolbar::setPinnedActions(QList<QAction*> actions)
{
  m_pinnedActions.clear();

  QSet<QAction*> seen;
  for (QAction* action : actions) {
    if (action == nullptr || seen.contains(action)) {
      continue;
    }

    seen.insert(action);
    m_pinnedActions.append(action);
  }

  rebuild();
}

void QuickAccessToolbar::showCustomizationMenu(const QPoint& globalPos)
{
  QMenu* menu = createCustomizationMenu(this);

  QAction* clickedAction = actionAt(mapFromGlobal(globalPos));
  const QString title = executableTitle(clickedAction);
  if (!title.isEmpty() && !menu->actions().isEmpty()) {
    QAction* firstAction = menu->actions().constFirst();
    auto* unpinAction = new QAction(
        tr("Unpin %1 from Quick Access").arg(title), menu);
    menu->insertAction(firstAction, unpinAction);
    menu->insertSeparator(firstAction);

    // Keep only the executable title: unpinRequested may synchronously rebuild
    // the toolbar and delete the QAction that opened this context menu.
    connect(unpinAction, &QAction::triggered, menu,
            [this, title] { emit unpinRequested(title); });
  }

  menu->exec(globalPos);
  delete menu;
}

QMenu* QuickAccessToolbar::createCustomizationMenu(QWidget* parent)
{
  auto* menu = new QMenu(tr("Quick Access"), parent == nullptr ? this : parent);

  QAction* visibilityAction = toggleViewAction();
  menu->addAction(visibilityAction);
  menu->addSeparator();

  QAction* customizeAction = menu->addAction(QString());
  connect(customizeAction, &QAction::triggered, this,
          &QuickAccessToolbar::customize);

  QMenu* presetMenu = menu->addMenu(QString());
  auto* presetGroup = new QActionGroup(presetMenu);
  presetGroup->setExclusive(true);
  QAction* defaultsAction = presetMenu->addAction(QString());
  defaultsAction->setCheckable(true);
  presetGroup->addAction(defaultsAction);
  connect(defaultsAction, &QAction::triggered, this, [this] {
    setActionOrder(defaultActionOrder());
  });

  QAction* mo2Action = presetMenu->addAction(QString());
  mo2Action->setCheckable(true);
  presetGroup->addAction(mo2Action);
  connect(mo2Action, &QAction::triggered, this, [this] {
    setActionOrder(mo2StyleActionOrder());
  });

  QMenu* styleMenu = menu->addMenu(QString());
  auto* styleGroup = new QActionGroup(styleMenu);
  styleGroup->setExclusive(true);
  auto addStyleAction = [this, styleMenu, styleGroup](Qt::ToolButtonStyle style) {
    QAction* action = styleMenu->addAction(QString());
    action->setCheckable(true);
    styleGroup->addAction(action);
    connect(action, &QAction::triggered, this,
            [this, style] { setToolButtonStyle(style); });
    return action;
  };

  QAction* iconOnlyAction = addStyleAction(Qt::ToolButtonIconOnly);
  QAction* besideIconAction = addStyleAction(Qt::ToolButtonTextBesideIcon);
  QAction* underIconAction = addStyleAction(Qt::ToolButtonTextUnderIcon);
  QAction* textOnlyAction = addStyleAction(Qt::ToolButtonTextOnly);

  QMenu* sizeMenu = menu->addMenu(QString());
  auto* sizeGroup = new QActionGroup(sizeMenu);
  sizeGroup->setExclusive(true);
  auto addSizeAction = [this, sizeMenu, sizeGroup](int size) {
    QAction* action = sizeMenu->addAction(QString());
    action->setCheckable(true);
    sizeGroup->addAction(action);
    connect(action, &QAction::triggered, this,
            [this, size] { setIconSize(QSize(size, size)); });
    return action;
  };

  QAction* smallAction = addSizeAction(20);
  QAction* mediumAction = addSizeAction(24);
  QAction* largeAction = addSizeAction(32);

  // The View menu owns this QMenu for the lifetime of the window. Refresh its
  // labels when opened so language changes are reflected without rebuilding it.
  connect(menu, &QMenu::aboutToShow, this,
          [this, menu, visibilityAction, customizeAction, presetMenu,
           defaultsAction, mo2Action, styleMenu, iconOnlyAction, besideIconAction,
           underIconAction, textOnlyAction, sizeMenu, smallAction, mediumAction,
           largeAction] {
            menu->setTitle(tr("Quick Access"));
            visibilityAction->setText(tr("Quick Access Toolbar"));
            customizeAction->setText(tr("Customize…"));

            presetMenu->setTitle(tr("Preset"));
            defaultsAction->setText(tr("Fluorine defaults"));
            mo2Action->setText(tr("MO2-style shortcuts"));
            defaultsAction->setChecked(m_actionOrder == defaultActionOrder());
            mo2Action->setChecked(m_actionOrder == mo2StyleActionOrder());

            styleMenu->setTitle(tr("Button style"));
            iconOnlyAction->setText(tr("Icons only"));
            besideIconAction->setText(tr("Text beside icons"));
            underIconAction->setText(tr("Text under icons"));
            textOnlyAction->setText(tr("Text only"));
            iconOnlyAction->setChecked(toolButtonStyle() == Qt::ToolButtonIconOnly);
            besideIconAction->setChecked(toolButtonStyle() == Qt::ToolButtonTextBesideIcon);
            underIconAction->setChecked(toolButtonStyle() == Qt::ToolButtonTextUnderIcon);
            textOnlyAction->setChecked(toolButtonStyle() == Qt::ToolButtonTextOnly);

            sizeMenu->setTitle(tr("Icon size"));
            smallAction->setText(tr("Small (20 px)"));
            mediumAction->setText(tr("Medium (24 px)"));
            largeAction->setText(tr("Large (32 px)"));
            smallAction->setChecked(iconSize() == QSize(20, 20));
            mediumAction->setChecked(iconSize() == QSize(24, 24));
            largeAction->setChecked(iconSize() == QSize(32, 32));
          });

  if (auto* parentMenu = qobject_cast<QMenu*>(parent)) {
    connect(parentMenu, &QMenu::aboutToShow, this, [this, menu] {
      menu->setTitle(tr("Quick Access"));
    });
  }

  menu->addSeparator();
  QMenu* pinsMenu = menu->addMenu(tr("Pinned Programs"));
  connect(menu, &QMenu::aboutToShow, this, [this, pinsMenu] {
    pinsMenu->setTitle(tr("Pinned Programs"));
    pinsMenu->setEnabled(!m_pinnedActions.isEmpty());
  });
  connect(pinsMenu, &QMenu::aboutToShow, this, [this, pinsMenu] {
    pinsMenu->clear();
    for (QAction* action : std::as_const(m_pinnedActions)) {
      const QString title = executableTitle(action);
      if (!title.isEmpty()) {
        pinsMenu->addAction(tr("Unpin %1").arg(title), this,
                           [this, title] { emit unpinRequested(title); });
      }
    }
  });

  return menu;
}

void QuickAccessToolbar::customize()
{
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Customize Quick Access Toolbar"));
  dialog.setModal(true);

  auto* list = new QListWidget(&dialog);
  list->setAccessibleName(tr("Quick Access commands"));
  list->setDragDropMode(QAbstractItemView::InternalMove);
  list->setDefaultDropAction(Qt::MoveAction);
  list->setDropIndicatorShown(true);
  list->setDragDropOverwriteMode(false);

  auto* instruction = new QLabel(
      tr("Choose the commands to show. Drag them or use Move Up/Down to change their order."),
      &dialog);
  instruction->setWordWrap(true);

  QSet<QString> selectedNames;
  for (const QString& name : m_actionOrder) {
    selectedNames.insert(name);
  }

  QSet<QString> addedNames;
  auto addActionItem = [list, &selectedNames, &addedNames](QAction* action) {
    if (action == nullptr || action->objectName().isEmpty() ||
        addedNames.contains(action->objectName())) {
      return;
    }

    addedNames.insert(action->objectName());
    QString displayText;
    const QString actionText = action->text();
    for (qsizetype i = 0; i < actionText.size(); ++i) {
      if (actionText.at(i) == QLatin1Char('&')) {
        if (i + 1 < actionText.size() &&
            actionText.at(i + 1) == QLatin1Char('&')) {
          displayText += QLatin1Char('&');
          ++i;
        }
        continue;
      }
      displayText += actionText.at(i);
    }

    if (!action->isVisible()) {
      displayText += tr(" (currently unavailable)");
    }
    auto* item = new QListWidgetItem(action->icon(), displayText, list);
    if (!action->isVisible()) {
      item->setToolTip(tr("This command will appear when it is available for the current game or configuration."));
    }
    item->setData(Qt::UserRole, action->objectName());
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                   Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled |
                   Qt::ItemIsUserCheckable);
    item->setCheckState(selectedNames.contains(action->objectName())
                            ? Qt::Checked
                            : Qt::Unchecked);
  };

  // Show selected entries in their active order first. Then offer every
  // remaining action, including actions currently hidden by game/plugin state.
  for (const QString& name : m_actionOrder) {
    for (QAction* action : m_availableActions) {
      if (action != nullptr && action->objectName() == name) {
        addActionItem(action);
        break;
      }
    }
  }
  for (QAction* action : m_availableActions) {
    addActionItem(action);
  }

  auto* upButton = new QPushButton(tr("Move Up"), &dialog);
  auto* downButton = new QPushButton(tr("Move Down"), &dialog);
  auto moveCurrentItem = [list](int offset) {
    const int row = list->currentRow();
    const int nextRow = row + offset;
    if (row < 0 || nextRow < 0 || nextRow >= list->count()) {
      return;
    }

    QListWidgetItem* item = list->takeItem(row);
    list->insertItem(nextRow, item);
    list->setCurrentItem(item);
    list->setFocus();
  };
  connect(upButton, &QPushButton::clicked, &dialog,
          [moveCurrentItem] { moveCurrentItem(-1); });
  connect(downButton, &QPushButton::clicked, &dialog,
          [moveCurrentItem] { moveCurrentItem(1); });

  auto* moveLayout = new QVBoxLayout;
  moveLayout->addWidget(upButton);
  moveLayout->addWidget(downButton);
  moveLayout->addStretch();

  auto* listLayout = new QHBoxLayout;
  listLayout->addWidget(list, 1);
  listLayout->addLayout(moveLayout);

  auto* buttonBox = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

  auto* layout = new QVBoxLayout(&dialog);
  layout->addWidget(instruction);
  layout->addLayout(listLayout);
  layout->addWidget(buttonBox);

  dialog.resize(440, 420);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }

  QStringList selectedOrder;
  for (int row = 0; row < list->count(); ++row) {
    const QListWidgetItem* item = list->item(row);
    if (item->checkState() == Qt::Checked) {
      selectedOrder.append(item->data(Qt::UserRole).toString());
    }
  }

  setActionOrder(std::move(selectedOrder));
}

QStringList QuickAccessToolbar::defaultActionOrder()
{
  return {QStringLiteral("action_Refresh"), QStringLiteral("actionModPage"),
          QStringLiteral("actionTool"), QStringLiteral("actionSettings"),
          QStringLiteral("actionNotifications")};
}

QStringList QuickAccessToolbar::mo2StyleActionOrder()
{
  return {QStringLiteral("actionChange_Game"), QStringLiteral("actionInstallMod"),
          QStringLiteral("actionModPage"), QStringLiteral("actionAdd_Profile"),
          QStringLiteral("action_Refresh"),
          QStringLiteral("actionModify_Executables"), QStringLiteral("actionTool"),
          QStringLiteral("actionSettings"), QStringLiteral("actionNotifications"),
          QStringLiteral("actionUpdate"), QStringLiteral("actionHelp")};
}

QString QuickAccessToolbar::executableTitle(const QAction* action)
{
  if (action == nullptr) {
    return {};
  }

  const QVariant value = action->property("quickAccessExecutable");
  if (!value.isValid() || value.isNull()) {
    return {};
  }

  if (value.metaType().id() == QMetaType::Bool) {
    if (!value.toBool()) {
      return {};
    }

    return action->text();
  }

  const QString title = value.toString();
  return title.isEmpty() ? action->text() : title;
}

void QuickAccessToolbar::rebuild()
{
  for (const QMetaObject::Connection& connection :
       std::as_const(m_buttonContextConnections)) {
    QObject::disconnect(connection);
  }
  m_buttonContextConnections.clear();

  for (QAction* action : std::as_const(m_renderedActions)) {
    if (action != nullptr) {
      removeAction(action);
    }
  }
  m_renderedActions.clear();
  removeAction(m_pinSeparator);

  QHash<QString, QAction*> availableByName;
  for (QAction* action : std::as_const(m_availableActions)) {
    if (action != nullptr && !action->objectName().isEmpty()) {
      availableByName.insert(action->objectName(), action);
    }
  }

  QSet<QAction*> addedActions;
  int commandCount = 0;
  for (const QString& name : std::as_const(m_actionOrder)) {
    QAction* action = availableByName.value(name, nullptr);
    if (action == nullptr || addedActions.contains(action)) {
      continue;
    }

    addAction(action);
    configureActionButton(action);
    m_renderedActions.append(action);
    addedActions.insert(action);
    ++commandCount;
  }

  if (commandCount > 0 && !m_pinnedActions.isEmpty()) {
    addAction(m_pinSeparator);
  }

  for (QAction* action : std::as_const(m_pinnedActions)) {
    if (action == nullptr || addedActions.contains(action)) {
      continue;
    }

    addAction(action);
    configureActionButton(action);
    m_renderedActions.append(action);
    addedActions.insert(action);
  }
}

void QuickAccessToolbar::configureActionButton(QAction* action)
{
  auto* button = qobject_cast<QToolButton*>(widgetForAction(action));
  if (button == nullptr) {
    return;
  }

  // Match MO2's behavior for menu-bearing toolbar commands: clicking anywhere
  // on the labeled button opens its menu instead of requiring a tiny arrow hit.
  if (action->menu() != nullptr) {
    button->setPopupMode(QToolButton::InstantPopup);
  }

  button->setContextMenuPolicy(Qt::CustomContextMenu);
  m_buttonContextConnections.append(connect(
      button, &QWidget::customContextMenuRequested, this,
      [this, button](const QPoint& position) {
        showCustomizationMenu(button->mapToGlobal(position));
      }));
}
