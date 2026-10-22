#pragma once

#include "clf3enginemanager.h"

#include <QByteArray>
#include <QProcess>
#include <QString>
#include <QWidget>
#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;

// "OpenMW" tab of the installer window: browses the modding-openmw.com lists
// and shows what installing one would do. Everything comes from the managed
// CLF3 engine (`clf3 momw lists` / `clf3 momw plan --json`), which only reads
// public site data, so no credentials are involved. Installing is not wired up
// yet; the page says so instead of offering a button that does nothing.
class Clf3MomwPage : public QWidget
{
  Q_OBJECT
public:
  explicit Clf3MomwPage(QWidget* parent = nullptr);
  ~Clf3MomwPage() override;

  // Called with (list slug, list title) when Install is pressed. Until a handler
  // is set the page stays a read-only preview.
  void setInstallHandler(std::function<void(const QString&, const QString&)> handler);

protected:
  void showEvent(QShowEvent* event) override;

private:
  enum class Mode
  {
    None,
    Lists,
    Plan
  };

  Clf3EngineManager m_engine;
  QProcess m_process;
  QByteArray m_output;
  QByteArray m_errors;
  QString m_enginePath;
  Mode m_mode{Mode::None};
  bool m_started{false};
  bool m_listsLoaded{false};

  QComboBox* m_lists{};
  QPushButton* m_review{};
  QPushButton* m_refresh{};
  QCheckBox* m_showAll{};
  QLabel* m_status{};
  QLabel* m_summary{};
  QTreeWidget* m_entries{};
  QPlainTextEdit* m_notes{};
  QPushButton* m_installButton{};
  QLabel* m_installNote{};
  std::function<void(const QString&, const QString&)> m_installHandler;
  bool m_planInstallable{false};

  void buildUi();
  void ensureEngine();
  void run(Mode mode, const QStringList& arguments);
  void setBusy(bool busy, const QString& message = {});
  void finished(int exitCode, QProcess::ExitStatus status);
  void showLists(const QByteArray& json);
  void showPlan(const QByteArray& json);
  void reviewSelected();
  void clearPlan();
  void updateInstallButton();
};
