#include "instancemanagerdialog.h"
#include "fluorinetheme.h"
#include "libraryview.h"
#include "plugincontainer.h"
#include "settings.h"
#include "ui_instancemanagerdialog.h"

#include <QShowEvent>
#include <QVBoxLayout>

InstanceManagerDialog::InstanceManagerDialog(PluginContainer& pc, QWidget* parent)
    : QDialog(parent), ui(new Ui::InstanceManagerDialog)
{
  ui->setupUi(this);
  m_libraryView = new LibraryView(pc, this);
  m_libraryView->setObjectName(QStringLiteral("fluorineLibraryView"));
  m_libraryView->setModalPresentation(true);
  FluorineTheme::apply(m_libraryView);
  ui->libraryContentLayout->addWidget(m_libraryView);

  connect(m_libraryView, &LibraryView::returnToSetupRequested,
          this, &QDialog::accept);
  connect(m_libraryView, &LibraryView::setupSelectionAccepted,
          this, &QDialog::accept);
  connect(m_libraryView, &LibraryView::closeRequested,
          this, &QDialog::reject);
}

InstanceManagerDialog::~InstanceManagerDialog() = default;

void InstanceManagerDialog::select(std::size_t index)
{
  m_libraryView->select(index);
}

void InstanceManagerDialog::select(const QString& name)
{
  m_libraryView->select(name);
}

void InstanceManagerDialog::selectActiveInstance()
{
  m_libraryView->selectActiveInstance();
}

void InstanceManagerDialog::openSelectedInstance()
{
  m_libraryView->openSelectedInstance();
}

void InstanceManagerDialog::rename()
{
  m_libraryView->rename();
}

void InstanceManagerDialog::exploreLocation()
{
  m_libraryView->exploreLocation();
}

void InstanceManagerDialog::exploreBaseDirectory()
{
  m_libraryView->exploreBaseDirectory();
}

void InstanceManagerDialog::exploreGame()
{
  m_libraryView->exploreGame();
}

void InstanceManagerDialog::convertToGlobal()
{
  m_libraryView->convertToGlobal();
}

void InstanceManagerDialog::convertToPortable()
{
  m_libraryView->convertToPortable();
}

void InstanceManagerDialog::openINI()
{
  m_libraryView->openINI();
}

void InstanceManagerDialog::removeFromList()
{
  m_libraryView->removeFromList();
}

void InstanceManagerDialog::deleteInstance()
{
  m_libraryView->deleteInstance();
}

void InstanceManagerDialog::setRestartOnSelect(bool restart)
{
  m_libraryView->setRestartOnSelect(restart);
}

void InstanceManagerDialog::done(int result)
{
  // There might not be a global Settings object when shown before setup init.
  if (auto* settings = Settings::maybeInstance()) {
    settings->geometry().saveGeometry(this);
  }
  QDialog::done(result);
}

void InstanceManagerDialog::showEvent(QShowEvent* event)
{
  // There might not be a global Settings object when shown before setup init.
  if (const auto* settings = Settings::maybeInstance()) {
    settings->geometry().restoreGeometry(this);
  }
  QDialog::showEvent(event);
}
