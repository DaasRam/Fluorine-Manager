#include "installqueuemodel.h"

#include <QCoreApplication>
#include <QStringList>
#include <QVariant>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace
{
qint64 nonNegative(qint64 value)
{
  return std::max<qint64>(0, value);
}

qint64 saturatedAdd(qint64 left, qint64 right)
{
  if (right > 0 && left > std::numeric_limits<qint64>::max() - right)
    return std::numeric_limits<qint64>::max();
  return left + right;
}

QString displayUnit(const QString& unit)
{
  if (unit.compare(QStringLiteral("directives"), Qt::CaseInsensitive) == 0)
    return QStringLiteral("operations");
  return unit;
}

QString formatBytes(qint64 bytes)
{
  static const QStringList suffixes = {QStringLiteral("B"), QStringLiteral("KiB"),
                                       QStringLiteral("MiB"), QStringLiteral("GiB"),
                                       QStringLiteral("TiB"), QStringLiteral("PiB")};
  double value = static_cast<double>(nonNegative(bytes));
  int suffix = 0;
  while (value >= 1024.0 && suffix + 1 < suffixes.size()) {
    value /= 1024.0;
    ++suffix;
  }
  const int precision = suffix == 0 || value >= 10.0 ? 0 : 1;
  return QStringLiteral("%1 %2").arg(QString::number(value, 'f', precision),
                                       suffixes.at(suffix));
}

QString formatProgress(qint64 completed, qint64 total, const QString& unit)
{
  completed = nonNegative(completed);
  total = nonNegative(total);
  const QString readableUnit = displayUnit(unit);
  if (unit.compare(QStringLiteral("bytes"), Qt::CaseInsensitive) == 0) {
    if (total > 0)
      return QStringLiteral("%1 / %2")
          .arg(formatBytes(completed), formatBytes(total));
    return formatBytes(completed);
  }
  if (total > 0) {
    const QString counts = QStringLiteral("%1 / %2").arg(completed).arg(total);
    return readableUnit.isEmpty() ? counts : counts + QLatin1Char(' ') + readableUnit;
  }
  const QString count = QString::number(completed);
  return readableUnit.isEmpty() ? count : count + QLatin1Char(' ') + readableUnit;
}

QString formatSpeed(double speed)
{
  if (!std::isfinite(speed) || speed <= 0.0) return {};
  static const QStringList suffixes = {QStringLiteral("B/s"), QStringLiteral("KiB/s"),
                                       QStringLiteral("MiB/s"), QStringLiteral("GiB/s"),
                                       QStringLiteral("TiB/s")};
  int suffix = 0;
  while (speed >= 1024.0 && suffix + 1 < suffixes.size()) {
    speed /= 1024.0;
    ++suffix;
  }
  const int precision = suffix == 0 || speed >= 10.0 ? 0 : 1;
  return QStringLiteral("%1 %2").arg(QString::number(speed, 'f', precision),
                                       suffixes.at(suffix));
}

bool isDownloadStage(const QString& stage)
{
  return stage.contains(QStringLiteral("download"), Qt::CaseInsensitive);
}
}

InstallQueueModel::InstallQueueModel(QObject* parent) : QAbstractTableModel(parent) {}

int InstallQueueModel::rowCount(const QModelIndex& parent) const
{
  return parent.isValid() ? 0 : m_Items.size();
}

int InstallQueueModel::columnCount(const QModelIndex& parent) const
{
  return parent.isValid() ? 0 : ColumnCount;
}

