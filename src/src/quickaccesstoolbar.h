#ifndef QUICKACCESSTOOLBAR_H
#define QUICKACCESSTOOLBAR_H

#include <QList>
#include <QMetaObject>
#include <QStringList>
#include <QToolBar>

class QAction;
class QMenu;
class QPoint;

/**
 * Compact, user-configurable toolbar for existing MainWindow actions.
 *
 * The toolbar never owns or clones the command actions supplied through
 * setAvailableActions() and setPinnedActions(). actionOrder() contains the
 * object names of the selected command actions, in display order. Pinned
 * executable actions are displayed after a separator and are managed by the
 * caller; when a pin is right-clicked, unpinRequested() reports its title.
 */
class QuickAccessToolbar : public QToolBar
{
  Q_OBJECT

public:
  explicit QuickAccessToolbar(QWidget* parent = nullptr);

  void setAvailableActions(QList<QAction*> actions);
  void setActionOrder(QStringList actionNames);
  QStringList actionOrder() const;
  void setPinnedActions(QList<QAction*> actions);

  void showCustomizationMenu(const QPoint& globalPos);
  QMenu* createCustomizationMenu(QWidget* parent = nullptr);
  void customize();

signals:
  void actionOrderChanged();
  void unpinRequested(const QString& executableTitle);

private:
  static QStringList defaultActionOrder();
  static QStringList mo2StyleActionOrder();
  static QString executableTitle(const QAction* action);

  void rebuild();
  void configureActionButton(QAction* action);

  QList<QAction*> m_availableActions;
  QList<QAction*> m_pinnedActions;
  QList<QAction*> m_renderedActions;
  QList<QMetaObject::Connection> m_buttonContextConnections;
  QStringList m_actionOrder;
  QAction* m_pinSeparator{nullptr};
};

#endif  // QUICKACCESSTOOLBAR_H
