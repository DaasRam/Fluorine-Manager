#pragma once

#ifdef MO2_WEBENGINE
#include <QWebEnginePage>
#include <functional>

class QWebEngineProfile;

// Embedded Nexus page that hands `nxm://` links to a callback instead of
// navigating to them. Shared by every installer that needs a download click.
class NexusAuthorizationPage : public QWebEnginePage
{
public:
  NexusAuthorizationPage(QWebEngineProfile* profile,
                         std::function<void(const QUrl&)> accepted, QObject* parent);

protected:
  bool acceptNavigationRequest(const QUrl& url, NavigationType type,
                               bool mainFrame) override;

private:
  std::function<void(const QUrl&)> m_accepted;
};

// The persistent web profile holding the user's Nexus session. One instance
// for the whole application: Qt rejects two profiles with the same name.
QWebEngineProfile* nexusProfile();
#endif
