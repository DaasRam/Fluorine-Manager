#include "clf3installerdialog.h"
#include "fluorinetheme.h"
#include "clf3installutils.h"
#include "clf3collectiondialog.h"
#include "clf3installertabs.h"
#include "installprogressview.h"
#include "modlistmetadata.h"

#include "curatedguidenxmbroker.h"
#include "gamedetection.h"
#include "instancemanager.h"
#include "knowngames.h"
#include "nexusinterface.h"
#include "nxmaccessmanager.h"
#include "settings.h"
#include "settingsdialognexus.h"
#include "wabbajackpostinstall.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QDateTime>
#include <QUuid>
#include <QTimer>
#include <QToolButton>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <nxmurl.h>
#include <algorithm>
#include <utility>

#ifdef MO2_WEBENGINE
#include <QWebEngineCookieStore>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>
#include <functional>

namespace
{
class NexusAuthorizationPage : public QWebEnginePage
{
public:
  NexusAuthorizationPage(QWebEngineProfile* profile,
                         std::function<void(const QUrl&)> accepted,
                         QObject* parent)
      : QWebEnginePage(profile, parent), m_accepted(std::move(accepted))
  {}

protected:
  bool acceptNavigationRequest(const QUrl& url, NavigationType type,
                               bool mainFrame) override
  {
    if (url.scheme().compare(QStringLiteral("nxm"), Qt::CaseInsensitive) == 0) {
      m_accepted(url);
      return false;
    }
    return QWebEnginePage::acceptNavigationRequest(url, type, mainFrame);
  }

private:
  std::function<void(const QUrl&)> m_accepted;
};

QWebEngineProfile* nexusProfile()
{
  static QWebEngineProfile* profile = [] {
    auto* result = new QWebEngineProfile(QStringLiteral("FluorineNexus"), qApp);
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                         + QStringLiteral("/nexus-web-profile");
    result->setPersistentStoragePath(root);
    result->setCachePath(root + QStringLiteral("/cache"));
    result->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    return result;
  }();
  return profile;
}
}
#endif

namespace
{
QString formatBytes(qint64 bytes)
{
  if (bytes <= 0) return QObject::tr("Unknown size");
  if (bytes < 1024) return QObject::tr("%1 B").arg(bytes);
  if (bytes < 1024 * 1024)
    return QObject::tr("%1 KiB").arg(double(bytes) / 1024.0, 0, 'f', 1);
  const double gib = double(bytes) / (1024.0 * 1024.0 * 1024.0);
  if (gib >= 1.0) return QObject::tr("%1 GiB").arg(gib, 0, 'f', 1);
  return QObject::tr("%1 MiB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
}

QString displayGameName(const QString& game)
{
  static const QHash<QString, QString> names{
      {QStringLiteral("skyrimspecialedition"), QStringLiteral("Skyrim Special Edition")},
      {QStringLiteral("skyrimvr"), QStringLiteral("Skyrim VR")},
      {QStringLiteral("fallout4"), QStringLiteral("Fallout 4")},
      {QStringLiteral("fallout4vr"), QStringLiteral("Fallout 4 VR")},
      {QStringLiteral("falloutnewvegas"), QStringLiteral("Fallout: New Vegas")},
      {QStringLiteral("oblivion"), QStringLiteral("Oblivion")},
      {QStringLiteral("oblivionremastered"), QStringLiteral("Oblivion Remastered")},
      {QStringLiteral("morrowind"), QStringLiteral("Morrowind")},
      {QStringLiteral("cyberpunk2077"), QStringLiteral("Cyberpunk 2077")},
      {QStringLiteral("stardewvalley"), QStringLiteral("Stardew Valley")},
  };
  return names.value(game.toLower(), game);
}

QString knownGameNameForWabbajack(const QString& gameId)
{
  static const QHash<QString, QString> names{
      {QStringLiteral("falloutnewvegas"), QStringLiteral("Fallout New Vegas")},
      {QStringLiteral("fallout3"), QStringLiteral("Fallout 3")},
      {QStringLiteral("fallout4"), QStringLiteral("Fallout 4")},
      {QStringLiteral("fallout4vr"), QStringLiteral("Fallout 4 VR")},
      {QStringLiteral("oblivion"), QStringLiteral("Oblivion")},
      {QStringLiteral("morrowind"), QStringLiteral("Morrowind")},
      {QStringLiteral("skyrim"), QStringLiteral("Skyrim")},
      {QStringLiteral("skyrimspecialedition"),
       QStringLiteral("Skyrim Special Edition")},
      {QStringLiteral("skyrimvr"), QStringLiteral("Skyrim VR")},
      {QStringLiteral("starfield"), QStringLiteral("Starfield")},
      {QStringLiteral("cyberpunk2077"), QStringLiteral("Cyberpunk 2077")},
  };
  return names.value(gameId.toLower());
}

QString launcherStore(const QString& launcher)
{
  if (launcher.contains(QStringLiteral("GOG"), Qt::CaseInsensitive))
    return QStringLiteral("GOG");
  if (launcher.contains(QStringLiteral("Epic"), Qt::CaseInsensitive))
    return QStringLiteral("Epic Games");
  if (launcher.contains(QStringLiteral("Steam"), Qt::CaseInsensitive))
    return QStringLiteral("Steam");
  return {};
}

const KnownGame* knownGameForDetected(const DetectedGame& game)
{
  if (game.launcher.contains(QStringLiteral("GOG"), Qt::CaseInsensitive))
    if (const auto* known = findKnownGameByGogId(game.app_id)) return known;
  if (game.launcher.contains(QStringLiteral("Epic"), Qt::CaseInsensitive))
    if (const auto* known = findKnownGameByEpicId(game.app_id)) return known;
  if (game.launcher.contains(QStringLiteral("Steam"), Qt::CaseInsensitive))
    if (const auto* known = findKnownGameBySteamId(game.app_id)) return known;
  return findKnownGameByTitle(game.name);
}

QIcon galleryPlaceholder()
{
  QPixmap pixmap(240, 135);
  pixmap.fill(QColor(35, 39, 42));
  QPainter painter(&pixmap);
  painter.setPen(QColor(105, 111, 116));
  QFont font = painter.font();
  font.setBold(true);
  font.setPointSize(24);
  painter.setFont(font);
  painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("W"));
  return QIcon(pixmap);
}

QString galleryImageKey(const QJsonObject& item)
{
  const QString machineName = item.value("machine_name").toString();
  return machineName.isEmpty()
             ? item.value("links").toObject().value("image").toString()
             : machineName;
}

bool isGalleryUnavailable(const QJsonObject& item)
{
  return item.value("force_down").toBool()
         || item.value("links").toObject().value("download").toString().isEmpty();
}

QSet<QString> requestedMods(const QString& text)
{
  QSet<QString> result;
  for (const QString& part : text.split(';', Qt::SkipEmptyParts)) {
    const QString name = part.trimmed().toLower();
    if (!name.isEmpty()) result.insert(name);
  }
  return result;
}

QString imageCacheDirectory()
{
  return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
         + QStringLiteral("/wabbajack-images-v1");
}

QString imageCachePath(const QUrl& url)
{
  const QByteArray digest =
      QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha256).toHex();
  return imageCacheDirectory() + QLatin1Char('/') + QString::fromLatin1(digest)
         + QStringLiteral(".image");
}

QPixmap cachedImage(const QUrl& url)
{
  QPixmap image;
  if (url.isValid()) image.load(imageCachePath(url));
  return image;
}

void storeCachedImage(const QUrl& url, const QByteArray& data)
{
  if (!url.isValid() || data.isEmpty()) return;
  QSaveFile file(imageCachePath(url));
  if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size()) file.commit();
}

QPixmap croppedImage(const QPixmap& source, int width, int height)
{
  const QPixmap scaled = source.scaled(width, height, Qt::KeepAspectRatioByExpanding,
                                       Qt::SmoothTransformation);
  const int x = qMax(0, (scaled.width() - width) / 2);
  const int y = qMax(0, (scaled.height() - height) / 2);
  return scaled.copy(x, y, width, height);
}

void pruneImageCache()
{
  constexpr qint64 maximumBytes = 256LL * 1024LL * 1024LL;
  QDir directory(imageCacheDirectory());
  if (!directory.exists()) directory.mkpath(QStringLiteral("."));
  const QFileInfoList files = directory.entryInfoList(
      {QStringLiteral("*.image")}, QDir::Files, QDir::Time | QDir::Reversed);
  qint64 total = 0;
  for (const QFileInfo& file : files) total += file.size();
  for (const QFileInfo& file : files) {
    if (total <= maximumBytes) break;
    if (QFile::remove(file.absoluteFilePath())) total -= file.size();
  }
}
}

