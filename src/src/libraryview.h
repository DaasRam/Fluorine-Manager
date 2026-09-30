#ifndef MODORGANIZER_LIBRARYVIEW_INCLUDED
#define MODORGANIZER_LIBRARYVIEW_INCLUDED

#include <QWidget>
#include <QStringList>
#include <cstddef>
#include <filterwidget.h>
#include <memory>
#include <vector>

class Instance;
class PluginContainer;
class QStandardItemModel;
class QResizeEvent;
class QEvent;

namespace Ui
{
struct LibraryViewUi;
}

// Native, embeddable view for browsing and managing Fluorine setups.
class LibraryView : public QWidget
{
  Q_OBJECT

public:
  explicit LibraryView(PluginContainer& pc, QWidget* parent = nullptr);
  ~LibraryView() override;

  // Refreshes the discovered setup list while preserving the current selection
  // where possible.
  void refresh();

  void select(std::size_t index);
  void select(const QString& name);
  bool selectByPath(const QString& path);
  void selectActiveInstance();
  // Gives the return action the close/cancel wording used by the startup dialog.
  void setModalPresentation(bool modal);

  // Activates the selected setup using the existing restart lifecycle. Opening
  // the already active setup requests a return to its workspace instead.
  void openSelectedInstance();
  void setRestartOnSelect(bool restart);

  // Reusable library actions, also invoked by the view's own buttons.
  void createNew();
  void openExistingPortable();
  void installWabbajack();

  // Existing setup detail actions, retained for InstanceManagerDialog clients.
  void rename();
  void exploreLocation();
  void exploreBaseDirectory();
  void exploreGame();
  void convertToGlobal();
  void convertToPortable();
  void openINI();
  void removeFromList();
  void deleteInstance();

signals:
  // The active setup was opened; an embedding shell should return to its
  // workspace without restarting or changing OrganizerCore.
  void returnToSetupRequested();

  // The view's Back to workspace action was invoked.
  void closeRequested();

  // A selection/create action has committed a setup choice and the legacy
  // modal wrapper should accept. A new main-window host can ignore this signal;
  // switching setups already schedules the application's normal restart.
  void setupSelectionAccepted();

protected:
  void resizeEvent(QResizeEvent* event) override;
  void changeEvent(QEvent* event) override;

private:
  static constexpr std::size_t NoSelection = static_cast<std::size_t>(-1);

  std::unique_ptr<Ui::LibraryViewUi> ui;
  PluginContainer& m_pc;
  std::vector<std::unique_ptr<Instance>> m_instances;
  MOBase::FilterWidget m_filter;
  QStandardItemModel* m_model{nullptr};
  bool m_restartOnSelect{true};
  bool m_verticalSplitter{false};

  void updateInstances();
  void onSelection();
  bool confirmSwitch(const Instance& to);
  std::size_t singleSelectionIndex() const;
  const Instance* singleSelection() const;
  void updateList();
  void fillData(const Instance& instance);
  void clearData();
  void setButtonsEnabled(bool enabled);
  bool doDelete(const QStringList& files, bool recycle);
  void downloadSLRIfNeeded();
  void updateResponsiveLayout();
  void updatePendingInstallation();
  void openModlistInstaller(bool resumePending);
};

#endif  // MODORGANIZER_LIBRARYVIEW_INCLUDED
