#pragma once

#include <QFile>
#include <QRegularExpression>

namespace DownloadResponse
{
struct Result
{
  bool accepted = false;
  qint64 offset = 0;
  qint64 bodySize = -1;
  qint64 totalSize = -1;
  QString error;
};

// Validate a response before consuming its body. A ranged response must
// describe the exact local prefix; if the server ignored Range, replace the
// partial file instead of appending a second copy of the full archive.
inline Result prepare(QFile& output, qint64 requestedOffset, int status,
                      const QByteArray& contentRange, qint64 contentLength,
                      const QByteArray& contentEncoding = {},
                      const QString& urlScheme = {})
{
  Result result;
  const bool encoded = !contentEncoding.isEmpty() && contentEncoding != "identity";
  const bool httpResponse =
      urlScheme.compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0 ||
      urlScheme.compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0;
  const bool allowStatusless = !urlScheme.isEmpty() && !httpResponse;
  if (status == 206) {
    static const QRegularExpression range(
        QStringLiteral("^bytes ([0-9]+)-([0-9]+)/([0-9]+)$"));
    const auto match = range.match(QString::fromLatin1(contentRange.trimmed()));
    bool startOk = false;
    bool endOk = false;
    bool totalOk = false;
    const qint64 start = match.captured(1).toLongLong(&startOk);
    const qint64 end = match.captured(2).toLongLong(&endOk);
    const qint64 total = match.captured(3).toLongLong(&totalOk);
    if (!match.hasMatch() || !startOk || !endOk || !totalOk || encoded ||
        start != requestedOffset || output.size() != requestedOffset ||
        end < start || total <= end || end != total - 1 ||
        (contentLength >= 0 && contentLength != end - start + 1)) {
      result.error = QStringLiteral("Server returned an inconsistent download range");
      return result;
    }
    result.offset = start;
    result.bodySize = end - start + 1;
    result.totalSize = total;
  } else if (status == 200 || (status == 0 && allowStatusless)) {
    if (requestedOffset > 0 && (!output.resize(0) || !output.seek(0))) {
      result.error = output.errorString();
      return result;
    }
    result.bodySize = encoded ? -1 : contentLength;
    result.totalSize = result.bodySize;
  } else {
    result.error = QStringLiteral("Download server returned HTTP %1").arg(status);
    return result;
  }
  result.accepted = true;
  return result;
}
}  // namespace DownloadResponse