QVariant InstallQueueModel::data(const QModelIndex& index, int role) const
{
  if (!index.isValid() || index.row() < 0 || index.row() >= m_Items.size() ||
      index.column() < 0 || index.column() >= ColumnCount)
    return {};

  const auto& item = m_Items.at(index.row());
  const auto column = static_cast<Column>(index.column());

  if (role == IdRole) return item.id;
  if (role == RequestIdRole) return item.requestId;
  if (role == StatusRole) return static_cast<int>(item.status);
  if (role == CompletedRole) return item.completed;
  if (role == TotalRole) return item.total;
  if (role == UnitRole) return item.unit;
  if (role == SpeedRole)
    return item.status == Status::Active && isDownloadStage(item.stage) ? item.speed : 0.0;
  if (role == SubtitleRole) return item.subtitle;
  if (role == MessageRole) return item.message;
  if (role == IndeterminateRole)
    return item.status == Status::Active && item.total <= 0;

  if (role == Qt::ToolTipRole || role == Qt::AccessibleDescriptionRole) {
    QStringList detail;
    if (!item.name.isEmpty()) detail.push_back(item.name);
    if (!item.displayName.isEmpty() && item.displayName != item.name)
      detail.push_back(QStringLiteral("Display name: %1").arg(item.displayName));
    if (!item.subtitle.isEmpty()) detail.push_back(item.subtitle);
    if (!item.stage.isEmpty()) detail.push_back(item.stage);
    if (!item.message.isEmpty()) detail.push_back(item.message);
    if (!item.unit.isEmpty()) detail.push_back(formatProgress(item.completed, item.total, item.unit));
    return detail.join(QLatin1Char('\n'));
  }

  if (role == Qt::TextAlignmentRole) {
    if (column == ProgressColumn || column == SpeedColumn)
      return static_cast<int>(Qt::AlignCenter);
    return static_cast<int>(Qt::AlignVCenter | Qt::AlignLeft);
  }

  if (role != Qt::DisplayRole) return {};

  switch (column) {
  case NameColumn:
    return item.displayName.isEmpty() ? item.name : item.displayName;
  case StageColumn:
    if (item.status == Status::Active)
      return item.stage.isEmpty() ? QStringLiteral("Working") : item.stage;
    return stageForStatus(item.status);
  case ProgressColumn:
    switch (item.status) {
    case Status::Active:
      return formatProgress(item.completed, item.total, item.unit);
    case Status::Failed:
    case Status::Stopped:
    case Status::Waiting:
      return item.message;
    case Status::Completed:
      return item.total > 0 ? formatProgress(item.total, item.total, item.unit)
                            : QStringLiteral("Complete");
    }
    break;
  case SpeedColumn:
    return item.status == Status::Active && isDownloadStage(item.stage)
               ? formatSpeed(item.speed)
               : QString();
  case ColumnCount:
    break;
  }
  return {};
}

QVariant InstallQueueModel::headerData(int section, Qt::Orientation orientation,
                                       int role) const
{
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
  switch (section) {
  case NameColumn: return tr("Name");
  case StageColumn: return tr("Stage / status");
  case ProgressColumn: return tr("Progress / details");
  case SpeedColumn: return tr("Speed");
  default: return {};
  }
}

QHash<int, QByteArray> InstallQueueModel::roleNames() const
{
  auto roles = QAbstractTableModel::roleNames();
  roles.insert(IdRole, "id");
  roles.insert(RequestIdRole, "requestId");
  roles.insert(StatusRole, "status");
  roles.insert(CompletedRole, "completed");
  roles.insert(TotalRole, "total");
  roles.insert(UnitRole, "unit");
  roles.insert(SpeedRole, "speed");
  roles.insert(SubtitleRole, "subtitle");
  roles.insert(MessageRole, "message");
  roles.insert(IndeterminateRole, "indeterminate");
  return roles;
}

InstallQueueModel::Counts InstallQueueModel::counts() const
{
  Counts result;
  result.active = m_ActiveCount;
  result.failed = m_FailedCount;
  result.stopped = m_StoppedCount;
  result.waitingRequests = m_WaitingCount;
  result.completedInHistory = m_CompletedHistoryCount;
  result.needsAttention = m_FailedCount + m_StoppedCount + m_WaitingCount;
  result.completedTotal = m_CompletedTotal;
  result.completedBytes = m_CompletedBytes;
  result.reusedCount = m_ReusedCount;
  result.reusedBytes = m_ReusedBytes;
  return result;
}

int InstallQueueModel::rowForId(const QString& id) const
{
  return m_RowById.value(id, -1);
}

QString InstallQueueModel::idAt(int row) const
{
  return row >= 0 && row < m_Items.size() ? m_Items.at(row).id : QString();
}

InstallQueueModel::Status InstallQueueModel::statusAt(int row) const
{
  return row >= 0 && row < m_Items.size() ? m_Items.at(row).status : Status::Failed;
}

