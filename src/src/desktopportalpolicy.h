#ifndef DESKTOP_PORTAL_POLICY_H
#define DESKTOP_PORTAL_POLICY_H

#include <QByteArray>

namespace env::desktopportal
{

inline bool shouldSelectBundledPortalTheme(const QByteArray& currentTheme,
                                           bool portalPluginAvailable)
{
  return currentTheme.isEmpty() && portalPluginAvailable;
}

}  // namespace env::desktopportal

#endif  // DESKTOP_PORTAL_POLICY_H
