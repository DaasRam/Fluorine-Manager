#pragma once

#include <QMap>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <optional>
#include <string>

// Legacy field: one NAME=value per line, with literal values. Kept for migration.
std::optional<QMap<QString, QString>> parseExecutableEnvironment(
    const QString& text, QString* error = nullptr);

struct LaunchWrapperOptions
{
  QStringList commands;
  QMap<QString, QString> environment;
};

// Shared syntax for global and per-executable wrappers. Uses Qt command-line
// quoting, accepts NAME=value tokens and an optional %command% marker.
std::optional<LaunchWrapperOptions> parseLaunchWrapperOptions(
    const QString& text, QString* error = nullptr);

// Upgrade the old literal, one-assignment-per-line field without turning spaces
// or quotes in saved values into wrapper commands.
QString wrapperOptionsFromLegacyEnvironment(const QString& text);

// Convert a game language name, short code, or locale tag to a UTF-8 POSIX locale.
// Unknown keys return empty so callers can retain the host locale.
QString gameLanguageToLocale(const QString& language);

// Read Bethesda-style [General] sLanguage from the resolved game INIs. The
// plugin's main INI must be first; a declared matching *Custom.ini overrides it.
// Paths must already refer to the selected profile or the game's documents.
QString gameLocaleFromIniFiles(const QStringList& iniFiles);

// Prepare only the child Proton environment. Explicit wrapper/executable locale
// variables take precedence over the selected game language, which takes
// precedence over inherited host categories. Call after merging overrides.
void prepareProtonLocale(
    QProcessEnvironment& environment,
    const QString& selectedGameLocale = {},
    const QMap<QString, QString>& explicitLocaleOverrides = {});

inline constexpr const char kFluorineLaunchTokenEnvironment[] =
    "FLUORINE_LAUNCH_TOKEN";

// Match a NUL-separated /proc/<pid>/environ against a Wine prefix and/or a
// per-launch token. Native launches require the token; Wine launches can still
// use their prefix when an older helper process did not inherit the token.
bool processEnvironmentMatchesLaunch(const QByteArray& processEnvironment,
                                     const QString& expectedWinePrefix,
                                     const QString& launchToken);

// Serialize Linux argv for the legacy command-line parser and IPC forwarding.
// Decode UTF-8, and protect quotes/backslashes from boost::split_unix.
std::wstring commandLineFromUtf8Arguments(int argc, char* const argv[]);
