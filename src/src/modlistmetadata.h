#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace ModlistMetadata
{
struct Receipt
{
  QString sourceUrl;
  QString title;
  QString author;
  QString version;
  QDateTime sourceUpdatedUtc;
  QDateTime installedAtUtc;
};

// Reads the upstream update field used by Wabbajack and Collections.
std::optional<QDateTime> sourceUpdated(const QJsonObject& metadata);
std::optional<QDateTime> parseIsoDateTime(const QString& value);

QString localDateLabel(const QDateTime& dateTime);
QString preciseLocalTooltip(const QDateTime& dateTime);

QString receiptPath(const QString& setupPath);
std::optional<Receipt> readReceipt(const QString& setupPath, QString* error = nullptr);
bool writeReceipt(const QString& setupPath, const Receipt& receipt, QString* error = nullptr);
} // namespace ModlistMetadata
