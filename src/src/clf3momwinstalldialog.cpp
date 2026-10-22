#include "clf3momwinstalldialog.h"
#include "curatedguidenxmbroker.h"
#include "gamedetection.h"
#include "installprogressview.h"
#include "instancemanager.h"
#include "nexusinterface.h"
#include "nxmaccessmanager.h"
#include "nexuswebauth.h"
#include "settings.h"
#include "settingsdialognexus.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <nxmurl.h>

#ifdef MO2_WEBENGINE
#include <QWebEngineCookieStore>
#include <QWebEngineProfile>
#include <QWebEngineView>
#endif

namespace
{
const QString kConsumer = QStringLiteral("momw");

QString defaultInstanceDir(const QString& title)
{
  QString name = title;
  name.replace(QRegularExpression("[/\\\\:*?\"<>|]"), "-");
  return QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation))
      .filePath(QStringLiteral("Games/") + name.trimmed());
}
}  // namespace

Clf3MomwInstallDialog::Clf3MomwInstallDialog(const QString& slug, const QString& title,
                                             QWidget* parent)
    : QDialog(parent), m_slug(slug), m_title(title)
{
  setWindowTitle(tr("Install %1").arg(title));
  resize(900, 640);
  buildUi();
  connectController();
}

Clf3MomwInstallDialog::~Clf3MomwInstallDialog()
{
  CuratedGuideNxmBroker::instance().clearConsumer(kConsumer);
  if (m_controller.isRunning()) m_controller.cancel();
}

void Clf3MomwInstallDialog::buildUi()
{
  auto* root = new QVBoxLayout(this);
  m_pages    = new QStackedWidget;
  root->addWidget(m_pages);

  // Page 0: where to put it.
  auto* setup       = new QWidget;
  auto* setupLayout = new QVBoxLayout(setup);
  auto* heading     = new QLabel(tr("Install %1").arg(m_title));
  QFont headingFont = heading->font();
  headingFont.setPointSize(headingFont.pointSize() + 5);
  headingFont.setBold(true);
  heading->setFont(headingFont);
  setupLayout->addWidget(heading);
  auto* intro = new QLabel(
      tr("Fluorine downloads every mod in the list, builds the merged plugins, "
         "groundcover, light fixes and navigation meshes, and sets up an instance "
         "that opens with this list's own configuration. Downloads are shared with "
         "your other OpenMW lists."));
  intro->setWordWrap(true);
  setupLayout->addWidget(intro);

  auto* form = new QFormLayout;
  m_instanceDir = new QLineEdit(defaultInstanceDir(m_title));
  auto* browseInstance = new QPushButton(tr("Browse…"));
  auto* instanceRow    = new QHBoxLayout;
  instanceRow->addWidget(m_instanceDir, 1);
  instanceRow->addWidget(browseInstance);
  form->addRow(tr("Instance folder"), instanceRow);
  m_gameDir = new QLineEdit(detectMorrowind());
  auto* browseGame = new QPushButton(tr("Browse…"));
  auto* gameRow    = new QHBoxLayout;
  gameRow->addWidget(m_gameDir, 1);
  gameRow->addWidget(browseGame);
  form->addRow(tr("Morrowind folder"), gameRow);
  setupLayout->addLayout(form);
  m_setupNote = new QLabel;
  m_setupNote->setWordWrap(true);
  setupLayout->addWidget(m_setupNote);
  setupLayout->addStretch(1);
  auto* setupButtons = new QHBoxLayout;
  setupButtons->addStretch(1);
  auto* setupCancel = new QPushButton(tr("Cancel"));
  m_install         = new QPushButton(tr("Install"));
  m_install->setDefault(true);
  setupButtons->addWidget(setupCancel);
  setupButtons->addWidget(m_install);
  setupLayout->addLayout(setupButtons);
  m_pages->addWidget(setup);

  // Page 1: progress.
  auto* run       = new QWidget;
  auto* runLayout = new QVBoxLayout(run);
  m_status        = new QLabel;
  m_status->setWordWrap(true);
  runLayout->addWidget(m_status);
  m_progress = new InstallProgressView;
  runLayout->addWidget(m_progress, 1);
  m_log = new QPlainTextEdit;
  m_log->setReadOnly(true);
  m_log->setMaximumBlockCount(5000);
  m_log->setMaximumHeight(140);
  runLayout->addWidget(m_log);
  auto* runButtons = new QHBoxLayout;
  runButtons->addStretch(1);
  m_cancel       = new QPushButton(tr("Stop"));
  m_openInstance = new QPushButton(tr("Open this instance"));
  m_openInstance->hide();
  m_close = new QPushButton(tr("Close"));
  m_close->setEnabled(false);
  runButtons->addWidget(m_cancel);
  runButtons->addWidget(m_openInstance);
  runButtons->addWidget(m_close);
  runLayout->addLayout(runButtons);
  m_pages->addWidget(run);

  connect(browseInstance, &QPushButton::clicked, this, [this] {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Choose the folder for this instance"),
        QFileInfo(m_instanceDir->text()).absolutePath());
    if (!dir.isEmpty()) m_instanceDir->setText(QDir::cleanPath(dir));
  });
  connect(browseGame, &QPushButton::clicked, this, [this] {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Choose your Morrowind folder (the one containing Data Files)"),
        m_gameDir->text());
    if (!dir.isEmpty()) m_gameDir->setText(QDir::cleanPath(dir));
  });
  connect(setupCancel, &QPushButton::clicked, this, &QDialog::reject);
  connect(m_install, &QPushButton::clicked, this, &Clf3MomwInstallDialog::start);
  connect(m_cancel, &QPushButton::clicked, this, [this] {
    if (!m_controller.isRunning() || m_stopping) return;
    m_stopping = true;
    m_cancel->setEnabled(false);
    m_status->setText(tr("Stopping… Downloaded archives are kept, so installing again "
                         "resumes where this stopped."));
    clearNexus();
    m_controller.cancel();
  });
  connect(m_close, &QPushButton::clicked, this, &QDialog::accept);
  connect(m_openInstance, &QPushButton::clicked, this, [this] {
    m_switchRequested = true;
    accept();
  });
  connect(m_progress, &InstallProgressView::attentionActivated, this,
          [this](const QString&) {
            if (m_browser) {
              m_browser->show();
              m_browser->raise();
            }
          });
}