Clf3InstallerDialog::Clf3InstallerDialog(QWidget* parent, bool resumePending)
    : QDialog(parent), m_resumePending(resumePending)
{
  m_imageNetwork = new QNetworkAccessManager(this);
  m_postInstall = new WabbajackPostInstall(this);
  pruneImageCache();
  buildUi();
  setObjectName("fluorineInstaller");
  FluorineTheme::apply(this);

  connect(&m_controller, &Clf3ProcessController::engineReady, this,
          [this](const QString& version) {
            m_engineVersion->setText(tr("CLF3 %1 · protocol %2")
                                         .arg(version)
                                         .arg(Clf3ProcessController::ProtocolVersion));
          });
  connect(&m_controller, &Clf3ProcessController::phaseChanged, this,
          [this](const QString& phase) {
            if (!m_stopping) m_progressView->setPhase(phase);
            m_log->appendPlainText(tr("Phase: %1").arg(phase));
          });
  connect(&m_controller, &Clf3ProcessController::statusChanged, this,
          [this](const QString& status) { if (!m_stopping) m_status->setText(status); });
  connect(&m_controller, &Clf3ProcessController::modlistPlanReady, this,
          [this](const QJsonObject& plan) {
    const QString version = plan.value("version").toString();
    if (!m_listVersion.isEmpty() && m_listVersion != version) m_listUpdated = {};
    m_listVersion = version;
    if (!plan.value("name").toString().isEmpty()) m_listTitle = plan.value("name").toString();
    if (!plan.value("author").toString().isEmpty()) m_listAuthor = plan.value("author").toString();
    updateInstallIdentity();
    if (!savePendingJob()) m_log->appendPlainText(m_pendingSaveError);
  });
  connect(&m_controller, &Clf3ProcessController::itemMetadata, this,
          [this](const QString& name, const QString& displayName,
                 const QString& subtitle, const QString& imageUrl) {
            if (!displayName.isEmpty()) m_activeDisplayNames.insert(name, displayName);
            if (!subtitle.isEmpty()) m_activeSubtitles.insert(name, subtitle);
            Q_UNUSED(imageUrl);
          });
  connect(&m_controller, &Clf3ProcessController::itemStarted, this,
          &Clf3InstallerDialog::startActiveItem);
  connect(&m_controller, &Clf3ProcessController::itemProgress, this,
          &Clf3InstallerDialog::updateActiveItem);
  connect(&m_controller, &Clf3ProcessController::itemMessage, this,
          &Clf3InstallerDialog::setActiveItemMessage);
  connect(&m_controller, &Clf3ProcessController::itemCompleted, this,
          &Clf3InstallerDialog::finishActiveItem);
  connect(&m_controller, &Clf3ProcessController::itemFailed, this,
          &Clf3InstallerDialog::failActiveItem);
  connect(&m_controller, &Clf3ProcessController::phaseProgress,
          m_progressView, &InstallProgressView::setProgress);
  connect(&m_controller, &Clf3ProcessController::archivesReused,
          m_progressView, &InstallProgressView::setReused);
  connect(&m_controller, &Clf3ProcessController::logLine, m_log,
          &QPlainTextEdit::appendPlainText);
  connect(&m_controller, &Clf3ProcessController::nexusAuthorizationRequired,
          this, &Clf3InstallerDialog::queueNexus);
  connect(&m_controller, &Clf3ProcessController::manualDownloadRequired,
          this, &Clf3InstallerDialog::requestManualFile);
  connect(&m_controller, &Clf3ProcessController::completed, this,
          [this](QJsonObject stats) {
    stats.insert(QStringLiteral("fluorineInstalledAt"),
                 QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    finishInstall(stats);
  });
  connect(m_postInstall, &WabbajackPostInstall::statusChanged, m_status,
          &QLabel::setText);
  connect(m_postInstall, &WabbajackPostInstall::logLine, m_log,
          &QPlainTextEdit::appendPlainText);
  connect(m_postInstall, &WabbajackPostInstall::stepStarted, this,
          [this](const QString& id, const QString& title) {
            startActiveItem(QStringLiteral("post:") + id, title, title, {},
                            tr("Fluorine setup"), {}, 0, QStringLiteral("items"));
          });
  connect(m_postInstall, &WabbajackPostInstall::stepFinished, this,
          [this](const QString& id) {
            finishActiveItem(QStringLiteral("post:") + id);
          });
  connect(m_postInstall, &WabbajackPostInstall::toolProgress, this,
          [this](const QString& id, qint64 completed, qint64 total) {
            const QString step =
                id == QStringLiteral("oblivion-4gb-patcher-linux")
                    ? QStringLiteral("post:oblivion-4gb")
                    : QStringLiteral("post:vnv-patcher");
            updateActiveItem(step, completed, total, 0.0,
                             QStringLiteral("bytes"));
          });
  connect(m_postInstall, &WabbajackPostInstall::nexusAuthorizationRequired,
          this, &Clf3InstallerDialog::queuePostInstallNexus);
  connect(m_postInstall, &WabbajackPostInstall::completed, this,
          &Clf3InstallerDialog::completePostInstall);
  connect(m_postInstall, &WabbajackPostInstall::failed, this,
          &Clf3InstallerDialog::failPostInstall);
  connect(&m_controller, &Clf3ProcessController::failed, this,
          [this](const QString& error) {
            CuratedGuideNxmBroker::instance().clearConsumer(QStringLiteral("clf3"));
            CuratedGuideNxmBroker::instance().clearConsumer(
                QStringLiteral("wabbajack-postinstall"));
            clearNexusRequests();
            m_status->setText(tr("Stopped: %1").arg(error));
            m_log->appendPlainText(tr("FAILED: %1").arg(error));
            m_progressView->stopActive(tr("Installation stopped"));
            m_progressView->setPhaseEnded(tr("Installation stopped"));
            m_cancel->setEnabled(false);
            m_close->setEnabled(true);
            m_resumeInstall->show();
            m_stopping = false;
            clearManualRequests();
            closeWhenIdle();
          });
  connect(&m_controller, &Clf3ProcessController::cancelled, this, [this] {
    CuratedGuideNxmBroker::instance().clearConsumer(QStringLiteral("clf3"));
    CuratedGuideNxmBroker::instance().clearConsumer(
        QStringLiteral("wabbajack-postinstall"));
    clearNexusRequests();
    m_stopping = false;
    clearManualRequests();
    m_status->setText(tr("Cancelled. The saved installation can be continued later."));
    m_progressView->stopActive(tr("Stopped; saved files are retained"));
    m_progressView->setPhaseEnded(tr("Stopped — ready to resume"));
    m_cancel->setEnabled(false);
    m_close->setEnabled(true);
    m_resumeInstall->show();
    closeWhenIdle();
  });
  connect(&CuratedGuideNxmBroker::instance(),
          &CuratedGuideNxmBroker::acceptedForConsumer, this,
          &Clf3InstallerDialog::nexusLinkAccepted);
  connect(&CuratedGuideNxmBroker::instance(),
          &CuratedGuideNxmBroker::rejectedForConsumer, this,
          [this](const QString& consumer, const QString& requestId,
                 const QString& reason) {
            if (!m_currentNexus || m_currentNexus->consumer != consumer
                || m_currentNexus->requestId != requestId)
              return;
            if (consumer == QStringLiteral("clf3"))
              m_controller.rejectRequest(requestId, reason);
            else if (consumer == QStringLiteral("wabbajack-postinstall"))
              m_postInstall->rejectNexusAuthorization(requestId, reason);
            if (m_browserDialog) m_browserDialog->hide();
            m_progressView->finishWaitingRequest(requestId);
            m_waitingNexus.remove(requestId);
            QMessageBox::warning(this, tr("Nexus account mismatch"), reason);
            m_currentNexus.reset();
            updateAttentionPanel();
            QTimer::singleShot(0, this, &Clf3InstallerDialog::beginNextNexus);
          });

  connect(&m_galleryLoader, &Clf3GalleryLoader::statusChanged,
          m_galleryStatus, &QLabel::setText);
  connect(&m_galleryLoader, &Clf3GalleryLoader::failed, this,
          [this](const QString& error) {
    m_refreshGallery->setEnabled(true);
    m_galleryStatus->setText(error);
    if (!m_galleryLoaded) {
      m_resultCount->setText(tr("Gallery unavailable"));
      m_details->setText(tr("Could not load the gallery. Retry with Refresh, or select "
                            "a local .wabbajack file or its URL."));
    }
  });
  connect(&m_galleryLoader, &Clf3GalleryLoader::loaded, this,
          [this](const QJsonDocument& document) {
            m_galleryLoaded = true;
            m_refreshGallery->setEnabled(true);
            m_galleryStatus->setText(tr("Gallery loaded."));
            m_gallery.clear();
            m_installedGames.clear();
            m_allMods.clear();
            m_modsPerList.clear();
            const QJsonArray modlists = document.isObject()
                                             ? document.object().value("modlists").toArray()
                                             : document.array();
            if (document.isObject()) {
              for (const auto& game : document.object().value("installed_games").toArray())
                m_installedGames.insert(game.toString().toLower());
              const auto searchIndex = document.object().value("search_index").toObject();
              for (const auto& mod : searchIndex.value("AllMods").toArray())
                m_allMods.push_back(mod.toString());
              const auto modsPerList = searchIndex.value("ModsPerList").toObject();
              for (auto it = modsPerList.begin(); it != modsPerList.end(); ++it) {
                QSet<QString> mods;
                for (const auto& mod : it.value().toArray())
                  mods.insert(mod.toString().toLower());
                m_modsPerList.insert(it.key().toLower(), std::move(mods));
              }
            }
            for (const auto& value : modlists)
              if (value.isObject()) m_gallery.push_back(value.toObject());
            updateGameFilter();
            for (QLineEdit* edit : {m_includeMods, m_excludeMods}) {
              auto* previous = edit->completer();
              auto* completer = new QCompleter(m_allMods, edit);
              completer->setCaseSensitivity(Qt::CaseInsensitive);
              completer->setCompletionMode(QCompleter::PopupCompletion);
              completer->setFilterMode(Qt::MatchContains);
              completer->setMaxVisibleItems(12);
              edit->setCompleter(completer);
              if (previous) previous->deleteLater();
              edit->setEnabled(!m_allMods.isEmpty());
            }
            populateGallery();
          });
  loadGallery();
  QTimer::singleShot(0, this, [this] {
    if (NexusInterface::instance().getAPIUserAccount().type() == APIUserAccountTypes::None) {
      NexusOAuthTokens tokens;
      const bool hasOAuth = GlobalSettings::nexusOAuthTokens(tokens);
      const bool hasKey = GlobalSettings::nexusApiKey(tokens.apiKey);
      if (hasOAuth || hasKey)
        NexusInterface::instance().getAccessManager()->apiCheck(tokens);
    }
    offerResume();
  });
}

Clf3InstallerDialog::~Clf3InstallerDialog()
{
  CuratedGuideNxmBroker::instance().clearConsumer(QStringLiteral("clf3"));
  CuratedGuideNxmBroker::instance().clearConsumer(
      QStringLiteral("wabbajack-postinstall"));
  m_postInstall->cancel();
  if (m_controller.isRunning()) m_controller.cancel();
}

bool Clf3InstallerDialog::shouldSwitchToInstance() const
{
  return m_switchInstance && m_switchInstance->isEnabled()
         && m_switchInstance->isChecked();
}

void Clf3InstallerDialog::buildUi()
{
  setWindowTitle(tr("Install a Modlist"));
  resize(1280, 820);
  auto* outer = new QVBoxLayout(this);
  m_pages     = new QStackedWidget(this);
  m_pages->setObjectName("modlistInstallPages");
  m_tabs = new Clf3InstallerTabs(m_pages, [this](QWidget* parent) {
    auto* panel = new Clf3CollectionDialog([](const QUrl& url, const QByteArray& json) {
      auto* manager = NexusInterface::instance().getAccessManager();
      return manager ? manager->makeCollectionRequest(url, json) : nullptr;
    }, parent);
    QHash<QString, QString> paths;
    for (const auto& game : detectAllGames().games) {
      const KnownGame* known = knownGameForDetected(game);
      if (known) paths.insert(QString::fromLatin1(known->name), game.install_path);
    }
    panel->setDetectedGames(paths);
    return panel;
  }, this);
  outer->addWidget(m_tabs, 1);
  auto* stage = new QLabel;
  stage->setObjectName("modlistInstallStage");
  stage->setTextFormat(Qt::PlainText);
  stage->setProperty("secondary", true);
  stage->setContentsMargins(10, 4, 10, 2);
  outer->insertWidget(0, stage);
  auto updateStage = [this, stage] {
    const QStringList stages{tr("1 · Choose a list"),
                             tr("2 · Review locations and requirements"),
                             tr("3 · Installation")};
    const int page = m_pages->currentIndex();
    stage->setText(page >= 0 && page < stages.size() ? stages[page] : stages[0]);
    stage->setVisible(m_tabs->currentIndex() == 0);
  };
  connect(m_pages, &QStackedWidget::currentChanged, this, updateStage);
  connect(m_tabs, &QTabWidget::currentChanged, this, updateStage);
  updateStage();
  connect(m_tabs, &Clf3InstallerTabs::nexusConnectionRequested,
          this, &Clf3InstallerDialog::connectNexus);
  connect(m_tabs, &Clf3InstallerTabs::collectionsFinished, this, [this](int result) {
    const auto* panel = m_tabs->collections();
    if (result == QDialog::Accepted && !panel->createdInstanceDir().isEmpty()) {
      m_createdInstanceDir = panel->createdInstanceDir();
      InstanceManager::registerPortableInstance(m_createdInstanceDir);
      m_switchInstance->setChecked(panel->shouldOpen());
      m_switchInstance->setEnabled(true);
      accept();
    } else if (m_deferredClose) closeWhenIdle();
    else reject();
  });
  connect(m_pages, &QStackedWidget::currentChanged, this, [this](int page) {
    m_tabs->setTabEnabled(1, page != 2);
  });

  auto* browsePage   = new QWidget;
  auto* browseLayout = new QVBoxLayout(browsePage);
  auto* heading      = new QLabel(tr("Choose a Wabbajack modlist"));
  QFont headingFont  = heading->font();
  headingFont.setPointSize(headingFont.pointSize() + 5);
  headingFont.setBold(true);
  heading->setFont(headingFont);
  browseLayout->addWidget(heading);
  auto* nexusConnect = new QPushButton(tr("Connect to Nexus…"));
  browseLayout->addWidget(nexusConnect, 0, Qt::AlignLeft);
  connect(nexusConnect, &QPushButton::clicked, this, &Clf3InstallerDialog::connectNexus);
  m_galleryStatus = new QLabel;
  m_galleryStatus->setWordWrap(true);
  m_galleryStatus->setTextFormat(Qt::PlainText);
  browseLayout->addWidget(m_galleryStatus);
  auto* searchRow = new QHBoxLayout;
  m_search        = new QLineEdit;
  m_search->setPlaceholderText(tr("Search title, author, or game…"));
  auto* refresh = new QPushButton(tr("Refresh"));
  m_refreshGallery = refresh;
  searchRow->addWidget(m_search, 1);
  searchRow->addWidget(refresh);
  browseLayout->addLayout(searchRow);

  auto* filterRow = new QHBoxLayout;
  m_gameFilter    = new QComboBox;
  m_sortOrder     = new QComboBox;
  m_installedOnly = new QCheckBox(tr("Installed games only"));
  m_showNsfw      = new QCheckBox(tr("Show NSFW"));
  m_showUnavailable = new QCheckBox(tr("Show unavailable"));
  m_officialOnly  = new QCheckBox(tr("Official only"));
  m_resultCount   = new QLabel;
  m_gameFilter->addItem(tr("All games"), QString());
  m_sortOrder->addItem(tr("Gallery order"), QStringLiteral("featured"));
  m_sortOrder->addItem(tr("Recently updated"), QStringLiteral("updated-desc"));
  m_sortOrder->addItem(tr("Title A–Z"), QStringLiteral("title-asc"));
  m_sortOrder->addItem(tr("Title Z–A"), QStringLiteral("title-desc"));
  m_sortOrder->addItem(tr("Smallest download"), QStringLiteral("download-asc"));
  m_sortOrder->addItem(tr("Largest download"), QStringLiteral("download-desc"));
  m_sortOrder->addItem(tr("Smallest installation"), QStringLiteral("installed-asc"));
  m_sortOrder->addItem(tr("Largest installation"), QStringLiteral("installed-desc"));
  m_sortOrder->addItem(tr("Fewest archives"), QStringLiteral("archives-asc"));
  m_sortOrder->addItem(tr("Most archives"), QStringLiteral("archives-desc"));
  filterRow->addWidget(new QLabel(tr("Game:")));
  filterRow->addWidget(m_gameFilter);
  filterRow->addWidget(new QLabel(tr("Sort:")));
  filterRow->addWidget(m_sortOrder);
  filterRow->addWidget(m_installedOnly);
  filterRow->addWidget(m_showNsfw);
  filterRow->addWidget(m_showUnavailable);
  filterRow->addWidget(m_officialOnly);
  filterRow->addStretch();
  filterRow->addWidget(m_resultCount);
  browseLayout->addLayout(filterRow);

  auto* modFilterRow = new QHBoxLayout;
  m_includeMods      = new QLineEdit;
  m_excludeMods      = new QLineEdit;
  m_includeMods->setPlaceholderText(tr("Exact mod names; separate several with ;"));
  m_excludeMods->setPlaceholderText(tr("Exact mod names; separate several with ;"));
  m_includeMods->setToolTip(
      tr("Only show lists containing every named Mod Organizer mod."));
  m_excludeMods->setToolTip(
      tr("Hide lists containing any named Mod Organizer mod."));
  m_includeMods->setEnabled(false);
  m_excludeMods->setEnabled(false);
  modFilterRow->addWidget(new QLabel(tr("Must include:")));
  modFilterRow->addWidget(m_includeMods, 1);
  modFilterRow->addWidget(new QLabel(tr("Must not include:")));
  modFilterRow->addWidget(m_excludeMods, 1);
  browseLayout->addLayout(modFilterRow);

  auto* galleryRow = new QHBoxLayout;
  m_galleryList    = new QListWidget;
  m_galleryList->setObjectName("modlistGallery");
  m_galleryList->setViewMode(QListView::IconMode);
  m_galleryList->setMovement(QListView::Static);
  m_galleryList->setResizeMode(QListView::Adjust);
  m_galleryList->setIconSize(QSize(240, 135));
  m_galleryList->setGridSize(QSize(270, 235));
  m_galleryList->setSpacing(7);
  m_galleryList->setWordWrap(true);
  m_galleryList->setUniformItemSizes(true);
  m_details        = new QLabel(tr("Loading the Wabbajack gallery…"));
  m_details->setObjectName("modlistGalleryDetails");
  m_details->setWordWrap(true);
  m_details->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  m_details->setTextInteractionFlags(Qt::TextBrowserInteraction);
  m_details->setOpenExternalLinks(true);
  m_details->setMinimumWidth(300);
  m_details->setMaximumWidth(430);
  galleryRow->addWidget(m_galleryList, 1);
  galleryRow->addWidget(m_details);
  browseLayout->addLayout(galleryRow, 1);
  auto* sourceRow = new QHBoxLayout;
  m_source        = new QLineEdit;
  m_source->setObjectName("modlistSource");
  m_source->setPlaceholderText(tr("Gallery URL or local .wabbajack file"));
  auto* browse = new QPushButton(tr("Browse…"));
  sourceRow->addWidget(m_source, 1);
  sourceRow->addWidget(browse);
  browseLayout->addLayout(sourceRow);
  auto* configure = new QPushButton(tr("Configure Installation →"));
  configure->setObjectName("modlistConfigure");
  configure->setProperty("primary", true);
  browseLayout->addWidget(configure, 0, Qt::AlignRight);
  m_pages->addWidget(browsePage);

  auto* configPage   = new QWidget;
  auto* configLayout = new QVBoxLayout(configPage);
  auto* reviewHeader = new QHBoxLayout;
  m_reviewIdentity = new QLabel;
  m_reviewIdentity->setObjectName("modlistReviewIdentity");
  m_reviewIdentity->setTextFormat(Qt::PlainText);
  m_reviewIdentity->setWordWrap(true);
  m_reviewIdentity->setFont(headingFont);
  m_reviewInstructions = new QPushButton(tr("List instructions"));
  reviewHeader->addWidget(m_reviewIdentity, 1);
  reviewHeader->addWidget(m_reviewInstructions, 0, Qt::AlignTop);
  configLayout->addLayout(reviewHeader);
  m_reviewUpdated = new QLabel;
  m_reviewUpdated->setObjectName("modlistReviewUpdated");
  m_reviewUpdated->setTextFormat(Qt::PlainText);
  m_reviewUpdated->setWordWrap(true);
  m_reviewUpdated->setProperty("secondary", true);
  configLayout->addWidget(m_reviewUpdated);
  configLayout->addWidget(new QLabel(tr("Installation paths")));
  auto* form = new QFormLayout;
  m_instanceName = new QLineEdit;
  m_instanceName->setObjectName("modlistSetupName");
  form->addRow(tr("Instance name:"), m_instanceName);
  auto pathRow = [this, form](const QString& label, QLineEdit*& edit) {
    auto* row = new QWidget;
    auto* box = new QHBoxLayout(row);
    box->setContentsMargins(0, 0, 0, 0);
    edit = new QLineEdit;
    auto* button = new QPushButton(tr("Browse…"));
    box->addWidget(edit, 1);
    box->addWidget(button);
    connect(button, &QPushButton::clicked, this,
            [this, edit] { chooseDirectory(edit); });
    form->addRow(label, row);
  };
  pathRow(tr("Instance folder:"), m_output);
  pathRow(tr("Download cache:"), m_downloads);
  pathRow(tr("Game folder:"), m_game);
  m_output->setObjectName("modlistOutputPath");
  m_downloads->setObjectName("modlistDownloadsPath");
  m_game->setObjectName("modlistGamePath");
  m_game->setPlaceholderText(tr("Leave empty to let CLF3 auto-detect the game"));
  m_store = new QComboBox;
  m_store->addItem(tr("Auto-detect"), QString());
  m_store->addItem(tr("Steam"), QStringLiteral("Steam"));
  m_store->addItem(tr("GOG"), QStringLiteral("GOG"));
  m_store->addItem(tr("Epic Games"), QStringLiteral("Epic Games"));
  form->addRow(tr("Game store:"), m_store);
  configLayout->addLayout(form);
  auto* note = new QLabel(tr("CLF3 reuses verified downloads when possible. Keep the same "
                              "download cache to continue an interrupted install or update a list."));
  note->setWordWrap(true);
  configLayout->addWidget(note);
  m_preflightSummary = new QLabel;
  m_preflightSummary->setWordWrap(true);
  m_preflightSummary->setTextFormat(Qt::PlainText);
  configLayout->addWidget(m_preflightSummary);
  for (auto* edit : {m_output, m_downloads, m_game})
    connect(edit, &QLineEdit::editingFinished, this,
            &Clf3InstallerDialog::updatePreflightSummary);
  configLayout->addStretch();
  auto* configButtons = new QHBoxLayout;
  auto* back          = new QPushButton(tr("← Back"));
  auto* install       = new QPushButton(tr("Install"));
  install->setObjectName("modlistBeginInstall");
  install->setProperty("primary", true);
  configButtons->addWidget(back);
  configButtons->addStretch();
  configButtons->addWidget(install);
  configLayout->addLayout(configButtons);
  m_pages->addWidget(configPage);

  auto* progressPage   = new QWidget;
  auto* progressLayout = new QVBoxLayout(progressPage);
  progressPage->setObjectName("modlistProgressPage");
  auto* installHeader = new QHBoxLayout;
  m_installIdentity = new QLabel;
  m_installIdentity->setObjectName("modlistInstallIdentity");
  m_installIdentity->setTextFormat(Qt::PlainText);
  m_installIdentity->setWordWrap(true);
  m_installIdentity->setFont(headingFont);
  m_installInstructions = new QPushButton(tr("List instructions"));
  installHeader->addWidget(m_installIdentity, 1);
  installHeader->addWidget(m_installInstructions, 0, Qt::AlignTop);
  progressLayout->addLayout(installHeader);
  m_installUpdated = new QLabel;
  m_installUpdated->setObjectName("modlistInstallUpdated");
  m_installUpdated->setTextFormat(Qt::PlainText);
  m_installUpdated->setWordWrap(true);
  m_installUpdated->setProperty("secondary", true);
  progressLayout->addWidget(m_installUpdated);
  for (auto* button : {m_reviewInstructions, m_installInstructions}) {
    connect(button, &QPushButton::clicked, this, [this] {
      if (m_readmeUrl.isValid() && (m_readmeUrl.scheme() == "https" || m_readmeUrl.scheme() == "http"))
        QDesktopServices::openUrl(m_readmeUrl);
    });
  }
  m_engineVersion      = new QLabel(tr("Waiting for CLF3…"));
  m_status             = new QLabel(tr("Preparing…"));
  m_status->setObjectName("modlistInstallStatus");
  m_status->setTextFormat(Qt::PlainText);
  m_status->setWordWrap(true);
  progressLayout->addWidget(m_status);
  m_progressView = new InstallProgressView;
  progressLayout->addWidget(m_progressView, 1);
  connect(m_progressView, &InstallProgressView::attentionActivated,
          this, &Clf3InstallerDialog::activateAttentionRequest);
  m_attentionPanel = new QFrame;
  m_attentionPanel->setObjectName("modlistAttentionPanel");
  auto* attentionLayout = new QVBoxLayout(m_attentionPanel);
  attentionLayout->setContentsMargins(8, 6, 8, 6);
  auto* attentionHeader = new QHBoxLayout;
  m_attentionSummary = new QLabel;
  m_attentionSummary->setTextFormat(Qt::PlainText);
  m_attentionSummary->setWordWrap(true);
  m_openNexus = new QPushButton(tr("Open Nexus download"));
  m_openNexus->setObjectName("modlistOpenNexusRequest");
  attentionHeader->addWidget(m_attentionSummary, 1);
  attentionHeader->addWidget(m_openNexus);
  attentionLayout->addLayout(attentionHeader);
  connect(m_openNexus, &QPushButton::clicked, this, [this] {
    if (m_currentNexus) showNexusBrowser(*m_currentNexus);
  });
  m_manualDownloads = new QListWidget;
  m_manualDownloads->setObjectName("modlistManualDownloads");
  m_manualDownloads->setMaximumHeight(190);
  m_manualDownloads->hide();
  attentionLayout->addWidget(m_manualDownloads);
  progressLayout->addWidget(m_attentionPanel);
  m_attentionPanel->hide();
  m_detailsPanel = new QWidget;
  auto* detailsLayout = new QVBoxLayout(m_detailsPanel);
  detailsLayout->setContentsMargins(0, 0, 0, 0);
  detailsLayout->addWidget(m_engineVersion);
  m_log = new QPlainTextEdit;
  m_log->setObjectName("modlistInstallLog");
  m_log->setReadOnly(true);
  m_log->setMaximumBlockCount(10000);
  detailsLayout->addWidget(m_log);
  progressLayout->addWidget(m_detailsPanel, 1);
  m_detailsPanel->hide();
  m_switchInstance = new QCheckBox(tr("Open this setup when this window closes"));
  m_switchInstance->setChecked(true);
  m_switchInstance->setEnabled(false);
  progressLayout->addWidget(m_switchInstance);
  auto* progressButtons = new QHBoxLayout;
  m_cancel              = new QPushButton(tr("Stop and resume later"));
  m_cancel->setObjectName("modlistStopInstall");
  m_close               = new QPushButton(tr("Close"));
  m_close->setObjectName("modlistCloseInstall");
  m_close->setEnabled(false);
  progressButtons->addWidget(m_cancel);
  m_resumeInstall = new QPushButton(tr("Resume installation"));
  m_resumeInstall->setObjectName("modlistResumeInstall");
  m_resumeInstall->setProperty("primary", true);
  m_resumeInstall->hide();
  progressButtons->addWidget(m_resumeInstall);
  connect(m_resumeInstall, &QPushButton::clicked, this, &Clf3InstallerDialog::startInstall);
  m_retrySetup = new QPushButton(tr("Retry compatibility setup"));
  m_retrySetup->setVisible(false);
  progressButtons->addWidget(m_retrySetup);
  m_detailsToggle = new QToolButton;
  m_detailsToggle->setText(tr("Details"));
  m_detailsToggle->setCheckable(true);
  m_detailsToggle->setObjectName("modlistDetailsToggle");
  connect(m_detailsToggle, &QToolButton::toggled, m_detailsPanel, &QWidget::setVisible);
  progressButtons->addWidget(m_detailsToggle);
  auto* exportButton = new QPushButton(tr("Export log…"));
  progressButtons->addWidget(exportButton);
  connect(exportButton, &QPushButton::clicked, this, &Clf3InstallerDialog::exportLog);
  connect(m_retrySetup, &QPushButton::clicked, this, [this] {
    if (!m_postInstallRunning && !m_controller.isRunning()) finishInstall(m_installStats);
  });
  progressButtons->addStretch();
  progressButtons->addWidget(m_close);
  progressLayout->addLayout(progressButtons);
  m_pages->addWidget(progressPage);

  connect(refresh, &QPushButton::clicked, this, [this] { loadGallery(true); });
  connect(m_search, &QLineEdit::textChanged, this,
          [this] { populateGallery(); });
  auto gallerySettings = Clf3InstallUtils::openSettings();
  gallerySettings->beginGroup(QStringLiteral("clf3/gallery"));
  m_showNsfw->setChecked(gallerySettings->value("showNsfw", false).toBool());
  m_showUnavailable->setChecked(
      gallerySettings->value("showUnavailable", false).toBool());
  m_officialOnly->setChecked(gallerySettings->value("officialOnly", false).toBool());
  m_installedOnly->setChecked(
      gallerySettings->value("installedOnly", false).toBool());
  const int savedSort = m_sortOrder->findData(
      gallerySettings->value("sort", QStringLiteral("featured")).toString());
  m_sortOrder->setCurrentIndex(qMax(0, savedSort));
  gallerySettings->endGroup();
  auto refilter = [this] {
    auto settings = Clf3InstallUtils::openSettings();
    settings->beginGroup(QStringLiteral("clf3/gallery"));
    settings->setValue("sort", m_sortOrder->currentData());
    settings->setValue("showNsfw", m_showNsfw->isChecked());
    settings->setValue("showUnavailable", m_showUnavailable->isChecked());
    settings->setValue("officialOnly", m_officialOnly->isChecked());
    settings->setValue("installedOnly", m_installedOnly->isChecked());
    settings->endGroup();
    populateGallery();
  };
  connect(m_gameFilter, &QComboBox::currentIndexChanged, this,
          refilter);
  connect(m_sortOrder, &QComboBox::currentIndexChanged, this,
          refilter);
  connect(m_installedOnly, &QCheckBox::toggled, this, [this, refilter] {
    updateGameFilter();
    refilter();
  });
  connect(m_showNsfw, &QCheckBox::toggled, this,
          refilter);
  connect(m_showUnavailable, &QCheckBox::toggled, this,
          refilter);
  connect(m_officialOnly, &QCheckBox::toggled, this,
          refilter);
  connect(m_includeMods, &QLineEdit::textChanged, this,
          [this] { populateGallery(); });
  connect(m_excludeMods, &QLineEdit::textChanged, this,
          [this] { populateGallery(); });
  connect(m_galleryList, &QListWidget::currentRowChanged, this,
          [this] { selectGalleryItem(); });
  connect(m_source, &QLineEdit::textEdited, this, [this] {
    m_machineName.clear();
    m_gameId.clear();
    m_game->clear();
    m_store->setCurrentIndex(0);
    m_listTitle.clear();
    m_listAuthor.clear();
    m_listVersion.clear();
    m_listUpdated = {};
    m_readmeUrl = QUrl();
    updateInstallIdentity();
  });
  connect(browse, &QPushButton::clicked, this, &Clf3InstallerDialog::chooseSource);
  connect(configure, &QPushButton::clicked, this,
          &Clf3InstallerDialog::showConfiguration);
  connect(back, &QPushButton::clicked, this, [this] { m_pages->setCurrentIndex(0); });
  connect(install, &QPushButton::clicked, this, &Clf3InstallerDialog::startInstall);
  connect(m_cancel, &QPushButton::clicked, this,
          &Clf3InstallerDialog::cancelInstall);
  connect(m_close, &QPushButton::clicked, this, &QDialog::accept);

  const QString configuredDownloadDir =
      Clf3InstallUtils::loadDefaultDownloadDir().trimmed();
  m_downloads->setText(!configuredDownloadDir.isEmpty()
                           ? configuredDownloadDir
                           : QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
                                 + QStringLiteral("/Fluorine/modlists"));
  updateInstallIdentity();
}

void Clf3InstallerDialog::loadGallery(bool refresh)
{
  if (m_galleryLoader.isBusy()) return;
  m_refreshGallery->setEnabled(false);
  if (!m_galleryLoaded) {
    m_resultCount->setText(tr("Loading…"));
    m_installedOnly->setEnabled(false);
    m_installedOnly->setText(tr("Installed games only (waiting for gallery)"));
  }
  m_galleryLoader.load(refresh);
}

void Clf3InstallerDialog::connectNexus()
{
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Connect to Nexus"));
  dialog.resize(580, 360);
  auto* layout = new QVBoxLayout(&dialog);
  auto* explanation = new QLabel(tr("Connect your Nexus account to download mods. "
                                    "You can skip this and browse the gallery without signing in."));
  explanation->setWordWrap(true);
  layout->addWidget(explanation);
  auto* buttons = new QHBoxLayout;
  auto* connectButton = new QPushButton(tr("Connect to Nexus"));
  auto* manualButton = new QPushButton(tr("Enter API key manually"));
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
  updatePreflightSummary();
}

void Clf3InstallerDialog::populateGallery()
{
  if (!m_galleryLoaded) return;
  const QString query = m_search->text().trimmed();
  const QSet<QString> requiredMods = requestedMods(m_includeMods->text());
  const QSet<QString> excludedMods = requestedMods(m_excludeMods->text());
  const QString game = m_gameFilter->currentData().toString();
  const QString sort = m_sortOrder->currentData().toString();
  const QString selectedMachine = m_galleryList->currentItem()
                                      ? m_galleryList->currentItem()
                                            ->data(Qt::UserRole + 1)
                                            .toString()
                                      : QString();
  QVector<int> visible;
  visible.reserve(m_gallery.size());

  for (int i = 0; i < m_gallery.size(); ++i) {
    const auto& item = m_gallery.at(i);
    const auto links = item.value("links").toObject();
    if (!m_showUnavailable->isChecked() && isGalleryUnavailable(item))
      continue;
    if (!m_showNsfw->isChecked() && item.value("nsfw").toBool()) continue;
    if (m_officialOnly->isChecked() && !item.value("official").toBool()) continue;
    const QString itemGame = item.value("game").toString();
    if (!game.isEmpty() && itemGame.compare(game, Qt::CaseInsensitive) != 0) continue;
    if (m_installedOnly->isChecked()
        && !m_installedGames.contains(itemGame.toLower()))
      continue;
    if (!requiredMods.isEmpty() || !excludedMods.isEmpty()) {
      const auto mods = m_modsPerList.value(
          item.value("machine_name").toString().toLower());
      bool matches = true;
      for (const auto& mod : requiredMods)
        if (!mods.contains(mod)) matches = false;
      for (const auto& mod : excludedMods)
        if (mods.contains(mod)) matches = false;
      if (!matches) continue;
    }

    QString haystack = item.value("title").toString() + ' '
                       + item.value("author").toString() + ' ' + itemGame + ' '
                       + item.value("description").toString();
    for (const auto& tag : item.value("tags").toArray())
      haystack += ' ' + tag.toString();
    if (!query.isEmpty() && !haystack.contains(query, Qt::CaseInsensitive)) continue;
    visible.push_back(i);
  }

  auto numeric = [this](int index, const char* field) {
    return m_gallery.at(index)
        .value("download_metadata")
        .toObject()
        .value(field)
        .toVariant()
        .toLongLong();
  };
  std::stable_sort(visible.begin(), visible.end(), [this, &sort, &numeric](int a, int b) {
    const auto& left  = m_gallery.at(a);
    const auto& right = m_gallery.at(b);
    if (sort == QStringLiteral("updated-desc")) {
      const auto leftDate = ModlistMetadata::sourceUpdated(left);
      const auto rightDate = ModlistMetadata::sourceUpdated(right);
      if (leftDate && rightDate) return *leftDate > *rightDate;
      return leftDate.has_value() && !rightDate.has_value();
    }
    if (sort == QStringLiteral("title-asc") || sort == QStringLiteral("title-desc")) {
      const int comparison = QString::localeAwareCompare(
          left.value("title").toString(), right.value("title").toString());
      return sort.endsWith("asc") ? comparison < 0 : comparison > 0;
    }
    const char* field = sort.startsWith("download")
                            ? "SizeOfArchives"
                            : sort.startsWith("installed") ? "SizeOfInstalledFiles"
                                                           : "NumberOfArchives";
    if (sort != QStringLiteral("featured")) {
      const qint64 leftValue  = numeric(a, field);
      const qint64 rightValue = numeric(b, field);
      if (leftValue != rightValue)
        return sort.endsWith("asc") ? leftValue < rightValue : leftValue > rightValue;
    }
    return false;
  });

  m_galleryList->clear();
  int restoreRow = -1;
  for (const int i : visible) {
    const auto& item = m_gallery.at(i);
    const auto metadata = item.value("download_metadata").toObject();
    const QString key = galleryImageKey(item);
    const QString official = item.value("official").toBool() ? tr(" · Official") : QString();
    const QString unavailable = isGalleryUnavailable(item)
                                    ? tr(" · Unavailable")
                                    : QString();
    const auto updated = ModlistMetadata::sourceUpdated(item);
    const QString updatedText = updated
        ? tr("Updated %1").arg(ModlistMetadata::localDateLabel(*updated))
        : tr("Update date not provided");
    auto* row = new QListWidgetItem(
        QStringLiteral("%1\n%2 · %3\n%4%5\n%6")
            .arg(item.value("title").toString(), item.value("author").toString(),
                 displayGameName(item.value("game").toString()),
                 formatBytes(metadata.value("SizeOfArchives").toVariant().toLongLong()),
                 official + unavailable, updatedText),
        m_galleryList);
    row->setData(Qt::UserRole, i);
    row->setData(Qt::UserRole + 1, item.value("machine_name").toString());
    row->setIcon(m_imageIcons.value(key, galleryPlaceholder()));
    row->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
    row->setToolTip(item.value("description").toString() + QStringLiteral("\n")
                   + (updated ? tr("List updated: %1").arg(ModlistMetadata::preciseLocalTooltip(*updated))
                              : updatedText));
    if (!selectedMachine.isEmpty()
        && item.value("machine_name").toString() == selectedMachine)
      restoreRow = m_galleryList->count() - 1;
    queueThumbnail(item);
  }
  m_resultCount->setText(tr("%1 modlists").arg(visible.size()));
  if (m_galleryList->count() > 0)
    m_galleryList->setCurrentRow(restoreRow >= 0 ? restoreRow : 0);
  else
    m_details->setText(tr("No modlists match these filters."));
  pumpThumbnailQueue();
}

void Clf3InstallerDialog::updateGameFilter()
{
  if (!m_galleryLoaded) {
    m_installedOnly->setEnabled(false);
    m_installedOnly->setText(tr("Installed games only (waiting for gallery)"));
    return;
  }
  const QString selected = m_gameFilter->currentData().toString();
  QList<QPair<QString, QString>> games;
  QSet<QString> seen;
  for (const auto& item : m_gallery) {
    const QString raw = item.value("game").toString();
    if (raw.isEmpty() || seen.contains(raw.toLower())) continue;
    if (m_installedOnly->isChecked()
        && !m_installedGames.contains(raw.toLower()))
      continue;
    seen.insert(raw.toLower());
    const QString installed = m_installedGames.contains(raw.toLower())
                                  ? tr(" (installed)")
                                  : QString();
    games.push_back({raw, displayGameName(raw) + installed});
  }
  std::sort(games.begin(), games.end(), [](const auto& a, const auto& b) {
    return QString::localeAwareCompare(a.second, b.second) < 0;
  });

  const QSignalBlocker blocker(m_gameFilter);
  m_gameFilter->clear();
  m_gameFilter->addItem(m_installedOnly->isChecked() ? tr("All installed games")
                                                      : tr("All games"),
                        QString());
  for (const auto& [raw, display] : games) m_gameFilter->addItem(display, raw);
  const int selectedIndex = m_gameFilter->findData(selected);
  m_gameFilter->setCurrentIndex(qMax(0, selectedIndex));

  m_installedOnly->setEnabled(!m_installedGames.isEmpty());
  m_installedOnly->setText(
      m_installedGames.isEmpty()
          ? tr("Installed games only (none detected)")
          : tr("Installed games only (%1)").arg(m_installedGames.size()));
  if (m_installedGames.isEmpty()) m_installedOnly->setChecked(false);
}

void Clf3InstallerDialog::queueThumbnail(const QJsonObject& item)
{
  const QString key = galleryImageKey(item);
  const QUrl url(item.value("links").toObject().value("image").toString());
  if (key.isEmpty() || !url.isValid() || url.scheme().isEmpty()
      || m_imageIcons.contains(key) || m_imageQueued.contains(key))
    return;

  const QPixmap cached = cachedImage(url);
  if (!cached.isNull()) {
    m_imageIcons.insert(key, QIcon(croppedImage(cached, 240, 135)));
    for (int row = 0; row < m_galleryList->count(); ++row) {
      auto* listItem = m_galleryList->item(row);
      const auto& metadata = m_gallery.at(listItem->data(Qt::UserRole).toInt());
      if (galleryImageKey(metadata) == key) listItem->setIcon(m_imageIcons.value(key));
    }
    return;
  }
  m_imageQueued.insert(key);
  m_imageQueue.enqueue({key, url});
}

void Clf3InstallerDialog::pumpThumbnailQueue()
{
  while (m_activeImageRequests < 6 && !m_imageQueue.isEmpty()) {
    const auto [key, url] = m_imageQueue.dequeue();
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::PreferCache);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "Fluorine/0.3 WabbajackGallery");
    auto* reply = m_imageNetwork->get(request);
    ++m_activeImageRequests;
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, url] {
      if (reply->error() == QNetworkReply::NoError) {
        const QByteArray data = reply->readAll();
        QPixmap source;
        if (source.loadFromData(data)) {
          storeCachedImage(url, data);
          m_imageIcons.insert(key, QIcon(croppedImage(source, 240, 135)));
          for (int row = 0; row < m_galleryList->count(); ++row) {
            auto* listItem = m_galleryList->item(row);
            const auto& metadata = m_gallery.at(listItem->data(Qt::UserRole).toInt());
            if (galleryImageKey(metadata) == key) listItem->setIcon(m_imageIcons.value(key));
          }
        }
      }
      reply->deleteLater();
      --m_activeImageRequests;
      pumpThumbnailQueue();
    });
  }
}

