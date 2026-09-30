#ifndef LOOTMANAGER_H
#define LOOTMANAGER_H

#include <QString>
#include <atomic>
#include <functional>

/// Returns the LOOT install directory: ~/.local/share/fluorine/tools/loot
QString lootInstallDir();

/// Returns true when the Fluorine-managed, Wine-compatible LOOT build is
/// installed. Legacy/unversioned installs are refreshed on the next sort.
bool isLootInstalled();

/// Returns the path to LOOT.exe, or empty if not installed.
QString getLootExePath();

/// Download and install Fluorine's pinned Wine-compatible LOOT release.
/// The managed application directory is transactionally replaced; LOOT's
/// per-game data and profiles are outside this directory and are untouched.
/// Returns empty string on success, or an error message.
QString downloadLoot(const std::function<void(float)>& progressCb,
                     const std::function<void(const QString&)>& statusCb,
                     const std::atomic_bool* cancelFlag = nullptr);

#endif  // LOOTMANAGER_H