QString Clf3MomwInstallDialog::detectMorrowind() const
{
  for (const auto& game : detectAllGames().games) {
    if (game.name.compare(QStringLiteral("Morrowind"), Qt::CaseInsensitive) != 0) continue;
    if (QFileInfo(QDir(game.install_path).filePath("Data Files")).isDir())
      return QDir::cleanPath(game.install_path);
  }
  return {};
}

bool Clf3MomwInstallDialog::signedInToNexus()
{
  if (NexusInterface::instance().getAPIUserAccount().type() != APIUserAccountTypes::None)
    return true;
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Connect to Nexus"));
  dialog.resize(580, 360);
  auto* layout      = new QVBoxLayout(&dialog);
  auto* explanation = new QLabel(tr("Connect your Nexus account to download the list's mods."));
  explanation->setWordWrap(true);
  layout->addWidget(explanation);
  auto* buttons          = new QHBoxLayout;
  auto* connectButton    = new QPushButton(tr("Connect to Nexus"));
  auto* manualButton     = new QPushButton(tr("Enter API key manually"));
  auto* disconnectButton = new QPushButton(tr("Disconnect"));
  buttons->addWidget(connectButton);
  buttons->addWidget(manualButton);
  buttons->addWidget(disconnectButton);
  layout->addLayout(buttons);
  auto* log = new QListWidget;
  layout->addWidget(log);
  auto* close = new QDialogButtonBox(QDialogButtonBox::Close);
  layout->addWidget(close);
  connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  NexusConnectionUI connection(&dialog, Settings::maybeInstance(), connectButton,
                               disconnectButton, manualButton, log);
  dialog.exec();
  return NexusInterface::instance().getAPIUserAccount().type() != APIUserAccountTypes::None;
}

void Clf3MomwInstallDialog::start()
{
  const QString instance = QDir::cleanPath(m_instanceDir->text().trimmed());
  const QString game     = QDir::cleanPath(m_gameDir->text().trimmed());
  if (instance.isEmpty() || QFileInfo(instance).isFile()
      || (QFileInfo(instance).isDir() && !QDir(instance).isEmpty())) {
    m_setupNote->setText(tr("Choose a new or empty folder for the instance."));
    return;
  }
  if (game.isEmpty() || !QFileInfo(QDir(game).filePath("Data Files")).isDir()) {
    m_setupNote->setText(
        tr("Choose your Morrowind folder: it must contain a Data Files folder."));
    return;
  }
  if (!signedInToNexus()) {
    m_setupNote->setText(tr("Sign in to Nexus Mods to download the list's mods."));
    return;
  }
  m_stopping = false;
  m_createdInstanceDir.clear();
  m_pages->setCurrentIndex(1);
  m_progress->reset();
  m_progress->setPhase(tr("Preparing installation"));
  m_log->clear();
  m_status->setText(tr("Starting the installer…"));
  m_close->setEnabled(false);
  m_cancel->setEnabled(true);
  m_createdInstanceDir = instance;
  m_controller.startMomwInstall(m_slug, instance, game);
}