void InstallQueueModel::resetQueue()
{
  beginResetModel();
  m_Items.clear();
  m_RowById.clear();
  m_RequestToItem.clear();
  m_CompletedTotal = 0;
  m_CompletedBytes = 0;
  m_ReusedCount = 0;
  m_ReusedBytes = 0;
  m_CompletionSequence = 0;
  m_ActiveCount = 0;
  m_FailedCount = 0;
  m_StoppedCount = 0;
  m_WaitingCount = 0;
  m_CompletedHistoryCount = 0;
  endResetModel();
}

void InstallQueueModel::setReused(qint64 count, qint64 bytes)
{
  m_ReusedCount = nonNegative(count);
  m_ReusedBytes = nonNegative(bytes);
}

void InstallQueueModel::startItem(const QString& id, const QString& name,
                                  const QString& displayName, const QString& subtitle,
                                  const QString& stage, qint64 total, const QString& unit)
{
  if (id.isEmpty()) return;
  Item item;
  const int oldRow = rowForId(id);
  if (oldRow >= 0) item = m_Items.at(oldRow);
  item.id = id;
  item.requestId.clear();
  item.name = name;
  item.displayName = displayName;
  item.subtitle = subtitle;
  item.stage = stage;
  item.total = nonNegative(total);
  if (!unit.isEmpty()) item.unit = unit;
  item.message.clear();
  item.speed = 0.0;
  item.status = Status::Active;
  insertOrReplace(std::move(item));
}

void InstallQueueModel::progressItem(const QString& id, qint64 completed, qint64 total,
                                     double speed, const QString& unit)
{
  const int row = rowForId(id);
  if (row < 0 || m_Items.at(row).status != Status::Active) return;
  auto& item = m_Items[row];
  item.completed = nonNegative(completed);
  item.total = nonNegative(total);
  if (!unit.isEmpty()) item.unit = unit;
  item.speed = std::isfinite(speed) && speed > 0.0 ? speed : 0.0;
  updateItem(row);
}

void InstallQueueModel::messageItem(const QString& id, const QString& message)
{
  const int row = rowForId(id);
  if (row < 0 || m_Items.at(row).status == Status::Waiting) return;
  m_Items[row].message = message;
  updateItem(row);
}

void InstallQueueModel::completeItem(const QString& id)
{
  const int row = rowForId(id);
  if (row < 0) return;
  auto& item = m_Items[row];
  if (item.status == Status::Completed || item.status == Status::Waiting) return;

  m_CompletedTotal = saturatedAdd(m_CompletedTotal, 1);
  if (item.unit.compare(QStringLiteral("bytes"), Qt::CaseInsensitive) == 0)
    m_CompletedBytes = saturatedAdd(m_CompletedBytes, std::max(item.total, item.completed));
  changeStatus(item, Status::Completed);
  item.message.clear();
  item.speed = 0.0;
  item.completionSequence = ++m_CompletionSequence;
  updateItem(row);
  pruneCompletedHistory();
}

void InstallQueueModel::failItem(const QString& id, const QString& message)
{
  const int row = rowForId(id);
  if (row < 0) return;
  auto& item = m_Items[row];
  if (item.status == Status::Completed || item.status == Status::Waiting) return;
  changeStatus(item, Status::Failed);
  item.message = message;
  item.speed = 0.0;
  updateItem(row);
}

void InstallQueueModel::stopActive(const QString& reason)
{
  for (int row = 0; row < m_Items.size(); ++row) {
    auto& item = m_Items[row];
    if (item.status != Status::Active) continue;
    changeStatus(item, Status::Stopped);
    item.message = reason;
    item.speed = 0.0;
    updateItem(row);
  }
}

void InstallQueueModel::setWaitingRequest(const QString& requestId, const QString& name,
                                          const QString& detail)
{
  if (requestId.isEmpty()) return;
  QString key = m_RequestToItem.value(requestId);
  if (key.isEmpty()) {
    key = QString(QChar(0x001f)) + QStringLiteral("request:") + requestId;
    while (m_RowById.contains(key)) key.prepend(QChar(0x001f));
    m_RequestToItem.insert(requestId, key);
  }

  Item item;
  const int oldRow = rowForId(key);
  if (oldRow >= 0) item = m_Items.at(oldRow);
  item.id = key;
  item.requestId = requestId;
  item.name = name;
  item.displayName = name;
  item.subtitle = detail;
  item.stage = tr("Waiting for input");
  item.message = detail;
  item.status = Status::Waiting;
  item.speed = 0.0;
  item.completed = 0;
  item.total = 0;
  item.unit.clear();
  item.completionSequence = 0;
  insertOrReplace(std::move(item));
}

