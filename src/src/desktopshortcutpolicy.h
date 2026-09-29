#ifndef DESKTOP_SHORTCUT_POLICY_H
#define DESKTOP_SHORTCUT_POLICY_H

#include <QCryptographicHash>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace env::desktopshortcut
{

inline QString sanitizeFilenamePart(const QString& value)
{
  QString result;
  result.reserve(value.size());
  for (const QChar c : value) {
    if (c.isLetterOrNumber() || c == '-' || c == '_' || c == '.') {
      result += c;
    } else if (c.isSpace()) {
      result += '-';
    }
  }
  return result;
}

inline QString ownerId(const QString& identity)
{
  QByteArray input = QByteArrayLiteral("fluorine-shortcut-v1");
  input.append('\0');
  input.append(identity.toUtf8());
  return QString::fromLatin1(
      QCryptographicHash::hash(input, QCryptographicHash::Sha256).toHex().left(24));
}

inline QString filenameStem(const QString& instanceName, const QString& name,
                            const QString& identity)
{
  QString readable = name;
  if (!instanceName.isEmpty()) {
    readable = instanceName + (readable.isEmpty() ? QString() : QStringLiteral("-")) +
               readable;
  }
  readable = sanitizeFilenamePart(readable).left(72);
  if (readable.isEmpty()) {
    readable = QStringLiteral("fluorine");
  }
  return readable + QLatin1Char('-') + ownerId(identity);
}

inline QString shellQuote(const QString& value)
{
  QString quoted = value;
  quoted.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"));
  return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

// Interpret the configured arguments as a command-line string, then quote
// every token for the generated shell script. Shell expansions in the input
// are consequently passed to the target literally.
inline QString shellCommand(const QString& target, const QString& arguments)
{
  QString command = shellQuote(target);
  for (const QString& argument : QProcess::splitCommand(arguments)) {
    command += QLatin1Char(' ') + shellQuote(argument);
  }
  return command;
}

inline QString commandLineToken(const QString& value)
{
  QString quoted = value;
  quoted.replace(QLatin1Char('"'), QStringLiteral("\"\"\""));
  return QLatin1Char('"') + quoted + QLatin1Char('"');
}

inline QString desktopEntryValue(const QString& value)
{
  QString escaped;
  escaped.reserve(value.size());
  for (const QChar c : value) {
    switch (c.unicode()) {
    case '\\': escaped += QStringLiteral("\\\\"); break;
    case '\n': escaped += QStringLiteral("\\n"); break;
    case '\r': escaped += QStringLiteral("\\r"); break;
    case '\t': escaped += QStringLiteral("\\t"); break;
    default: escaped += c; break;
    }
  }
  return escaped;
}

// Exec has its own quoting rules in addition to the generic Desktop Entry
// value escapes. Reject line breaks/control characters, which cannot safely
// be represented as a single command argument.
inline QString desktopExecArgument(const QString& value)
{
  QString escaped;
  escaped.reserve(value.size());
  for (const QChar c : value) {
    if (c.isNull() || c == QLatin1Char('\n') || c == QLatin1Char('\r') ||
        c.category() == QChar::Other_Control) {
      return {};
    }
    if (c == QLatin1Char('\\') || c == QLatin1Char('"') ||
        c == QLatin1Char('$') || c == QLatin1Char('`')) {
      escaped += QLatin1Char('\\');
    }
    if (c == QLatin1Char('%')) {
      escaped += QLatin1Char('%');
    }
    escaped += c;
  }
  return QLatin1Char('"') + escaped + QLatin1Char('"');
}

inline QString desktopExecEntryValue(const QString& value)
{
  const QString argument = desktopExecArgument(value);
  return argument.isEmpty() ? QString() : desktopEntryValue(argument);
}

inline QString desktopExecCommandLine(const QStringList& arguments)
{
  if (arguments.isEmpty()) {
    return {};
  }

  QStringList encoded;
  encoded.reserve(arguments.size());
  for (const QString& argument : arguments) {
    const QString escaped = desktopExecEntryValue(argument);
    if (escaped.isEmpty()) {
      return {};
    }
    encoded.append(escaped);
  }
  return encoded.join(QLatin1Char(' '));
}

inline bool desktopFileHasOwner(const QByteArray& contents, const QString& id)
{
  const QByteArray marker =
      QByteArrayLiteral("X-Fluorine-Shortcut-Id=") + id.toLatin1();
  bool inDesktopEntry = false;
  const QList<QByteArray> lines = contents.split('\n');
  for (const QByteArray& line : lines) {
    if (line.startsWith('[') && line.endsWith(']')) {
      inDesktopEntry = line == QByteArrayLiteral("[Desktop Entry]");
    } else if (inDesktopEntry && line == marker) {
      return true;
    }
  }
  return false;
}

inline bool scriptFileHasOwner(const QByteArray& contents, const QString& id)
{
  const QByteArray marker =
      QByteArrayLiteral("# fluorine-shortcut-owner=") + id.toLatin1();
  const QList<QByteArray> lines = contents.split('\n');
  for (int i = 0; i < lines.size() && i < 8; ++i) {
    if (lines[i] == marker) {
      return true;
    }
  }
  return false;
}

}  // namespace env::desktopshortcut

#endif  // DESKTOP_SHORTCUT_POLICY_H
