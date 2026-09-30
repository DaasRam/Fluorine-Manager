#ifndef MODORGANIZER_INSTANCEMANAGERDIALOG_INCLUDED
#define MODORGANIZER_INSTANCEMANAGERDIALOG_INCLUDED

#include <QDialog>
#include <cstddef>
#include <memory>

namespace Ui
{
class InstanceManagerDialog;
}

class Instance;
class LibraryView;
class PluginContainer;
class QShowEvent;

// Modal compatibility wrapper around the reusable LibraryView.
class InstanceManagerDialog : public QDialog
{
  Q_OBJECT

public:
  explicit InstanceManagerDialog(PluginContainer& pc, QWidget* parent = nullptr);
  ~InstanceManagerDialog() override;

  void select(std::size_t index);
  void select(const QString& name);
  void selectActiveInstance();
  void openSelectedInstance();
  void rename();
  void exploreLocation();
  void exploreBaseDirectory();
  void exploreGame();
  void convertToGlobal();
  void convertToPortable();
  void openINI();
  void removeFromList();
  void deleteInstance();
  void setRestartOnSelect(bool restart);

  void done(int result) override;

protected:
  void showEvent(QShowEvent* event) override;

private:
  std::unique_ptr<Ui::InstanceManagerDialog> ui;
  LibraryView* m_libraryView{nullptr};
};

#endif  // MODORGANIZER_INSTANCEMANAGERDIALOG_INCLUDED