void InstallQueueModel::finishWaitingRequest(const QString& requestId)
{
  const QString key = m_RequestToItem.take(requestId);
  if (key.isEmpty()) return;
  const int row = rowForId(key);
  if (row < 0) return;
  beginRemoveRows(QModelIndex(), row, row);
  adjustStatusCount(m_Items.at(row).status, -1);
  m_Items.removeAt(row);
  m_RowById.remove(key);
  for (int i = row; i < m_Items.size(); ++i) m_RowById[m_Items.at(i).id] = i;
  endRemoveRows();
}

void InstallQueueModel::insertOrReplace(Item item)
{
  const int oldRow = rowForId(item.id);
  if (oldRow >= 0) {
    if (m_Items.at(oldRow).status != item.status) {
      adjustStatusCount(m_Items.at(oldRow).status, -1);
      adjustStatusCount(item.status, 1);
    }
    m_Items[oldRow] = std::move(item);
    updateItem(oldRow);
    return;
  }
  const int row = m_Items.size();
  beginInsertRows(QModelIndex(), row, row);
  adjustStatusCount(item.status, 1);
  m_Items.push_back(std::move(item));
  m_RowById.insert(m_Items.constLast().id, row);
  endInsertRows();
}

void InstallQueueModel::updateItem(int row)
{
  if (row < 0 || row >= m_Items.size()) return;
  emit dataChanged(index(row, 0), index(row, ColumnCount - 1),
                   {Qt::DisplayRole, Qt::ToolTipRole, Qt::AccessibleDescriptionRole,
                    Qt::TextAlignmentRole, IdRole, RequestIdRole, StatusRole, CompletedRole,
                    TotalRole, UnitRole, SpeedRole, SubtitleRole, MessageRole,
                    IndeterminateRole});
}

void InstallQueueModel::pruneCompletedHistory()
{
  while (m_CompletedHistoryCount > MaxCompletedHistory) {
    int oldestRow = -1;
    qint64 oldestSequence = std::numeric_limits<qint64>::max();
    for (int row = 0; row < m_Items.size(); ++row) {
      const auto& item = m_Items.at(row);
      if (item.status == Status::Completed && item.completionSequence < oldestSequence) {
        oldestSequence = item.completionSequence;
        oldestRow = row;
      }
    }
    if (oldestRow < 0) return;
    const QString id = m_Items.at(oldestRow).id;
    beginRemoveRows(QModelIndex(), oldestRow, oldestRow);
    adjustStatusCount(m_Items.at(oldestRow).status, -1);
    m_Items.removeAt(oldestRow);
    m_RowById.remove(id);
    for (int row = oldestRow; row < m_Items.size(); ++row)
      m_RowById[m_Items.at(row).id] = row;
    endRemoveRows();
  }
}

void InstallQueueModel::changeStatus(Item& item, Status status)
{
  if (item.status == status) return;
  adjustStatusCount(item.status, -1);
  item.status = status;
  adjustStatusCount(item.status, 1);
}

void InstallQueueModel::adjustStatusCount(Status status, int adjustment)
{
  int* count = nullptr;
  switch (status) {
  case Status::Active: count = &m_ActiveCount; break;
  case Status::Failed: count = &m_FailedCount; break;
  case Status::Stopped: count = &m_StoppedCount; break;
  case Status::Completed: count = &m_CompletedHistoryCount; break;
  case Status::Waiting: count = &m_WaitingCount; break;
  }
  if (count) *count += adjustment;
}

QString InstallQueueModel::stageForStatus(Status status)
{
  switch (status) {
  case Status::Active: return tr("Active");
  case Status::Failed: return tr("Failed");
  case Status::Stopped: return tr("Stopped");
  case Status::Completed: return tr("Complete");
  case Status::Waiting: return tr("Needs input");
  }
  return {};
}
