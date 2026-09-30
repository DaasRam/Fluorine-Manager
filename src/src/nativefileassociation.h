#pragma once

#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QMap>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>
#include <QVector>

#include <optional>

namespace nativefileassociation
{
struct Command
{
  QString executable;
  QString arguments;
  QString commandLine;
};

namespace detail
{
using DesktopValues = QMap<QString, QString>;

struct Token
{
  QString value;
  QVector<bool> quoted;
};

inline bool fail(QString* error, const QString& message)
{
  if (error) *error = message;
  return false;
}

inline std::optional<QString> decodeDesktopValue(const QString& raw,
                                                 QString* error)
{
  QString decoded;
  decoded.reserve(raw.size());
  for (qsizetype i = 0; i < raw.size(); ++i) {
    if (raw[i] != QLatin1Char('\\')) {
      decoded.append(raw[i]);
      continue;
    }
    if (++i >= raw.size()) {
      fail(error, QStringLiteral("Desktop entry value ends with an incomplete escape"));
      return std::nullopt;
    }
    switch (raw[i].unicode()) {
    case '\\': decoded.append(QLatin1Char('\\')); break;
    case 's': decoded.append(QLatin1Char(' ')); break;
    case 'n': decoded.append(QLatin1Char('\n')); break;
    case 't': decoded.append(QLatin1Char('\t')); break;
    case 'r': decoded.append(QLatin1Char('\r')); break;
    default:
      fail(error, QStringLiteral("Desktop entry contains an invalid string escape"));
      return std::nullopt;
    }
  }
  return decoded;
}

inline std::optional<DesktopValues> readDesktopEntry(const QString& path,
                                                     QString* error)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    fail(error, QStringLiteral("Could not read desktop entry"));
    return std::nullopt;
  }

  DesktopValues values;
  bool inDesktopEntry = false;
  bool foundDesktopEntry = false;
  while (!file.atEnd()) {
    QString line = QString::fromUtf8(file.readLine());
    while (line.endsWith(QLatin1Char('\n')) || line.endsWith(QLatin1Char('\r'))) {
      line.chop(1);
    }
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) continue;
    if (trimmed.startsWith(QLatin1Char('[')) && trimmed.endsWith(QLatin1Char(']'))) {
      inDesktopEntry = trimmed == QStringLiteral("[Desktop Entry]");
      foundDesktopEntry = foundDesktopEntry || inDesktopEntry;
      continue;
    }
    if (!inDesktopEntry) continue;
    const qsizetype equals = line.indexOf(QLatin1Char('='));
    if (equals <= 0) continue;
    const QString key = line.left(equals).trimmed();
    if (key.contains(QLatin1Char('[')) || key.contains(QLatin1Char(']'))) continue;
    auto value = decodeDesktopValue(line.mid(equals + 1), error);
    if (!value) return std::nullopt;
    values.insert(key, *value);
  }
  if (!foundDesktopEntry) {
    fail(error, QStringLiteral("Desktop file has no Desktop Entry group"));
    return std::nullopt;
  }
  return values;
}

inline bool splitDesktopExec(const QString& command, QVector<Token>& tokens,
                             QString* error)
{
  Token token;
  bool inQuotes = false;
  bool started = false;
  for (qsizetype i = 0; i < command.size(); ++i) {
    const QChar c = command[i];
    if (c == QLatin1Char('\\')) {
      if (i + 1 >= command.size()) {
        return fail(error, QStringLiteral("Exec ends with an incomplete escape"));
      }
      const QChar escaped = command[++i];
      if (escaped != QLatin1Char('\\') && escaped != QLatin1Char('"') &&
          escaped != QLatin1Char('`') && escaped != QLatin1Char('$')) {
        return fail(error, QStringLiteral("Exec contains an unsupported escape"));
      }
      token.value.append(escaped);
      token.quoted.append(inQuotes);
      started = true;
      continue;
    }
    if (c == QLatin1Char('"')) {
      inQuotes = !inQuotes;
      started = true;
      continue;
    }
    if (c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
      return fail(error, QStringLiteral("Exec contains a newline"));
    }
    if (c.isSpace() && !inQuotes) {
      if (started) {
        tokens.append(std::move(token));
        token = {};
        started = false;
      }
      continue;
    }
    token.value.append(c);
    token.quoted.append(inQuotes);
    started = true;
  }
  if (inQuotes) return fail(error, QStringLiteral("Exec has unmatched quotes"));
  if (started) tokens.append(std::move(token));
  if (tokens.isEmpty() || tokens.first().value.isEmpty()) {
    return fail(error, QStringLiteral("Exec has no executable"));
  }
  return true;
}