void Clf3InstallerDialog::startActiveItem(
    const QString& itemId, const QString& name, const QString& displayName,
    const QString& subtitle, const QString& stage, const QString& imageUrl,
    qint64 total, const QString& unit)
{
  Q_UNUSED(imageUrl);
  m_progressView->startItem(itemId, name,
      m_activeDisplayNames.value(name, displayName),
      m_activeSubtitles.value(name, subtitle), stage, total, unit);
}

void Clf3InstallerDialog::updateActiveItem(const QString& itemId,
                                           qint64 completed, qint64 total,
                                           double speed, const QString& unit)
{
  m_progressView->progressItem(itemId, completed, total, speed, unit);
}

void Clf3InstallerDialog::setActiveItemMessage(const QString& itemId,
                                                const QString& message)
{
  m_progressView->messageItem(itemId, message);
}

void Clf3InstallerDialog::finishActiveItem(const QString& itemId)
{
  m_progressView->completeItem(itemId);
}

void Clf3InstallerDialog::failActiveItem(const QString& itemId,
                                         const QString& message)
{
  m_progressView->failItem(itemId, message);
  m_log->appendPlainText(message);
}

void Clf3InstallerDialog::updateInstallIdentity()
{
  const QString name = !m_listTitle.isEmpty() ? m_listTitle :
      !m_instanceName->text().isEmpty() ? m_instanceName->text() : tr("Install a modlist");
  QStringList context;
  if (!m_gameId.isEmpty()) context << displayGameName(m_gameId);
  if (!m_listAuthor.isEmpty()) context << m_listAuthor;
  for (auto* label : {m_reviewIdentity, m_installIdentity}) {
    label->setText(name);
    label->setToolTip(context.join(QStringLiteral(" · ")));
  }
  QStringList release;
  if (!m_listVersion.isEmpty()) release << tr("Version %1").arg(m_listVersion);
  release << (m_listUpdated.isValid()
                  ? tr("List updated: %1").arg(ModlistMetadata::localDateLabel(m_listUpdated))
                  : tr("Update date not provided"));
  for (auto* label : {m_reviewUpdated, m_installUpdated}) {
    label->setText(release.join(QStringLiteral(" · ")));
    label->setToolTip(ModlistMetadata::preciseLocalTooltip(m_listUpdated));
  }
  const bool hasInstructions = m_readmeUrl.isValid() &&
      (m_readmeUrl.scheme() == "https" || m_readmeUrl.scheme() == "http");
  m_reviewInstructions->setVisible(hasInstructions);
  m_installInstructions->setVisible(hasInstructions);
}