void Clf3MomwInstallDialog::connectController()
{
  connect(&m_controller, &Clf3ProcessController::phaseChanged, m_progress,
          &InstallProgressView::setPhase);
  connect(&m_controller, &Clf3ProcessController::statusChanged, this,
          [this](const QString& status) {
            if (!m_stopping) m_status->setText(status);
          });
  connect(&m_controller, &Clf3ProcessController::itemStarted, this,
          [this](const QString& id, const QString& name, const QString& displayName,
                 const QString& subtitle, const QString& stage, const QString&,
                 qint64 total, const QString& unit) {
            m_progress->startItem(id, name, displayName, subtitle, stage, total, unit);
          });
  connect(&m_controller, &Clf3ProcessController::itemProgress, m_progress,
          &InstallProgressView::progressItem);
  connect(&m_controller, &Clf3ProcessController::itemMessage, m_progress,
          &InstallProgressView::messageItem);
  connect(&m_controller, &Clf3ProcessController::itemCompleted, m_progress,
          &InstallProgressView::completeItem);
  connect(&m_controller, &Clf3ProcessController::itemFailed, m_progress,
          &InstallProgressView::failItem);
  connect(&m_controller, &Clf3ProcessController::phaseProgress, m_progress,
          &InstallProgressView::setProgress);
  connect(&m_controller, &Clf3ProcessController::logLine, m_log,
          &QPlainTextEdit::appendPlainText);
  connect(&m_controller, &Clf3ProcessController::nexusAuthorizationRequired, this,
          [this](const QString& requestId, const QString& archiveName,
                 const QString& domain, int modId, int fileId, qint64) {
            queueNexus(requestId, archiveName, domain, modId, fileId);
          });
  connect(&m_controller, &Clf3ProcessController::nexusFilesRequired, this,
          &Clf3MomwInstallDialog::listFiles);
  connect(&m_controller, &Clf3ProcessController::completed, this,
          &Clf3MomwInstallDialog::finish);
  connect(&m_controller, &Clf3ProcessController::failed, this,
          &Clf3MomwInstallDialog::fail);
  connect(&m_controller, &Clf3ProcessController::cancelled, this, [this] {
    clearNexus();
    m_status->setText(tr("Stopped. Downloaded archives are kept; install again to resume."));
    m_progress->stopActive(tr("Stopped"));
    m_cancel->setEnabled(false);
    m_close->setEnabled(true);
    if (m_deferredClose) QDialog::done(*m_deferredClose);
  });

  connect(&CuratedGuideNxmBroker::instance(),
          &CuratedGuideNxmBroker::acceptedForConsumer, this,
          [this](const QString& consumer, const QString& requestId, const QString& url) {
            if (consumer != kConsumer || !m_current || m_current->requestId != requestId)
              return;
            resolveNexus(*m_current, url);
          });
  connect(&CuratedGuideNxmBroker::instance(),
          &CuratedGuideNxmBroker::rejectedForConsumer, this,
          [this](const QString& consumer, const QString& requestId, const QString& reason) {
            if (consumer != kConsumer || !m_current || m_current->requestId != requestId)
              return;
            m_controller.rejectRequest(requestId, reason);
            finishCurrentNexus(requestId);
            QMessageBox::warning(this, tr("Nexus account mismatch"), reason);
          });
}

void Clf3MomwInstallDialog::finish(const QJsonObject& stats)
{
  clearNexus();
  CuratedGuideNxmBroker::instance().clearConsumer(kConsumer);
  if (QFileInfo::exists(QDir(m_createdInstanceDir).filePath("ModOrganizer.ini")))
    InstanceManager::registerPortableInstance(m_createdInstanceDir);
  const QJsonArray warnings = stats.value("warnings").toArray();
  m_progress->setPhaseEnded(tr("Installed"));
  m_status->setText(warnings.isEmpty()
                        ? tr("%1 is installed and added to your instances.").arg(m_title)
                        : tr("%1 is installed with %2 note(s); see the log.")
                              .arg(m_title)
                              .arg(warnings.size()));
  for (const auto& w : warnings) m_log->appendPlainText(tr("Note: %1").arg(w.toString()));
  const QString problems = stats.value("validator_problems").toString();
  if (!problems.isEmpty())
    m_log->appendPlainText(tr("Validator reported problems:\n%1").arg(problems));
  m_cancel->setEnabled(false);
  m_close->setEnabled(true);
  m_openInstance->show();
  if (m_deferredClose) QDialog::done(*m_deferredClose);
}

