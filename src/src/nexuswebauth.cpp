#include "nexuswebauth.h"

#ifdef MO2_WEBENGINE
#include <QApplication>
#include <QStandardPaths>
#include <QWebEngineProfile>

NexusAuthorizationPage::NexusAuthorizationPage(QWebEngineProfile* profile,
                                               std::function<void(const QUrl&)> accepted,
                                               QObject* parent)
    : QWebEnginePage(profile, parent), m_accepted(std::move(accepted))
{}

bool NexusAuthorizationPage::acceptNavigationRequest(const QUrl& url, NavigationType type,
                                                     bool mainFrame)
{
  if (url.scheme().compare(QStringLiteral("nxm"), Qt::CaseInsensitive) == 0) {
    m_accepted(url);
    return false;
  }
  return QWebEnginePage::acceptNavigationRequest(url, type, mainFrame);
}

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
#endif