void Clf3InstallerDialog::updateAttentionPanel()
{
  const int manual = m_manualRequests.size();
  const int nexus = m_waitingNexus.size();
  m_attentionSummary->setText(tr("Needs attention: %1 manual downloads · %2 Nexus requests")
                                 .arg(manual).arg(nexus));
  m_openNexus->setVisible(m_currentNexus &&
      m_waitingNexus.contains(m_currentNexus->requestId));
  m_manualDownloads->setVisible(manual > 0);
  m_attentionPanel->setVisible(manual > 0 || nexus > 0);
}

void Clf3InstallerDialog::clearNexusRequests()
{
  for (const auto& id : std::as_const(m_waitingNexus))
    m_progressView->finishWaitingRequest(id);
  m_waitingNexus.clear();
  m_nexusQueue.clear();
  m_currentNexus.reset();
  if (m_browserDialog) m_browserDialog->hide();
  updateAttentionPanel();
}

void Clf3InstallerDialog::activateAttentionRequest(const QString& requestId)
{
  if (auto* item = m_manualRequests.value(requestId, nullptr)) {
    if (auto* row = m_manualDownloads->itemWidget(item))
      if (auto* select = row->findChild<QPushButton*>("selectManualFile")) select->click();
  } else if (m_currentNexus && m_currentNexus->requestId == requestId) {
    showNexusBrowser(*m_currentNexus);
  } else if (m_waitingNexus.contains(requestId)) {
    m_status->setText(tr("Complete the current Nexus request first. The next download will open automatically."));
    if (m_currentNexus) showNexusBrowser(*m_currentNexus);
  }
}

