#include "modlistmetadata.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrl>

namespace ModlistMetadata
{
namespace
{
constexpr int ReceiptSchemaVersion = 1;
constexpr qint64 MaxReceiptBytes = 128 * 1024;
constexpr qint64 FutureSkewMilliseconds = 24LL * 60 * 60 * 1000;

void setError(QString* error, const QString& message)
{
  if (error) *error = message;
}

std::optional<QDateTime> acceptedDateTime(QDateTime value)
{
  if (!value.isValid()) return std::nullopt;
  value = value.toUTC();
  if (value.toMSecsSinceEpoch() <= 0 ||
      value > QDateTime::currentDateTimeUtc().addMSecs(FutureSkewMilliseconds)) {
    return std::nullopt;
  }
  return value;
}

QString isoUtc(const QDateTime& value)
{
  return value.toUTC().toString(Qt::ISODateWithMs);
}

std::optional<QDateTime> parseStoredDate(const QJsonObject& object, const QString& key)
{
  const auto value = object.value(key);
  if (!value.isString()) return std::nullopt;
  return parseIsoDateTime(value.toString());
}

QString sanitizedSourceUrl(const QString& value)
{
  const QString trimmed = value.trimmed();
  const QUrl url(trimmed);
  const QString scheme = url.scheme().toLower();
  if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))
    return trimmed;
  if (!url.isValid()) return {};
  QUrl safe = url;
  safe.setUserInfo(QString{});
  safe.setQuery(QString{});
  safe.setFragment(QString{});
  return safe.toString(QUrl::FullyEncoded);
}
} // namespace

std::optional<QDateTime> parseIsoDateTime(const QString& raw)
{
  const QString value = raw.trimmed();
  if (value.isEmpty()) return std::nullopt;

  static const QRegularExpression timestamp(
      R"(^([0-9]{4}-[0-9]{2}-[0-9]{2})[Tt]([0-9]{2}):([0-9]{2})(?::([0-9]{2})(?:\.([0-9]+))?)?(Z|z|[+-][0-9]{2}:?[0-9]{2})$)");

  const auto match = timestamp.match(value);
  if (!match.hasMatch()) return std::nullopt;

  QString seconds = match.captured(4);
  if (seconds.isEmpty()) seconds = QStringLiteral("00");
  QString milliseconds = match.captured(5).left(3).leftJustified(3, QLatin1Char('0'));

  QString zone = match.captured(6).toUpper();
  if (zone != QStringLiteral("Z")) {
    QString digits = zone.mid(1);
    digits.remove(QLatin1Char(':'));
    bool hoursOk = false;
    bool minutesOk = false;
    const int hours = digits.left(2).toInt(&hoursOk);
    const int minutes = digits.mid(2, 2).toInt(&minutesOk);
    if (!hoursOk || !minutesOk || minutes > 59 || hours > 14 ||
        (hours == 14 && minutes != 0)) {
      return std::nullopt;
    }
    zone = zone.left(1) + digits.left(2) + QLatin1Char(':') + digits.mid(2, 2);
  }

  const QString normalized = QStringLiteral("%1T%2:%3:%4.%5%6")
                                 .arg(match.captured(1), match.captured(2), match.captured(3),
                                      seconds, milliseconds, zone);

  const auto parsed = QDateTime::fromString(normalized, Qt::ISODateWithMs);
  return acceptedDateTime(parsed);
}

std::optional<QDateTime> sourceUpdated(const QJsonObject& metadata)
{
  // dateUpdated is authoritative in Wabbajack's gallery response. The other
  // spellings cover older and Collections-shaped metadata objects.
  for (const auto* key : { "dateUpdated", "date_updated", "updatedAt" }) {
    if (!metadata.contains(QLatin1String(key))) continue;
    const auto value = metadata.value(QLatin1String(key));
    if (!value.isString()) return std::nullopt;
    return parseIsoDateTime(value.toString());
  }
  return std::nullopt;
}

QString localDateLabel(const QDateTime& dateTime)
{
  return dateTime.isValid()
             ? QLocale().toString(dateTime.toLocalTime().date(), QLocale::ShortFormat)
             : QString{};
}

QString preciseLocalTooltip(const QDateTime& dateTime)
{
  if (!dateTime.isValid()) return {};
  const auto local = dateTime.toLocalTime();
  const int offset = local.offsetFromUtc();
  const int absoluteOffset = qAbs(offset);
  const QString zone = QStringLiteral("UTC%1%2:%3")
                           .arg(offset < 0 ? QLatin1Char('-') : QLatin1Char('+'))
                           .arg(absoluteOffset / 3600, 2, 10, QLatin1Char('0'))
                           .arg((absoluteOffset % 3600) / 60, 2, 10, QLatin1Char('0'));
  return local.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")) + QLatin1Char(' ') + zone;
}