inline QString findProgram(const QString& name,
                           const QProcessEnvironment& environment)
{
  if (name.isEmpty() || name.contains(QLatin1Char('='))) return {};
  const QFileInfo info(name);
  if (info.isAbsolute()) return info.isExecutable() ? info.absoluteFilePath() : QString{};
  if (name.contains(QLatin1Char('/'))) return {};
  const QString path = environment.value(QStringLiteral("PATH"));
  const QStringList searchPaths = path.split(QLatin1Char(':'), Qt::KeepEmptyParts);
  return QStandardPaths::findExecutable(name, searchPaths);
}

inline bool serializeArguments(const QStringList& arguments, QString& serialized,
                               QString* error)
{
  QStringList tokens;
  tokens.reserve(arguments.size());
  for (QString argument : arguments) {
    if (argument.isEmpty()) {
      return fail(error, QStringLiteral("Qt's launch argument parser cannot preserve an empty argument"));
    }
    if (argument.contains(QChar::Null)) {
      return fail(error, QStringLiteral("An argument contains a NUL character"));
    }
    argument.replace(QStringLiteral("\""), QStringLiteral("\"\"\""));
    tokens.append(QLatin1Char('"') + argument + QLatin1Char('"'));
  }
  serialized = tokens.join(QLatin1Char(' '));
  if (QProcess::splitCommand(serialized) != arguments) {
    return fail(error, QStringLiteral("Arguments cannot be represented by the launch parser"));
  }
  return true;
}
}  // namespace detail