void Clf3InstallerDialog::detectSelectedGamePath()
{
  if (!m_game->text().trimmed().isEmpty()) return;
  const QString targetName = knownGameNameForWabbajack(m_gameId);
  if (targetName.isEmpty()) return;

  for (const auto& game : detectAllGames().games) {
    const KnownGame* known = knownGameForDetected(game);
    if ((!known || QString::fromLatin1(known->name).compare(
                       targetName, Qt::CaseInsensitive) != 0)
        && game.name.compare(targetName, Qt::CaseInsensitive) != 0)
      continue;
    if (!QFileInfo::exists(game.install_path)) continue;
    m_game->setText(QDir::cleanPath(game.install_path));
    const QString store = launcherStore(game.launcher);
    const int storeIndex = m_store->findData(store);
    if (storeIndex >= 0) m_store->setCurrentIndex(storeIndex);
    return;
  }
}

void Clf3InstallerDialog::selectGalleryItem()
{
  if (m_pages->currentIndex() != 0) return;
  const auto* row = m_galleryList->currentItem();
  if (!row) return;
  const auto item = m_gallery.value(row->data(Qt::UserRole).toInt());
  const auto links = item.value("links").toObject();
  const QString selectedGameId = item.value("game").toString();
  if (!m_gameId.isEmpty()
      && selectedGameId.compare(m_gameId, Qt::CaseInsensitive) != 0) {
    m_game->clear();
    m_store->setCurrentIndex(0);
  }
  m_source->setText(links.value("download").toString());
  m_machineName = item.value("machine_name").toString();
  m_gameId = selectedGameId;
  m_instanceName->setText(item.value("title").toString());
  m_listTitle = item.value("title").toString();
  m_listAuthor = item.value("author").toString();
  m_listVersion = item.value("version").toString();
  m_listUpdated = ModlistMetadata::sourceUpdated(item).value_or(QDateTime());
  m_readmeUrl = QUrl(links.value("readme").toString());
  updateInstallIdentity();
  const auto metadata = item.value("download_metadata").toObject();
  QString description = item.value("description").toString();
  description.replace(QStringLiteral("\\u002B"), QStringLiteral("+"),
                      Qt::CaseInsensitive);
  description.replace(QStringLiteral("\\u0026"), QStringLiteral("&"),
                      Qt::CaseInsensitive);
  QStringList tags;
  for (const auto& tag : item.value("tags").toArray()) tags.push_back(tag.toString());
  QStringList badgeParts;
  if (item.value("official").toBool()) badgeParts.push_back(tr("Official"));
  if (WabbajackPostInstall::hasAdapter(m_machineName))
    badgeParts.push_back(tr("Automatic Linux setup"));
  if (item.value("nsfw").toBool()) badgeParts.push_back(tr("NSFW"));
  if (isGalleryUnavailable(item))
    badgeParts.push_back(tr("Unavailable"));
  if (!item.value("version").toString().isEmpty())
    badgeParts.push_back(tr("Version %1").arg(item.value("version").toString()));
  badgeParts.push_back(m_listUpdated.isValid()
      ? tr("List updated: %1").arg(ModlistMetadata::localDateLabel(m_listUpdated))
      : tr("Update date not provided"));
  const QString badges = badgeParts.join(QStringLiteral(" · "));
  m_details->setToolTip(ModlistMetadata::preciseLocalTooltip(m_listUpdated));
  m_details->setText(
      QStringLiteral("<h2>%1</h2><p><b>%2</b> · %3</p><p>%4</p><p>%5</p>"
                     "<p><b>%6:</b> %7<br><b>%8:</b> %9<br><b>%10:</b> %11</p>"
                     "<p>%12</p><p><a href=\"%13\">%14</a></p>")
          .arg(item.value("title").toString().toHtmlEscaped(),
               item.value("author").toString().toHtmlEscaped(),
               displayGameName(item.value("game").toString()).toHtmlEscaped(),
               badges.toHtmlEscaped(), description.toHtmlEscaped(), tr("Download"),
               formatBytes(metadata.value("SizeOfArchives").toVariant().toLongLong()),
               tr("Installed"),
               formatBytes(metadata.value("SizeOfInstalledFiles").toVariant().toLongLong()),
               tr("Archives"),
               QString::number(metadata.value("NumberOfArchives").toInt()),
               tags.join(QStringLiteral(" · ")).toHtmlEscaped(),
               links.value("readme").toString().toHtmlEscaped(), tr("Readme")));
}

