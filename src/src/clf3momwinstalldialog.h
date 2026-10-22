#pragma once

#include "clf3processcontroller.h"

#include <QDialog>
#include <QQueue>
#include <QSet>
#include <optional>

class InstallProgressView;
class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;

// Installs one modding-openmw.com list as a new Fluorine instance. CLF3 does
// the work; this window supplies the paths, shows progress, and answers CLF3's
// Nexus requests with the user's own Nexus session (a direct link for Premium
// accounts, the "Mod Manager Download" click for everyone else).
class Clf3MomwInstallDialog : public QDialog
{
  Q_OBJECT

public:
  Clf3MomwInstallDialog(const QString& slug, const QString& title,
                        QWidget* parent = nullptr);
  ~Clf3MomwInstallDialog() override;

  QString createdInstanceDir() const { return m_createdInstanceDir; }
  bool switchRequested() const { return m_switchRequested; }
  void done(int result) override;

private:
  struct NexusRequest
  {
    QString requestId;
    QString archiveName;
    QString domain;
    int modId{0};
    int fileId{0};
  };

  QString m_slug;
  QString m_title;
  Clf3ProcessController m_controller;

  QStackedWidget* m_pages{};
  QLineEdit* m_instanceDir{};
  QLineEdit* m_gameDir{};
  QLabel* m_setupNote{};
  QPushButton* m_install{};
  InstallProgressView* m_progress{};
  QLabel* m_status{};
  QPlainTextEdit* m_log{};
  QPushButton* m_cancel{};
  QPushButton* m_close{};
  QPushButton* m_openInstance{};

  QQueue<NexusRequest> m_queue;
  std::optional<NexusRequest> m_current;
  QSet<QString> m_waiting;
  QDialog* m_browser{};
  QString m_createdInstanceDir;
  bool m_switchRequested{false};
  bool m_stopping{false};
  std::optional<int> m_deferredClose;

  void buildUi();
  void connectController();
  QString detectMorrowind() const;
  bool signedInToNexus();
  void start();
  void finish(const QJsonObject& stats);
  void fail(const QString& reason);

  void queueNexus(const QString& requestId, const QString& archiveName,
                  const QString& domain, int modId, int fileId);
  void beginNextNexus();
  void showBrowser(const NexusRequest& request);
  void resolveNexus(const NexusRequest& request, const QString& nxmUrl = {});
  void finishCurrentNexus(const QString& requestId);
  void listFiles(const QString& requestId, const QString& domain, int modId);
  void clearNexus();
};