// Parse a trusted XDG desktop entry and expand its Exec field for one local
// file. The result remains an argv-style command; no shell is involved.
inline std::optional<Command> fromDesktopFile(
    const QString& desktopFilePath, const QFileInfo& target,
    const QProcessEnvironment& hostEnvironment, QString* error = nullptr)
{
  if (error) error->clear();
  const auto desktop = detail::readDesktopEntry(desktopFilePath, error);
  if (!desktop) return std::nullopt;
  if (desktop->value(QStringLiteral("Type")) != QStringLiteral("Application") ||
      desktop->value(QStringLiteral("Hidden")).compare(QStringLiteral("true"),
                                                        Qt::CaseInsensitive) == 0) {
    detail::fail(error, QStringLiteral("Desktop entry is hidden or is not an application"));
    return std::nullopt;
  }
  if (desktop->value(QStringLiteral("Terminal")).compare(QStringLiteral("true"),
                                                          Qt::CaseInsensitive) == 0) {
    detail::fail(error, QStringLiteral("Terminal desktop entries are not supported for hooked launches"));
    return std::nullopt;
  }

  const QString exec = desktop->value(QStringLiteral("Exec")).trimmed();
  QVector<detail::Token> tokens;
  if (!detail::splitDesktopExec(exec, tokens, error)) return std::nullopt;

  const QString tryExec = desktop->value(QStringLiteral("TryExec")).trimmed();
  if (!tryExec.isEmpty() && detail::findProgram(tryExec, hostEnvironment).isEmpty()) {
    detail::fail(error, QStringLiteral("Desktop entry TryExec is unavailable"));
    return std::nullopt;
  }

  const QString desktopName = desktop->value(QStringLiteral("Name"));
  const QString icon = desktop->value(QStringLiteral("Icon"));
  const QString desktopUri = QUrl::fromLocalFile(QFileInfo(desktopFilePath).absoluteFilePath())
                                .toString(QUrl::FullyEncoded);
  const QString filePath = target.absoluteFilePath();
  const QString fileUri = QUrl::fromLocalFile(filePath).toString(QUrl::FullyEncoded);
  QStringList expanded;
  bool hasFileCode = false;

  for (qsizetype tokenIndex = 0; tokenIndex < tokens.size(); ++tokenIndex) {
    const detail::Token& token = tokens[tokenIndex];
    if (tokenIndex == 0) {
      expanded.append(token.value);
      continue;
    }

    if (token.value == QStringLiteral("%i")) {
      if (token.quoted.value(0)) {
        detail::fail(error, QStringLiteral("Icon field code cannot be quoted"));
        return std::nullopt;
      }
      if (!icon.isEmpty()) expanded << QStringLiteral("--icon") << icon;
      continue;
    }

    QString value;
    for (qsizetype i = 0; i < token.value.size(); ++i) {
      const QChar c = token.value[i];
      if (c != QLatin1Char('%')) {
        value.append(c);
        continue;
      }
      if (i + 1 >= token.value.size()) {
        detail::fail(error, QStringLiteral("Exec contains a bare percent sign"));
        return std::nullopt;
      }
      const QChar code = token.value[++i];
      if (code == QLatin1Char('%')) {
        value.append(QLatin1Char('%'));
        continue;
      }
      if (token.quoted.value(i - 1) || token.quoted.value(i)) {
        detail::fail(error, QStringLiteral("Exec field codes inside quotes are unsupported"));
        return std::nullopt;
      }

      if (code == QLatin1Char('f') || code == QLatin1Char('u') ||
          code == QLatin1Char('F') || code == QLatin1Char('U')) {
        if (hasFileCode) {
          detail::fail(error, QStringLiteral("Exec contains more than one file field code"));
          return std::nullopt;
        }
        hasFileCode = true;
        if ((code == QLatin1Char('F') || code == QLatin1Char('U')) &&
            token.value != QStringLiteral("%") + code) {
          detail::fail(error, QStringLiteral("List field codes must occupy a whole argument"));
          return std::nullopt;
        }
        const QString fileValue = (code == QLatin1Char('u') || code == QLatin1Char('U'))
                                      ? fileUri : filePath;
        if (code == QLatin1Char('F') || code == QLatin1Char('U')) {
          expanded.append(fileValue);
        } else {
          value.append(fileValue);
        }
        continue;
      }
      if (code == QLatin1Char('c')) {
        value.append(desktopName);
      } else if (code == QLatin1Char('k')) {
        value.append(desktopUri);
      } else if (code == QLatin1Char('i')) {
        detail::fail(error, QStringLiteral("Icon field code must occupy a whole argument"));
        return std::nullopt;
      } else if (code == QLatin1Char('d') || code == QLatin1Char('D') ||
                 code == QLatin1Char('n') || code == QLatin1Char('N') ||
                 code == QLatin1Char('v') || code == QLatin1Char('m')) {
        // Deprecated desktop-entry codes are specified to be ignored.
      } else {
        detail::fail(error, QStringLiteral("Exec contains an unknown field code"));
        return std::nullopt;
      }
    }
    if (!value.isEmpty()) expanded.append(value);
  }

  if (!hasFileCode) expanded.append(filePath);
  if (expanded.isEmpty()) {
    detail::fail(error, QStringLiteral("Exec expands to no command"));
    return std::nullopt;
  }

  const QString executable = detail::findProgram(expanded.takeFirst(), hostEnvironment);
  if (executable.isEmpty()) {
    detail::fail(error, QStringLiteral("Desktop entry executable is unavailable"));
    return std::nullopt;
  }

  QString arguments;
  if (!detail::serializeArguments(expanded, arguments, error)) return std::nullopt;
  QStringList wholeCommand{executable};
  wholeCommand.append(expanded);
  QString commandLine;
  if (!detail::serializeArguments(wholeCommand, commandLine, error)) return std::nullopt;
  return Command{executable, arguments, commandLine};
}
}  // namespace nativefileassociation