void Clf3InstallerDialog::chooseSource()
{
  const QString file = QFileDialog::getOpenFileName(
      this, tr("Choose Wabbajack Modlist"), {}, tr("Wabbajack Modlist (*.wabbajack)"));
  if (!file.isEmpty()) {
    m_source->setText(file);
    m_listTitle.clear();
    m_listAuthor.clear();
    m_listVersion.clear();
    m_listUpdated = {};
    m_readmeUrl = QUrl();
    m_machineName.clear();
    m_instanceName->setText(QFileInfo(file).completeBaseName());
    m_gameId.clear();
    m_game->clear();
    m_store->setCurrentIndex(0);
  }
}

void Clf3InstallerDialog::chooseDirectory(QLineEdit* target)
{
  const QString path = QFileDialog::getExistingDirectory(this, tr("Choose Folder"),
                                                          target->text());
  if (!path.isEmpty()) target->setText(path);
}

void Clf3InstallerDialog::showConfiguration()
{
  if (m_source->text().trimmed().isEmpty()) {
    QMessageBox::warning(this, tr("Modlist required"),
                         tr("Choose a gallery entry, local file, or URL first."));
    return;
  }
  if (m_instanceName->text().trimmed().isEmpty()) {
    const QFileInfo source(m_source->text());
    m_instanceName->setText(source.completeBaseName().isEmpty()
                                ? tr("Wabbajack Modlist")
                                : source.completeBaseName());
  }
  if (m_output->text().isEmpty())
    m_output->setText(InstanceManager::singleton().instancePath(
        InstanceManager::singleton().makeUniqueName(m_instanceName->text())));
  detectSelectedGamePath();
  updatePreflightSummary();
  updateInstallIdentity();
  m_pages->setCurrentIndex(1);
}

void Clf3InstallerDialog::startInstall()
{
  if (m_controller.isRunning() || m_postInstallRunning) return;
  // A local/cached-only list may still be installed after skipping connection.
  if (!GlobalSettings::hasNexusOAuthTokens() && !GlobalSettings::hasNexusApiKey()
      && NexusInterface::instance().getAPIUserAccount().type() == APIUserAccountTypes::None)
    connectNexus();
  if (!checkInstallation()) return;
  // The installer uses the same cache; do not run two engine updaters at once.
  m_galleryLoader.cancel();
  m_refreshGallery->setEnabled(true);
  m_stopping = false;
  m_deferredClose.reset();
  m_createdInstanceDir.clear();
  m_installStats = {};
  if (m_setupJobId.isEmpty())
    m_setupJobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  if (!savePendingJob()) {
    QMessageBox::warning(this, tr("Cannot save installation"),
                         m_pendingSaveError);
    return;
  }
  // Gallery thumbnails must not compete with the installation for bandwidth.
  m_imageQueue.clear();
  for (auto* reply : m_imageNetwork->findChildren<QNetworkReply*>()) reply->abort();
  m_pages->setCurrentIndex(2);
  updateInstallIdentity();
  m_retrySetup->setVisible(false);
  m_resumeInstall->hide();
  clearManualRequests();
  clearNexusRequests();
  m_activeDisplayNames.clear();
  m_activeSubtitles.clear();
  m_progressView->reset();
  m_progressView->setPhase(tr("Preparing installation"));
  if (m_hasStarted) m_log->appendPlainText(tr("Resuming installation using the same folders."));
  else m_log->clear();
  m_hasStarted = true;
  m_status->setText(tr("Verified archives in the download cache will be reused."));
  m_detailsToggle->setChecked(false);
  m_cancel->setEnabled(true);
  m_close->setEnabled(false);
  m_switchInstance->setEnabled(false);
  Clf3Tuning tuning = Clf3InstallUtils::loadPerfTuning();
  m_controller.startInstall(m_source->text(), m_downloads->text(), m_output->text(),
                            m_game->text(), m_machineName, tuning);
}

bool Clf3InstallerDialog::savePendingJob(const QString& stage)
{
  auto settings = Clf3InstallUtils::openSettings();
  settings->beginGroup(QStringLiteral("clf3/pending"));
  settings->setValue(QStringLiteral("source"), m_source->text());
  settings->setValue(QStringLiteral("instanceName"), m_instanceName->text());
  settings->setValue(QStringLiteral("listTitle"), m_listTitle);
  settings->setValue(QStringLiteral("listAuthor"), m_listAuthor);
  settings->setValue(QStringLiteral("listVersion"), m_listVersion);
  settings->setValue(QStringLiteral("listUpdated"), m_listUpdated.toUTC().toString(Qt::ISODateWithMs));
  settings->setValue(QStringLiteral("readme"), m_readmeUrl.toString());
  settings->setValue(QStringLiteral("output"), m_output->text());
  settings->setValue(QStringLiteral("downloads"), m_downloads->text());
  settings->setValue(QStringLiteral("game"), m_game->text());
  settings->setValue(QStringLiteral("machineName"), m_machineName);
  settings->setValue(QStringLiteral("gameId"), m_gameId);
  settings->setValue(QStringLiteral("store"), m_store->currentData());
  settings->setValue(QStringLiteral("stage"), stage);
  settings->setValue(QStringLiteral("setupJobId"), m_setupJobId);
  settings->setValue(QStringLiteral("engineVersion"), m_engineVersion->text());
  settings->setValue(QStringLiteral("stats"), QJsonDocument(m_installStats).toJson(QJsonDocument::Compact));
  settings->endGroup();
  settings->sync();
  m_pendingSaveError.clear();
  if (settings->status() == QSettings::FormatError)
    m_pendingSaveError = tr("The resume settings file is invalid:\n%1").arg(settings->fileName());
  else if (settings->status() != QSettings::NoError)
    m_pendingSaveError = tr("Cannot write resume information to:\n%1").arg(settings->fileName());
  return m_pendingSaveError.isEmpty();
}

void Clf3InstallerDialog::clearPendingJob()
{
  auto settings = Clf3InstallUtils::openSettings();
  settings->remove(QStringLiteral("clf3/pending"));
  settings->sync();
}

void Clf3InstallerDialog::offerResume()
{
  auto settings = Clf3InstallUtils::openSettings();
  settings->beginGroup(QStringLiteral("clf3/pending"));
  const QString source = settings->value(QStringLiteral("source")).toString();
  if (source.isEmpty()) {
    settings->endGroup();
    return;
  }
  const bool setupOnly = settings->value(QStringLiteral("stage")).toString()
                         == QStringLiteral("post-install");
  const auto answer = m_resumePending ? QMessageBox::Yes : QMessageBox::question(
      this, setupOnly ? tr("Continue compatibility setup?") : tr("Continue modlist installation?"),
      setupOnly ? tr("The modlist installation finished, but compatibility setup is incomplete. "
                     "Retry setup using the saved folders? Downloads will not be restarted.")
                : tr("Fluorine found an unfinished CLF3 installation. Continue it using the saved paths?"),
      QMessageBox::Yes | QMessageBox::No | QMessageBox::Discard, QMessageBox::Yes);
  m_resumePending = false;
  if (answer == QMessageBox::Discard) {
    settings->endGroup();
    clearPendingJob();
    return;
  }
  if (answer == QMessageBox::No) {
    settings->endGroup();
    return;
  }
  m_galleryLoader.cancel();
  m_source->setText(source);
  m_instanceName->setText(settings->value(QStringLiteral("instanceName")).toString());
  m_listTitle = settings->value(QStringLiteral("listTitle")).toString();
  m_listAuthor = settings->value(QStringLiteral("listAuthor")).toString();
  m_listVersion = settings->value(QStringLiteral("listVersion")).toString();
  m_listUpdated = ModlistMetadata::parseIsoDateTime(settings->value(QStringLiteral("listUpdated")).toString())
                      .value_or(QDateTime());
  m_readmeUrl = QUrl(settings->value(QStringLiteral("readme")).toString());
  m_output->setText(settings->value(QStringLiteral("output")).toString());
  m_downloads->setText(settings->value(QStringLiteral("downloads")).toString());
  m_game->setText(settings->value(QStringLiteral("game")).toString());
  m_machineName = settings->value(QStringLiteral("machineName")).toString();
  m_gameId = settings->value(QStringLiteral("gameId")).toString();
  const int storeIndex = m_store->findData(settings->value(QStringLiteral("store")));
  m_store->setCurrentIndex(qMax(0, storeIndex));
  m_setupJobId = settings->value(QStringLiteral("setupJobId")).toString();
  if (setupOnly)
    m_engineVersion->setText(settings->value(QStringLiteral("engineVersion"),
                                           tr("Resuming compatibility setup")).toString());
  m_installStats = QJsonDocument::fromJson(settings->value(QStringLiteral("stats")).toByteArray()).object();
  settings->endGroup();
  updateInstallIdentity();
  if (setupOnly) {
    m_pages->setCurrentIndex(2);
    finishInstall(m_installStats);
  } else {
    startInstall();
  }
}

void Clf3InstallerDialog::finishInstall(const QJsonObject& stats)
{
  if (m_controller.isRunning() || m_postInstallRunning) return;
  m_postInstallRunning = true;
  m_status->setText(tr("Applying Fluorine compatibility setup…"));
  m_log->appendPlainText(tr("Phase: Fluorine compatibility setup"));
  m_retrySetup->setVisible(false);
  m_resumeInstall->hide();
  clearManualRequests();
  CuratedGuideNxmBroker::instance().clearConsumer(QStringLiteral("clf3"));
  clearNexusRequests();
  m_createdInstanceDir = m_output->text();
  m_installStats = stats;
  m_cancel->setEnabled(false);
  m_close->setEnabled(false);
  m_switchInstance->setEnabled(false);
  m_progressView->setPhase(tr("Compatibility setup"));

  if (m_setupJobId.isEmpty()) m_setupJobId = QUuid::createUuid().toString(QUuid::WithoutBraces);
  // Persist the stage before any setup work or nested game-folder dialog.
  if (!savePendingJob(QStringLiteral("post-install"))) {
    failPostInstall(m_pendingSaveError);
    return;
  }
  if (!QFileInfo(m_createdInstanceDir).isDir()
      || QDir(m_createdInstanceDir).isEmpty()) {
    failPostInstall(tr("The installed modlist folder is missing or empty. Restore it before retrying setup."));
    return;
  }
  const QString resolvedByClf3 = stats.value(QStringLiteral("game_path")).toString();
  if (!resolvedByClf3.isEmpty() && QFileInfo::exists(resolvedByClf3))
    m_game->setText(QDir::cleanPath(resolvedByClf3));
  detectSelectedGamePath();
  if (WabbajackPostInstall::hasAdapter(m_machineName)
      && m_game->text().trimmed().isEmpty()) {
    const QString selected = QFileDialog::getExistingDirectory(
        this, tr("Choose the original %1 game folder").arg(displayGameName(m_gameId)),
        QDir::homePath());
    if (selected.isEmpty()) {
      failPostInstall(
          tr("The modlist is installed, but automatic Linux compatibility setup was "
             "deferred because no original game folder was selected."));
      return;
    }
    m_game->setText(QDir::cleanPath(selected));
  }
  if (!savePendingJob(QStringLiteral("post-install"))) {
    failPostInstall(m_pendingSaveError);
    return;
  }
  m_postInstall->start({m_machineName, m_gameId, m_createdInstanceDir,
                        m_downloads->text(), m_game->text(),
                        m_store->currentData().toString(), m_setupJobId});
}