QString receiptPath(const QString& setupPath)
{
  return setupPath.isEmpty() ? QString{} : QDir(setupPath).filePath(QStringLiteral("fluorine-modlist.json"));
}

std::optional<Receipt> readReceipt(const QString& setupPath, QString* error)
{
  if (setupPath.isEmpty()) {
    setError(error, QStringLiteral("The modlist setup path is empty."));
    return std::nullopt;
  }

  const QString path = receiptPath(setupPath);
  QFile file(path);
  if (QFileInfo(path).isSymLink() || !file.open(QIODevice::ReadOnly)) {
    setError(error, QStringLiteral("The modlist receipt could not be opened."));
    return std::nullopt;
  }
  if (file.size() < 0 || file.size() > MaxReceiptBytes) {
    setError(error, QStringLiteral("The modlist receipt is too large."));
    return std::nullopt;
  }

  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    setError(error, QStringLiteral("The modlist receipt is invalid."));
    return std::nullopt;
  }
  const auto object = document.object();
  if (object.value(QStringLiteral("schemaVersion")).toInt(-1) != ReceiptSchemaVersion ||
      !object.value(QStringLiteral("sourceUrl")).isString() ||
      object.value(QStringLiteral("sourceUrl")).toString().trimmed().isEmpty()) {
    setError(error, QStringLiteral("The modlist receipt has an unsupported format."));
    return std::nullopt;
  }

  const auto installedAt = parseStoredDate(object, QStringLiteral("installedAt"));
  if (!installedAt) {
    setError(error, QStringLiteral("The modlist receipt has no valid installation date."));
    return std::nullopt;
  }

  Receipt receipt;
  receipt.sourceUrl = sanitizedSourceUrl(object.value(QStringLiteral("sourceUrl")).toString());
  if (receipt.sourceUrl.isEmpty()) {
    setError(error, QStringLiteral("The modlist receipt has no valid source URL."));
    return std::nullopt;
  }
  receipt.title = object.value(QStringLiteral("title")).toString();
  receipt.author = object.value(QStringLiteral("author")).toString();
  receipt.version = object.value(QStringLiteral("version")).toString();
  receipt.sourceUpdatedUtc = parseStoredDate(object, QStringLiteral("sourceUpdated")).value_or(QDateTime{});
  receipt.installedAtUtc = *installedAt;
  setError(error, {});
  return receipt;
}

bool writeReceipt(const QString& setupPath, const Receipt& receipt, QString* error)
{
  const QString sourceUrl = sanitizedSourceUrl(receipt.sourceUrl);
  if (setupPath.isEmpty() || sourceUrl.isEmpty()) {
    setError(error, QStringLiteral("The modlist receipt is missing its setup path or source URL."));
    return false;
  }
  const auto installedAt = acceptedDateTime(receipt.installedAtUtc);
  if (!installedAt) {
    setError(error, QStringLiteral("The modlist receipt needs a valid installation date."));
    return false;
  }

  QString sourceUpdated;
  if (receipt.sourceUpdatedUtc.isValid()) {
    const auto updated = acceptedDateTime(receipt.sourceUpdatedUtc);
    if (!updated) {
      setError(error, QStringLiteral("The modlist source update date is invalid."));
      return false;
    }
    sourceUpdated = isoUtc(*updated);
  }

  if (!QDir().mkpath(setupPath)) {
    setError(error, QStringLiteral("The modlist setup folder could not be created."));
    return false;
  }

  const QJsonObject object{
    { QStringLiteral("schemaVersion"), ReceiptSchemaVersion },
    { QStringLiteral("sourceUrl"), sourceUrl },
    { QStringLiteral("title"), receipt.title },
    { QStringLiteral("author"), receipt.author },
    { QStringLiteral("version"), receipt.version },
    { QStringLiteral("sourceUpdated"), sourceUpdated },
    { QStringLiteral("installedAt"), isoUtc(*installedAt) },
  };
  const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
  QSaveFile file(receiptPath(setupPath));
  if (!file.open(QIODevice::WriteOnly)) {
    setError(error, QStringLiteral("The modlist receipt could not be opened: %1")
                        .arg(file.errorString()));
    return false;
  }
  if (file.write(bytes) != bytes.size()) {
    setError(error, QStringLiteral("The modlist receipt could not be written: %1")
                        .arg(file.errorString()));
    file.cancelWriting();
    return false;
  }
  if (!file.commit()) {
    setError(error, QStringLiteral("The modlist receipt could not be saved: %1")
                        .arg(file.errorString()));
    return false;
  }
  setError(error, {});
  return true;
}
} // namespace ModlistMetadata