void Clf3MomwInstallDialog::fail(const QString& reason)
{
  clearNexus();
  m_createdInstanceDir.clear();
  m_status->setText(tr("The installation stopped: %1").arg(reason));
  m_log->appendPlainText(reason);
  m_progress->stopActive(tr("Stopped"));
  m_cancel->setEnabled(false);
  m_close->setEnabled(true);
  if (m_deferredClose) QDialog::done(*m_deferredClose);
}

void Clf3MomwInstallDialog::done(int result)
{
  if (m_controller.isRunning()) {
    // Never leave the installer running unseen: stop it and close after.
    m_deferredClose = result;
    if (!m_stopping) {
      m_stopping = true;
      m_cancel->setEnabled(false);
      m_status->setText(tr("Stopping before closing…"));
      clearNexus();
      m_controller.cancel();
    }
    return;
  }
  QDialog::done(result);
}

void Clf3MomwInstallDialog::queueNexus(const QString& requestId, const QString& archiveName,
                                       const QString& domain, int modId, int fileId)
{
  if (m_stopping) return;
  m_queue.enqueue({requestId, archiveName, domain, modId, fileId});
  if (NexusInterface::instance().getAPIUserAccount().type() != APIUserAccountTypes::Premium) {
    m_waiting.insert(requestId);
    m_progress->setWaitingRequest(requestId, archiveName,
                                  tr("Authorize this download on Nexus Mods"));
  }
  beginNextNexus();
}

void Clf3MomwInstallDialog::beginNextNexus()
{
  if (m_stopping || m_current || m_queue.isEmpty()) return;
  m_current = m_queue.dequeue();
  const auto account = NexusInterface::instance().getAPIUserAccount();
  if (account.type() == APIUserAccountTypes::None) {
    m_controller.rejectRequest(m_current->requestId,
                               tr("Sign in to Nexus in Fluorine before installing."));
    finishCurrentNexus(m_current->requestId);
  } else if (account.type() == APIUserAccountTypes::Premium) {
    resolveNexus(*m_current);
  } else {
    CuratedGuideNxmBroker::instance().expectForConsumer(
        kConsumer, m_current->requestId, m_current->domain, m_current->modId,
        m_current->fileId, account.id().toInt());
    showBrowser(*m_current);
  }
}

void Clf3MomwInstallDialog::finishCurrentNexus(const QString& requestId)
{
  m_progress->finishWaitingRequest(requestId);
  m_waiting.remove(requestId);
  m_current.reset();
  if (m_browser && m_waiting.isEmpty()) m_browser->hide();
  QTimer::singleShot(350, this, &Clf3MomwInstallDialog::beginNextNexus);
}

void Clf3MomwInstallDialog::showBrowser(const NexusRequest& request)
{
  m_progress->setWaitingRequest(request.requestId, request.archiveName,
                                tr("Choose Mod Manager Download, then Slow Download"));
  m_status->setText(tr("Waiting for your Nexus download click: %1").arg(request.archiveName));
  const QUrl page(QString("https://www.nexusmods.com/%1/mods/%2?tab=files&file_id=%3&nmm=1")
                      .arg(request.domain)
                      .arg(request.modId)
                      .arg(request.fileId));
#ifdef MO2_WEBENGINE
  if (m_browser) m_browser->deleteLater();
  m_browser = new QDialog(this);
  m_browser->setAttribute(Qt::WA_DeleteOnClose, false);
  m_browser->setWindowTitle(tr("Authorize Nexus Download — %1").arg(request.archiveName));
  m_browser->resize(1100, 800);
  auto* layout = new QVBoxLayout(m_browser);
  auto* help   = new QLabel(tr("Choose Mod Manager Download and then Slow Download. "
                               "Fluorine returns to the installation automatically."));
  help->setWordWrap(true);
  layout->addWidget(help);
  auto* view     = new QWebEngineView(m_browser);
  auto* authPage = new NexusAuthorizationPage(
      nexusProfile(),
      [](const QUrl& url) { CuratedGuideNxmBroker::instance().tryConsume(url.toString()); },
      view);
  view->setPage(authPage);
  layout->addWidget(view, 1);
  auto* buttons  = new QHBoxLayout;
  auto* external = new QPushButton(tr("Open in External Browser"));
  auto* hide     = new QPushButton(tr("Hide"));
  buttons->addWidget(external);
  buttons->addStretch();
  buttons->addWidget(hide);
  layout->addLayout(buttons);
  connect(external, &QPushButton::clicked, m_browser,
          [page] { QDesktopServices::openUrl(page); });
  connect(hide, &QPushButton::clicked, m_browser, &QDialog::hide);
  view->load(page);
  m_browser->show();
  m_browser->raise();
  m_browser->activateWindow();
#else
  QDesktopServices::openUrl(page);
  QMessageBox::information(
      this, tr("Nexus authorization"),
      tr("The embedded browser is unavailable in this build. Complete the Nexus download "
         "click in your browser; Fluorine will capture the NXM link."));
#endif
}