void Clf3InstallerDialog::completePostInstall(const QStringList& adjustments,
                                              const QStringList& warnings)
{
  QStringList allWarnings = warnings;
  const QString receiptError = recordInstalledMetadata();
  if (!receiptError.isEmpty()) allWarnings << receiptError;
  m_postInstallRunning = false;
  m_retrySetup->setVisible(false);
  CuratedGuideNxmBroker::instance().clearConsumer(
      QStringLiteral("wabbajack-postinstall"));
  clearNexusRequests();
  clearPendingJob();
  if (QFileInfo::exists(QDir(m_createdInstanceDir).filePath("ModOrganizer.ini"))) {
    InstanceManager::registerPortableInstance(m_createdInstanceDir);
    m_switchInstance->setEnabled(true);
    m_status->setText(allWarnings.isEmpty()
                          ? tr("Installation, compatibility setup, and instance "
                               "registration complete.")
                          : tr("Installation complete with compatibility warnings."));
  } else {
    m_status->setText(tr("Installation complete. ModOrganizer.ini was not produced, so the "
                         "folder was not registered automatically."));
  }
  m_progressView->setPhase(allWarnings.isEmpty() ? tr("Installation complete")
                                             : tr("Completed with warnings"));
  m_progressView->setProgress(1, 1, QStringLiteral("items"));
  m_log->appendPlainText(tr("Completed: %1 downloaded, %2 reused, %3 failed.")
                             .arg(m_installStats.value("archives_downloaded").toInt())
                             .arg(m_installStats.value("archives_skipped").toInt())
                             .arg(m_installStats.value("archives_failed").toInt()));
  for (const QString& adjustment : adjustments)
    m_log->appendPlainText(tr("Setup: %1").arg(adjustment));
  for (const QString& warning : allWarnings)
    m_log->appendPlainText(tr("Warning: %1").arg(warning));
  if (!allWarnings.isEmpty()) m_detailsToggle->setChecked(true);
  m_cancel->setEnabled(false);
  m_close->setEnabled(true);
  closeWhenIdle();
}

void Clf3InstallerDialog::failPostInstall(const QString& error)
{
  m_postInstallRunning = false;
  m_retrySetup->setVisible(true);
  CuratedGuideNxmBroker::instance().clearConsumer(
      QStringLiteral("wabbajack-postinstall"));
  clearNexusRequests();
  m_status->setText(
      tr("Modlist installation complete. Compatibility setup needs attention: %1")
          .arg(error));
  m_log->appendPlainText(tr("POST-INSTALL FAILED: %1").arg(error));
  const QString receiptError = recordInstalledMetadata();
  if (!receiptError.isEmpty()) m_log->appendPlainText(receiptError);
  m_detailsToggle->setChecked(true);
  m_progressView->stopActive(tr("Compatibility setup stopped"));
  m_progressView->setPhaseEnded(tr("Installed · setup incomplete"));
  if (QFileInfo::exists(QDir(m_createdInstanceDir).filePath("ModOrganizer.ini")))
    InstanceManager::registerPortableInstance(m_createdInstanceDir);
  m_cancel->setEnabled(false);
  m_close->setEnabled(true);
  closeWhenIdle();
}

QString Clf3InstallerDialog::recordInstalledMetadata()
{
  // Only an engine completion establishes the local installation date. Old
  // pending jobs and compatibility-only retries must not acquire a new date.
  const auto installedAt = ModlistMetadata::parseIsoDateTime(
      m_installStats.value(QStringLiteral("fluorineInstalledAt")).toString());
  if (!installedAt || !QFileInfo::exists(QDir(m_createdInstanceDir).filePath("ModOrganizer.ini")))
    return {};
  ModlistMetadata::Receipt receipt;
  receipt.sourceUrl = m_source->text();
  receipt.title = m_listTitle.isEmpty() ? m_instanceName->text() : m_listTitle;
  receipt.author = m_listAuthor;
  receipt.version = m_listVersion;
  receipt.sourceUpdatedUtc = m_listUpdated;
  receipt.installedAtUtc = *installedAt;
  QString error;
  if (!ModlistMetadata::writeReceipt(m_createdInstanceDir, receipt, &error))
    return tr("Could not save the modlist update dates: %1").arg(error);
  return {};
}

void Clf3InstallerDialog::queueNexus(const QString& requestId,
                                     const QString& archiveName,
                                     const QString& domain, int modId, int fileId,
                                     qint64 expectedSize)
{
  if (m_stopping) return;
  m_nexusQueue.enqueue({QStringLiteral("clf3"), requestId, archiveName, domain,
                        modId, fileId, expectedSize});
  if (NexusInterface::instance().getAPIUserAccount().type() != APIUserAccountTypes::Premium) {
    m_waitingNexus.insert(requestId);
    m_progressView->setWaitingRequest(requestId, archiveName,
                                    tr("Authorize this download on Nexus Mods"));
    updateAttentionPanel();
  }
  beginNextNexus();
}

void Clf3InstallerDialog::queuePostInstallNexus(
    const QString& requestId, const QString& artifactName,
    const QString& domain, int modId, int fileId, qint64 expectedSize)
{
  m_nexusQueue.enqueue({QStringLiteral("wabbajack-postinstall"), requestId,
                        artifactName, domain, modId, fileId, expectedSize});
  m_waitingNexus.insert(requestId);
  m_progressView->setWaitingRequest(requestId, artifactName,
                                  tr("Authorize this compatibility download on Nexus Mods"));
  updateAttentionPanel();
  beginNextNexus();
}

void Clf3InstallerDialog::beginNextNexus()
{
  if (m_stopping || m_currentNexus || m_nexusQueue.isEmpty()) return;
  m_currentNexus = m_nexusQueue.dequeue();
  if (NexusInterface::instance().getAPIUserAccount().type() == APIUserAccountTypes::None) {
    connectNexus();
    if (m_stopping || !m_currentNexus) return;
  }
  const auto account = NexusInterface::instance().getAPIUserAccount();
  if (account.type() == APIUserAccountTypes::None) {
    const QString requestId = m_currentNexus->requestId;
    if (m_currentNexus->consumer == QStringLiteral("clf3"))
      m_controller.rejectRequest(requestId,
                                 tr("Sign in to Nexus in Fluorine before installing."));
    else
      m_postInstall->rejectNexusAuthorization(
          requestId, tr("Sign in to Nexus in Fluorine before installing."));
    m_progressView->finishWaitingRequest(requestId);
    m_waitingNexus.remove(requestId);
    m_currentNexus.reset();
    updateAttentionPanel();
    QTimer::singleShot(0, this, &Clf3InstallerDialog::beginNextNexus);
  } else if (m_currentNexus->consumer == QStringLiteral("clf3")
             && account.type() == APIUserAccountTypes::Premium) {
    resolveNexus(*m_currentNexus);
  } else {
    CuratedGuideNxmBroker::instance().expectForConsumer(
        m_currentNexus->consumer, m_currentNexus->requestId, m_currentNexus->domain,
        m_currentNexus->modId, m_currentNexus->fileId, account.id().toInt());
    showNexusBrowser(*m_currentNexus);
  }
}

void Clf3InstallerDialog::showNexusBrowser(const NexusRequest& request)
{
  m_waitingNexus.insert(request.requestId);
  m_progressView->setWaitingRequest(request.requestId, request.archiveName,
                                  tr("Choose Mod Manager Download, then Slow Download"));
  updateAttentionPanel();
  const QUrl page(QString("https://www.nexusmods.com/%1/mods/%2?tab=files&file_id=%3&nmm=1")
                      .arg(request.domain)
                      .arg(request.modId)
                      .arg(request.fileId));
  m_status->setText(tr("Waiting for your Nexus download click: %1")
                        .arg(request.archiveName));
#ifdef MO2_WEBENGINE
  if (m_browserDialog) m_browserDialog->deleteLater();
  m_browserDialog = new QDialog(this);
  m_browserDialog->setAttribute(Qt::WA_DeleteOnClose, false);
  m_browserDialog->setWindowTitle(tr("Authorize Nexus Download — %1")
                                      .arg(request.archiveName));
  m_browserDialog->resize(1100, 800);
  auto* layout = new QVBoxLayout(m_browserDialog);
  auto* help = new QLabel(tr("Choose Mod Manager Download and then Slow Download. "
                             "Fluorine will return to installation progress automatically."));
  help->setWordWrap(true);
  layout->addWidget(help);
  auto* view = new QWebEngineView(m_browserDialog);
  auto* authPage = new NexusAuthorizationPage(
      nexusProfile(), [](const QUrl& url) {
        CuratedGuideNxmBroker::instance().tryConsume(url.toString());
      }, view);
  view->setPage(authPage);
  layout->addWidget(view, 1);
  auto* buttons = new QHBoxLayout;
  auto* external = new QPushButton(tr("Open in External Browser"));
  auto* clearSession = new QPushButton(tr("Clear Nexus Session"));
  auto* hide = new QPushButton(tr("Hide"));
  buttons->addWidget(external);
  buttons->addWidget(clearSession);
  buttons->addStretch();
  buttons->addWidget(hide);
  layout->addLayout(buttons);
  connect(external, &QPushButton::clicked, m_browserDialog,
          [page] { QDesktopServices::openUrl(page); });
  connect(clearSession, &QPushButton::clicked, m_browserDialog, [] {
    nexusProfile()->cookieStore()->deleteAllCookies();
  });
  connect(hide, &QPushButton::clicked, m_browserDialog, &QDialog::hide);
  view->load(page);
  m_browserDialog->show();
  m_browserDialog->raise();
  m_browserDialog->activateWindow();
#else
  QDesktopServices::openUrl(page);
  QMessageBox::information(
      this, tr("Nexus authorization"),
      tr("The embedded browser is unavailable in this build. Complete the Nexus "
         "download click in your browser; Fluorine will capture the NXM link."));
#endif
}

void Clf3InstallerDialog::resolveNexus(const NexusRequest& request,
                                      const QString& nxmUrl)
{
  QUrl endpoint(QString("https://api.nexusmods.com/v1/games/%1/mods/%2/files/%3/download_link.json")
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
    m_progressView->finishWaitingRequest(request.requestId);
    m_waitingNexus.remove(request.requestId);
    m_currentNexus.reset();
    updateAttentionPanel();
    beginNextNexus();
    return;
  }
  connect(reply, &QNetworkReply::finished, this, [this, reply, request] {
    reply->deleteLater();
    if (m_stopping || !m_currentNexus
        || m_currentNexus->requestId != request.requestId
        || m_currentNexus->consumer != request.consumer) return;
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
      if (m_browserDialog) m_browserDialog->hide();
    }
    m_progressView->finishWaitingRequest(request.requestId);
    m_waitingNexus.remove(request.requestId);
    m_currentNexus.reset();
    updateAttentionPanel();
    QTimer::singleShot(350, this, &Clf3InstallerDialog::beginNextNexus);
  });
}

void Clf3InstallerDialog::nexusLinkAccepted(const QString& consumer,
                                            const QString& requestId,
                                            const QString& url)
{
  if (!m_currentNexus || m_currentNexus->consumer != consumer
      || m_currentNexus->requestId != requestId)
    return;
  if (consumer == QStringLiteral("clf3")) {
    resolveNexus(*m_currentNexus, url);
    return;
  }
  if (consumer == QStringLiteral("wabbajack-postinstall")) {
    if (m_browserDialog) m_browserDialog->hide();
    m_progressView->finishWaitingRequest(requestId);
    m_waitingNexus.remove(requestId);
    m_currentNexus.reset();
    updateAttentionPanel();
    m_postInstall->provideNexusAuthorization(requestId, url);
    QTimer::singleShot(0, this, &Clf3InstallerDialog::beginNextNexus);
  }
}

