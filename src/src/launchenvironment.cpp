#include "launchenvironment.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTextStream>
#include <algorithm>

std::wstring commandLineFromUtf8Arguments(int argc, char* const argv[])
{
  QStringList arguments;
  for (int i = 0; i < argc; ++i) {
    QString argument = QString::fromUtf8(argv[i]);
    argument.replace('\\', "\\\\");
    argument.replace('"', "\\\"");
    argument.replace('\'', "\\'");
    arguments.append('"' + argument + '"');
  }
  return arguments.join(' ').toStdWString();
}

namespace
{
QString utf8Locale(QString locale)
{
  // A locale's language and modifier are independent of its codeset.
  // Keep e.g. ja_JP or sr_RS@latin when upgrading an older encoding.
  locale = locale.trimmed();
  const auto modifierPos = locale.indexOf('@');
  const QString modifier = modifierPos < 0 ? QString{} : locale.mid(modifierPos);
  QString base = modifierPos < 0 ? locale : locale.left(modifierPos);
  const auto codesetPos = base.indexOf('.');
  if (codesetPos >= 0) {
    const QString codeset = base.mid(codesetPos + 1).toUpper();
    if (codeset == "UTF-8" || codeset == "UTF8") return locale;
    base.truncate(codesetPos);
  }
  if (base.isEmpty() || base == "C" || base == "POSIX") return "C.UTF-8";
  return base + ".UTF-8" + modifier;
}

bool isLocaleVariable(const QString& name)
{
  return name == QLatin1String("LANG") ||
         name == QLatin1String("LANGUAGE") ||
         name == QLatin1String("LC_ALL") ||
         name == QLatin1String("HOST_LC_ALL") ||
         name.startsWith(QLatin1String("LC_"));
}

QString canonicalOrCleanPath(const QString& path)
{
  if (path.trimmed().isEmpty()) return {};
  const QFileInfo info(path);
  const QString canonical = info.canonicalFilePath();
  return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

std::optional<QString> iniGameLanguage(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return std::nullopt;

  // Windows INI names are case-insensitive, even when read on Linux. Avoid
  // QSettings' special treatment of [General] and backslash escapes. QTextStream
  // also handles the UTF-8/UTF-16 BOMs found in imported Windows profiles.
  QTextStream input(&file);
  bool inGeneral = false;
  while (!input.atEnd()) {
    const QString line = input.readLine().trimmed();
    if (line.isEmpty() || line.startsWith(';') || line.startsWith('#')) continue;
    if (line.startsWith('[')) {
      const auto end = line.indexOf(']');
      inGeneral = end > 0 && line.mid(1, end - 1).trimmed().compare(
          QStringLiteral("General"), Qt::CaseInsensitive) == 0;
      continue;
    }
    const auto equals = line.indexOf('=');
    if (!inGeneral || equals < 0 || line.left(equals).trimmed().compare(
            QStringLiteral("sLanguage"), Qt::CaseInsensitive) != 0) continue;

    QString value = line.mid(equals + 1).trimmed();
    if (value.startsWith('"') || value.startsWith('\'')) {
      const auto end = value.indexOf(value.front(), 1);
      return end > 0 ? value.mid(1, end - 1).trimmed() : QString{};
    }
    value = value.section(';', 0, 0).section('#', 0, 0).trimmed();
    return value;
  }
  return std::nullopt;
}
}

QString gameLanguageToLocale(const QString& language)
{
  const QString key = language.trimmed().toCaseFolded();
  static const QMap<QString, QString> locales{
      {"arabic", "ar_SA.UTF-8"},
      {"brazilian", "pt_BR.UTF-8"},
      {"bulgarian", "bg_BG.UTF-8"},
      {"czech", "cs_CZ.UTF-8"},
      {"danish", "da_DK.UTF-8"},
      {"dutch", "nl_NL.UTF-8"},
      {"english", "en_US.UTF-8"},
      {"finnish", "fi_FI.UTF-8"},
      {"french", "fr_FR.UTF-8"},
      {"german", "de_DE.UTF-8"},
      {"greek", "el_GR.UTF-8"},
      {"hungarian", "hu_HU.UTF-8"},
      {"indonesian", "id_ID.UTF-8"},
      {"italian", "it_IT.UTF-8"},
      {"japanese", "ja_JP.UTF-8"},
      {"korean", "ko_KR.UTF-8"},
      {"koreana", "ko_KR.UTF-8"},
      {"latam", "es_MX.UTF-8"},
      {"latamspanish", "es_MX.UTF-8"},
      {"norwegian", "nb_NO.UTF-8"},
      {"polish", "pl_PL.UTF-8"},
      {"portuguese", "pt_PT.UTF-8"},
      {"romanian", "ro_RO.UTF-8"},
      {"russian", "ru_RU.UTF-8"},
      {"schinese", "zh_CN.UTF-8"},
      {"spanish", "es_ES.UTF-8"},
      {"swedish", "sv_SE.UTF-8"},
      {"tchinese", "zh_TW.UTF-8"},
      {"thai", "th_TH.UTF-8"},
      {"turkish", "tr_TR.UTF-8"},
      {"ukrainian", "uk_UA.UTF-8"},
      {"vietnamese", "vi_VN.UTF-8"},
  };

  if (const auto it = locales.constFind(key); it != locales.cend()) {
    return it.value();
  }

  // Fallout/Starfield and some non-Steam installers use short language codes.
  static const QMap<QString, QString> shortNames{
      {"ar", "arabic"}, {"bg", "bulgarian"}, {"cs", "czech"},
      {"da", "danish"}, {"de", "german"}, {"el", "greek"},
      {"en", "english"}, {"es", "spanish"}, {"fi", "finnish"},
      {"fr", "french"}, {"hu", "hungarian"}, {"id", "indonesian"},
      {"it", "italian"}, {"ja", "japanese"}, {"ko", "korean"},
      {"nb", "norwegian"}, {"nl", "dutch"}, {"no", "norwegian"},
      {"pl", "polish"}, {"pt", "portuguese"}, {"ro", "romanian"},
      {"ru", "russian"}, {"sv", "swedish"}, {"th", "thai"},
      {"tr", "turkish"}, {"uk", "ukrainian"}, {"vi", "vietnamese"},
  };
  if (const auto it = shortNames.constFind(key); it != shortNames.cend()) {
    return locales.value(it.value());
  }

  // Some tools store a locale tag instead of Steam's usual language name.
  QString localeKey = key;
  localeKey.replace('-', '_');
  static const QRegularExpression localePattern(
      QStringLiteral(R"(^([a-z]{2,3})_([a-z]{2,3})((?:\.[a-z0-9_-]+)?(?:@[a-z0-9_-]+)?)$)"));
  const auto localeMatch = localePattern.match(localeKey);
  if (localeMatch.hasMatch()) {
    return utf8Locale(localeMatch.captured(1) + QLatin1Char('_')
                      + localeMatch.captured(2).toUpper() + localeMatch.captured(3));
  }

  return {};
}

QString gameLocaleFromIniFiles(const QStringList& iniFiles)
{
  if (iniFiles.isEmpty()) return {};
  auto language = iniGameLanguage(iniFiles.front());
  QString gameStem = QFileInfo(iniFiles.front()).completeBaseName();
  // Starfield exposes Prefs as its primary profile INI, followed by Custom.
  if (gameStem.endsWith(QLatin1String("Prefs"), Qt::CaseInsensitive)) {
    gameStem.chop(5);
  }
  const QString customName = gameStem + QStringLiteral("Custom.ini");
  for (const QString& path : iniFiles.mid(1)) {
    // Other Prefs and editor INIs are not overrides for the declared main INI.
    if (QFileInfo(path).fileName().compare(customName, Qt::CaseInsensitive) != 0) {
      continue;
    }
    if (const auto customLanguage = iniGameLanguage(path)) language = customLanguage;
  }
  return language ? gameLanguageToLocale(*language) : QString{};
}

void prepareProtonLocale(
    QProcessEnvironment& environment, const QString& selectedGameLocale,
    const QMap<QString, QString>& explicitLocaleOverrides)
{
  auto explicitValue = [&explicitLocaleOverrides](const QString& name) {
    const auto it = explicitLocaleOverrides.constFind(name);
    return it == explicitLocaleOverrides.cend() ? QString{} : it.value();
  };
  const bool hasExplicitGlobal =
      !explicitValue(QStringLiteral("HOST_LC_ALL")).trimmed().isEmpty() ||
      !explicitValue(QStringLiteral("LC_ALL")).trimmed().isEmpty();
  bool hasExplicitLocale = false;
  for (auto it = explicitLocaleOverrides.cbegin();
       it != explicitLocaleOverrides.cend(); ++it) {
    hasExplicitLocale = hasExplicitLocale || isLocaleVariable(it.key());
  }

  // A wrapper or executable's explicit full-locale setting is authoritative.
  // Remove inherited categories so pressure-vessel does not generate unrelated
  // host locales and then let LC_* variables compete with the requested one.
  if (hasExplicitGlobal) {
    QString all = explicitValue(QStringLiteral("HOST_LC_ALL")).trimmed();
    if (all.isEmpty()) all = explicitValue(QStringLiteral("LC_ALL")).trimmed();
    all = utf8Locale(all);
    environment.insert("HOST_LC_ALL", all);
    environment.insert("LC_ALL", all);
    const QString explicitLang = explicitValue(QStringLiteral("LANG")).trimmed();
    environment.insert("LANG", explicitLang.isEmpty() ? all : utf8Locale(explicitLang));
    for (const QString& key : environment.keys()) {
      if (key.startsWith(QLatin1String("LC_")) && key != QLatin1String("LC_ALL") &&
          !explicitLocaleOverrides.contains(key)) {
        environment.remove(key);
      }
      if (key == QLatin1String("LANGUAGE") &&
          !explicitLocaleOverrides.contains(key)) {
        environment.remove(key);
      }
    }
    if (explicitLocaleOverrides.contains(QStringLiteral("LANGUAGE"))) {
      environment.insert("LANGUAGE", explicitValue(QStringLiteral("LANGUAGE")));
    }
    for (auto it = explicitLocaleOverrides.cbegin();
         it != explicitLocaleOverrides.cend(); ++it) {
      if (it.key().startsWith(QLatin1String("LC_")) &&
          it.key() != QLatin1String("LC_ALL")) {
        environment.insert(it.key(), it.value().trimmed().isEmpty()
                                         ? QString{} : utf8Locale(it.value()));
      }
    }
    return;
  }

  // An explicitly chosen executable LANG or a selected game language should
  // become the child's fallback locale. Keep explicitly supplied LC_* values,
  // but strip inherited host categories (LC_ADDRESS, LC_IDENTIFICATION, etc.)
  // so they cannot make the game or pressure-vessel pick a different locale.
  QString baseLocale = explicitValue(QStringLiteral("LANG")).trimmed();
  if (baseLocale.isEmpty()) baseLocale = selectedGameLocale.trimmed();
  if (!baseLocale.isEmpty() && (hasExplicitLocale || !selectedGameLocale.isEmpty())) {
    baseLocale = utf8Locale(baseLocale);
    environment.remove("HOST_LC_ALL");
    environment.remove("LC_ALL");
    environment.insert("LANG", baseLocale);

    for (const QString& key : environment.keys()) {
      if (key.startsWith(QLatin1String("LC_")) &&
          key != QLatin1String("LC_ALL") &&
          !explicitLocaleOverrides.contains(key)) {
        environment.remove(key);
      }
      if (key == QLatin1String("LANGUAGE") &&
          !explicitLocaleOverrides.contains(key)) {
        environment.remove(key);
      }
    }
    for (auto it = explicitLocaleOverrides.cbegin();
         it != explicitLocaleOverrides.cend(); ++it) {
      if (it.key().startsWith(QLatin1String("LC_")) &&
          it.key() != QLatin1String("LC_ALL") && !it.value().trimmed().isEmpty()) {
        environment.insert(it.key(), utf8Locale(it.value()));
      }
    }
    if (explicitLocaleOverrides.contains(QStringLiteral("LANGUAGE"))) {
      environment.insert("LANGUAGE", explicitValue(QStringLiteral("LANGUAGE")));
    }
    return;
  }

  // With no game or executable preference, preserve the host's locale policy.
  // Proton restores HOST_LC_ALL, so normalize and mirror a supplied LC_ALL.
  QString all = environment.value("HOST_LC_ALL");
  if (all.isEmpty()) all = environment.value("LC_ALL");
  if (!all.isEmpty()) {
    all = utf8Locale(all);
    environment.insert("LC_ALL", all);
    environment.insert("HOST_LC_ALL", all);
  } else {
    environment.remove("LC_ALL");
    environment.remove("HOST_LC_ALL");
  }

  environment.insert("LANG", utf8Locale(environment.value("LANG")));
  if (!environment.value("LC_CTYPE").isEmpty()) {
    environment.insert("LC_CTYPE", utf8Locale(environment.value("LC_CTYPE")));
  } else {
    environment.remove("LC_CTYPE");
  }
  // LC_MESSAGES, LANGUAGE and other category overrides stay independent.
}

bool processEnvironmentMatchesLaunch(const QByteArray& processEnvironment,
                                     const QString& expectedWinePrefix,
                                     const QString& launchToken)
{
  const bool filterPrefix = !expectedWinePrefix.trimmed().isEmpty();
  const bool filterToken = !launchToken.trimmed().isEmpty();
  if (!filterPrefix && !filterToken) return false;

  QString processPrefix;
  QString processToken;
  for (const QByteArray& entry : processEnvironment.split('\0')) {
    if (entry.startsWith("WINEPREFIX=")) {
      processPrefix = QString::fromUtf8(entry.mid(11));
    } else if (entry.startsWith(QByteArray(kFluorineLaunchTokenEnvironment) + '=')) {
      processToken = QString::fromUtf8(
          entry.mid(static_cast<qsizetype>(sizeof(kFluorineLaunchTokenEnvironment))));
    }
  }

  const bool prefixMatches =
      filterPrefix && !processPrefix.isEmpty() &&
      canonicalOrCleanPath(processPrefix) == canonicalOrCleanPath(expectedWinePrefix);
  if (filterToken && !processToken.isEmpty()) {
    if (processToken != launchToken) return false;
    return !filterPrefix || processPrefix.isEmpty() || prefixMatches;
  }
  return prefixMatches;
}

std::optional<QMap<QString, QString>> parseExecutableEnvironment(
    const QString& text, QString* error)
{
  if (error) error->clear();
  static const QRegularExpression namePattern(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
  QMap<QString, QString> variables;
  const auto lines = text.split('\n');
  for (qsizetype i = 0; i < lines.size(); ++i) {
    QString line = lines[i];
    if (line.endsWith('\r')) line.chop(1);
    if (line.trimmed().isEmpty()) continue;
    const auto equals = line.indexOf('=');
    const QString name = line.left(equals).trimmed();
    if (equals < 0 || !namePattern.match(name).hasMatch() || line.contains(QChar::Null)) {
      if (error) {
        *error = QCoreApplication::translate("ExecutableEnvironment",
            "Line %1 must contain NAME=value, with a valid environment variable name.")
                     .arg(i + 1);
      }
      return std::nullopt;
    }
    variables.insert(name, line.mid(equals + 1));
  }
  return variables;
}

std::optional<LaunchWrapperOptions> parseLaunchWrapperOptions(
    const QString& text, QString* error)
{
  if (error) error->clear();
  if (text.contains(QChar::Null)) {
    if (error) *error = QCoreApplication::translate(
        "LaunchWrapperOptions", "Wrapper options cannot contain a null character.");
    return std::nullopt;
  }

  LaunchWrapperOptions options;
  for (const auto& token : QProcess::splitCommand(text.trimmed())) {
    if (token.compare("%command%", Qt::CaseInsensitive) == 0) continue;
    const auto equals = token.indexOf('=');
    const auto name = token.left(equals);
    const bool validName = !name.isEmpty() &&
        (name.front().isLetter() || name.front() == '_') &&
        std::all_of(name.begin(), name.end(), [](QChar c) {
          return c.isLetterOrNumber() || c == '_';
        });
    if (equals > 0 && validName)
      options.environment.insert(name, token.mid(equals + 1));
    else
      options.commands.append(token);
  }
  return options;
}

QString wrapperOptionsFromLegacyEnvironment(const QString& text)
{
  const auto environment = parseExecutableEnvironment(text);
  if (!environment) return text;
  QStringList options;
  for (auto it = environment->cbegin(); it != environment->cend(); ++it) {
    QString token = it.key() + '=' + it.value();
    // QProcess::splitCommand represents a literal double quote with three.
    token.replace('"', QStringLiteral("\"\"\""));
    options.append('"' + token + '"');
  }
  return options.join('\n');
}