void Clf3MomwInstallDialog::resolveNexus(const NexusRequest& request, const QString& nxmUrl)
{
  QUrl endpoint(
      QString("https://api.nexusmods.com/v1/games/%1/mods/%2/files/%3/download_link.json")
          .arg(request.domain)
          .arg(request.modId)
          .arg(request.fileId));
  if (!nxmUrl.isEmpty()) {
    const NXMUrl parsed(nxmUrl);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), parsed.key());
    query.addQueryItem(QStringLiteral("expires"), QString::number(parsed.expires()));
    endpoint.setQuery(query);
  }
  auto* manager = NexusInterface::instance().getAccessManager();
  auto* reply   = manager ? manager->makeAuthenticatedGetRequest(endpoint) : nullptr;
  if (!reply) {
    m_controller.rejectRequest(request.requestId, tr("Nexus authentication is unavailable."));
    finishCurrentNexus(request.requestId);
    return;
  }
  connect(reply, &QNetworkReply::finished, this, [this, reply, request] {
    reply->deleteLater();
    if (m_stopping || !m_current || m_current->requestId != request.requestId) return;
    QStringList urls;
    if (reply->error() == QNetworkReply::NoError) {
      for (const auto& value : QJsonDocument::fromJson(reply->readAll()).array()) {
        const QString uri = value.toObject().value("URI").toString();
        if (!uri.isEmpty()) urls.push_back(uri);
      }
    }
    if (urls.isEmpty()) {
      m_controller.rejectRequest(request.requestId,
                                 tr("Nexus returned no usable download locations: %1")
                                     .arg(reply->errorString()));
    } else {
      m_controller.sendNexusUrls(request.requestId, urls);
      m_status->setText(tr("Downloading %1").arg(request.archiveName));
    }
    finishCurrentNexus(request.requestId);
  });
}

void Clf3MomwInstallDialog::listFiles(const QString& requestId, const QString& domain,
                                      int modId)
{
  static const QRegularExpression game("^[a-z0-9_-]+$");
  if (m_stopping) return;
  if (!game.match(domain).hasMatch() || modId <= 0) {
    m_controller.rejectRequest(requestId, tr("Invalid Nexus mod reference."));
    return;
  }
  const QUrl endpoint(QString("https://api.nexusmods.com/v1/games/%1/mods/%2/files.json")
                          .arg(domain)
                          .arg(modId));
  auto* manager = NexusInterface::instance().getAccessManager();
  auto* reply   = manager ? manager->makeAuthenticatedGetRequest(endpoint) : nullptr;
  if (!reply) {
    m_controller.rejectRequest(requestId, tr("Nexus authentication is unavailable."));
    return;
  }
  connect(reply, &QNetworkReply::finished, this, [this, reply, requestId, domain, modId] {
    reply->deleteLater();
    if (m_stopping) return;
    if (reply->error() != QNetworkReply::NoError) {
      m_controller.rejectRequest(requestId, tr("Nexus file list for %1/%2 failed: %3")
                                                .arg(domain)
                                                .arg(modId)
                                                .arg(reply->errorString()));
      return;
    }
    const auto document = QJsonDocument::fromJson(reply->readAll());
    if (!document.isObject()) {
      m_controller.rejectRequest(requestId, tr("Nexus returned an unreadable file list."));
      return;
    }
    m_controller.sendNexusFiles(requestId, document.object());
  });
}

void Clf3MomwInstallDialog::clearNexus()
{
  CuratedGuideNxmBroker::instance().clearConsumer(kConsumer);
  for (const auto& id : m_waiting) m_progress->finishWaitingRequest(id);
  m_waiting.clear();
  m_queue.clear();
  m_current.reset();
  if (m_browser) m_browser->hide();
}