void Clf3InstallerDialog::requestManualFile(const QString& requestId,
                                            const QString& archiveName,
                                            const QString& url,
                                            const QString& prompt,
                                            qint64 expectedSize,
                                            const QString& expectedHash)
{
  if (m_stopping || !m_controller.isRunning() || m_manualRequests.contains(requestId)) return;
  Q_UNUSED(expectedHash); // CLF3 verifies the hash when the selected file is submitted.
  m_log->appendPlainText(tr("Manual download required: %1").arg(archiveName));
  auto* item = new QListWidgetItem(m_manualDownloads);
  m_manualRequests.insert(requestId, item);
  auto* row = new QWidget;
  auto* layout = new QVBoxLayout(row);
  auto* description = new QLabel(tr("Manual download: %1 (%2)\n%3")
                                    .arg(archiveName, formatBytes(expectedSize), prompt));
  description->setTextFormat(Qt::PlainText);
  description->setWordWrap(true);
  layout->addWidget(description);
  auto* buttons = new QHBoxLayout;
  auto* open = new QPushButton(tr("Open download page"));
  auto* select = new QPushButton(tr("Select downloaded file…"));
  select->setObjectName(QStringLiteral("selectManualFile"));
  const QUrl page(url);
  open->setEnabled(page.isValid() && (page.scheme() == "https" || page.scheme() == "http"));
  buttons->addWidget(open);
  buttons->addWidget(select);
  buttons->addStretch();
  layout->addLayout(buttons);
  item->setSizeHint(row->sizeHint());
  m_manualDownloads->setItemWidget(item, row);
  m_progressView->setWaitingRequest(requestId, archiveName, prompt);
  updateAttentionPanel();
  connect(open, &QPushButton::clicked, this, [page] { QDesktopServices::openUrl(page); });
  connect(select, &QPushButton::clicked, this,
          [this, requestId, archiveName, expectedSize] {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select downloaded archive: %1").arg(archiveName), m_downloads->text());
    // Requests can finish/cancel while the file picker runs its nested event loop.
    if (path.isEmpty() || m_stopping || !m_manualRequests.contains(requestId)) return;
    const QFileInfo file(path);
    if (!file.isFile() || !file.isReadable()
        || (expectedSize > 0 && file.size() != expectedSize)) {
      QMessageBox::warning(this, tr("Archive does not match"),
                           tr("Select a readable archive with the expected size: %1. "
                              "This download remains in the queue.").arg(formatBytes(expectedSize)));
      return;
    }
    // CLF3 performs the authoritative hash check before accepting this archive.
    m_controller.sendManualFile(requestId, path);
    delete m_manualRequests.take(requestId);
    m_progressView->finishWaitingRequest(requestId);
    updateAttentionPanel();
  });
}

void Clf3InstallerDialog::done(int result)
{
  if (m_tabs->collections() && m_tabs->collections()->isBusy()) {
    m_deferredClose = result;
    m_tabs->collections()->done(QDialog::Rejected);
    return;
  }
  if (m_controller.isRunning() || m_postInstallRunning) {
    m_deferredClose = result;
    if (m_controller.isRunning()) {
      cancelInstall();
    } else {
      // Native patchers may be updating game executables. Keep their owner alive
      // until completion, including any required Nexus authorization.
      m_status->setText(tr("Finishing compatibility setup before closing…"));
    }
    return;
  }
  m_galleryLoader.cancel();
  QDialog::done(result);
}

void Clf3InstallerDialog::cancelInstall()
{
  if (!m_controller.isRunning() || m_stopping) return;
  m_stopping = true;
  m_cancel->setEnabled(false);
  m_close->setEnabled(false);
  m_status->setText(tr("Stopping safely… Your installation can be continued later."));
  CuratedGuideNxmBroker::instance().clearConsumer(QStringLiteral("clf3"));
  clearNexusRequests();
  clearManualRequests();
  m_controller.cancel();
}

void Clf3InstallerDialog::closeWhenIdle()
{
  if (!m_deferredClose || m_controller.isRunning() || m_postInstallRunning
      || (m_tabs->collections() && m_tabs->collections()->isBusy())) return;
  const int result = *m_deferredClose;
  m_deferredClose.reset();
  QTimer::singleShot(0, this, [this, result] { done(result); });
}

void Clf3InstallerDialog::clearManualRequests()
{
  for (auto it = m_manualRequests.cbegin(); it != m_manualRequests.cend(); ++it)
    m_progressView->finishWaitingRequest(it.key());
  m_manualRequests.clear();
  m_manualDownloads->clear();
  updateAttentionPanel();
}

void Clf3InstallerDialog::exportLog()
{
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Export installation log"),
      QDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
          .filePath(QStringLiteral("fluorine-install.log")), tr("Log files (*.log);;All files (*)"));
  if (path.isEmpty()) return;
  const QString report = QStringLiteral("Fluorine %1\n%2\n%3 UTC\nStatus: %4\n\n%5\n")
                             .arg(QCoreApplication::applicationVersion(),
                                  m_engineVersion->text(),
                                  QDateTime::currentDateTimeUtc().toString(Qt::ISODate),
                                  m_status->text(), m_log->toPlainText());
  const auto bytes = Clf3InstallUtils::redactLog(report).toUtf8();
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    QMessageBox::warning(this, tr("Log export failed"), file.errorString());
}

QJsonObject Clf3InstallerDialog::selectedDownloadMetadata() const
{
  // Editing a URL must not reuse another list's size estimate.
  for (const auto& item : m_gallery) {
    if (item.value("links").toObject().value("download").toString()
        == m_source->text().trimmed())
      return item.value("download_metadata").toObject();
  }
  return {};
}

namespace
{
QStorageInfo storageForPath(QString path)
{
  // QStorageInfo cannot reliably resolve a folder that has not been created yet.
  while (!path.isEmpty() && !QFileInfo::exists(path)) {
    const QString parent = QFileInfo(path).absolutePath();
    if (parent == path) break;
    path = parent;
  }
  return QStorageInfo(path);
}

QVector<Clf3InstallUtils::SpaceRequirement> installationSpace(
    const QString& output, const QString& downloads, const QJsonObject& metadata)
{
  const qint64 archives = qMax(qint64(0), metadata.value("SizeOfArchives").toVariant().toLongLong());
  const qint64 installed = qMax(qint64(0), metadata.value("SizeOfInstalledFiles").toVariant().toLongLong());
  auto requirement = [](const QString& path, const QString& purpose, qint64 bytes) {
    const QStorageInfo storage = storageForPath(path);
    const QString device = storage.device().isEmpty() ? storage.rootPath()
                                                     : QString::fromUtf8(storage.device());
    return Clf3InstallUtils::SpaceRequirement{
        device, purpose, storage.isValid() && storage.isReady() ? storage.bytesAvailable() : -1, bytes};
  };
  // CLF3 stages extracted files and texture spills under the output directory.
  return Clf3InstallUtils::combineSpaceRequirements({
      requirement(output, QObject::tr("Installed files"), installed),
      requirement(downloads, QObject::tr("Downloads"), archives),
      requirement(output, QObject::tr("Temporary extraction"),
                  Clf3InstallUtils::temporarySpaceEstimate(archives, installed))});
}
}

void Clf3InstallerDialog::updatePreflightSummary()
{
  const auto metadata = selectedDownloadMetadata();
  QStringList lines;
  for (const auto& space : installationSpace(m_output->text(), m_downloads->text(), metadata)) {
    lines << tr("%1: %2 estimated additional space; %3 available.")
                 .arg(space.purpose, formatBytes(space.required),
                      space.available < 0 ? tr("unknown") : formatBytes(space.available));
  }
  if (metadata.value("SizeOfArchives").toVariant().toLongLong() <= 0
      || metadata.value("SizeOfInstalledFiles").toVariant().toLongLong() <= 0)
    lines << tr("A complete gallery size estimate is unavailable. The space estimate above is incomplete.");
  lines << tr("Estimates include a temporary extraction allowance of 20% (at least 2 GiB). "
              "Actual peak usage may be higher; cached files may reduce what is needed.");
  const auto account = NexusInterface::instance().getAPIUserAccount();
  if (account.type() == APIUserAccountTypes::None)
    lines << tr("Nexus: not signed in. Use Connect to Nexus in the gallery before installing a list "
                "that needs new Nexus downloads. Cached and public downloads can still be used.");
  else if (account.type() == APIUserAccountTypes::Premium)
    lines << tr("Nexus: Premium account connected.");
  else
    lines << tr("Nexus: account connected. Free downloads require your browser clicks.");
  m_preflightSummary->setText(lines.join('\n'));
}

bool Clf3InstallerDialog::checkInstallation()
{
  detectSelectedGamePath();
  for (auto* edit : {m_source, m_output, m_downloads, m_game}) edit->setText(edit->text().trimmed());
  const QString source = m_source->text();
  auto fail = [this](const QString& error) {
    QMessageBox::warning(this, tr("Installation checks"), error);
    return false;
  };
  if (source.isEmpty() || m_output->text().isEmpty() || m_downloads->text().isEmpty())
    return fail(tr("Source, instance, and download paths are required."));
  const QUrl sourceUrl(source);
  const bool remote = sourceUrl.scheme() == QStringLiteral("https")
                      || sourceUrl.scheme() == QStringLiteral("http");
  if (remote) {
    if (!sourceUrl.isValid() || sourceUrl.host().isEmpty())
      return fail(tr("Enter a valid HTTP or HTTPS modlist URL."));
  } else {
    if (!QFileInfo(source).isFile() || !QFileInfo(source).isReadable())
      return fail(tr("The local modlist file does not exist or cannot be read."));
    m_source->setText(QFileInfo(source).absoluteFilePath());
  }
  if (!m_game->text().isEmpty()
      && (!QFileInfo(m_game->text()).isDir() || !QFileInfo(m_game->text()).isReadable()))
    return fail(tr("The selected game folder does not exist or cannot be read."));
  for (auto* edit : {m_output, m_downloads}) {
    if (Clf3InstallUtils::pathsOverlap(edit->text(), m_game->text()))
      return fail(tr("The instance and download folders must be separate from the original game folder."));
    const QString error = Clf3InstallUtils::prepareWritableDirectory(edit->text());
    if (!error.isEmpty()) return fail(error);
    edit->setText(QFileInfo(edit->text()).canonicalFilePath());
  }
  if (m_output->text() == m_downloads->text())
    return fail(tr("Choose different folders for the installed instance and download cache."));

  updatePreflightSummary();
  QStringList warnings;
  for (const auto& space : installationSpace(m_output->text(), m_downloads->text(), selectedDownloadMetadata())) {
    if (space.available < 0)
      warnings << tr("Available space could not be measured for %1.").arg(space.purpose);
    else if (space.available < space.required)
      warnings << tr("%1: %2 estimated additional space, but only %3 is available.")
                      .arg(space.purpose, formatBytes(space.required), formatBytes(space.available));
  }
  if (NexusInterface::instance().getAPIUserAccount().type() == APIUserAccountTypes::None)
    warnings << tr("You are not signed in to Nexus. New Nexus downloads will require a connected "
                   "account; cached and public downloads can still be used.");
  if (!warnings.isEmpty()) {
    QMessageBox box(QMessageBox::Warning, tr("Review installation checks"),
                    warnings.join(QStringLiteral("\n\n"))
                        + tr("\n\nSpace estimates include temporary extraction and do not subtract cached files. "
                             "Continue only if these folders have enough space for this installation."),
                    QMessageBox::NoButton, this);
    auto* back = box.addButton(tr("Back"), QMessageBox::RejectRole);
    auto* proceed = box.addButton(tr("Continue"), QMessageBox::AcceptRole);
    box.setDefaultButton(back);
    box.exec();
    if (box.clickedButton() != proceed) return false;
  }
  return true;
}
