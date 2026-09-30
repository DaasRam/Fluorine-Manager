#pragma once

#include <QAbstractTableModel>
#include <QHash>
#include <QString>
#include <QVector>

class InstallQueueModel final : public QAbstractTableModel
{
  Q_OBJECT

public:
  enum class Status { Active, Failed, Stopped, Completed, Waiting };
  Q_ENUM(Status)

  enum Column { NameColumn, StageColumn, ProgressColumn, SpeedColumn, ColumnCount };
  enum Role {
    IdRole = Qt::UserRole + 1,
    RequestIdRole,
    StatusRole,
    CompletedRole,
    TotalRole,
    UnitRole,
    SpeedRole,
    SubtitleRole,
    MessageRole,
    IndeterminateRole
  };

  struct Counts {
    int active = 0;
    int failed = 0;
    int stopped = 0;
    int needsAttention = 0;
    int completedInHistory = 0;
    int waitingRequests = 0;
    qint64 completedTotal = 0;
    qint64 completedBytes = 0;
    qint64 reusedCount = 0;
    qint64 reusedBytes = 0;
  };

  explicit InstallQueueModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;
  int columnCount(const QModelIndex& parent = QModelIndex()) const override;
  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override;
  QHash<int, QByteArray> roleNames() const override;

  Counts counts() const;
  int rowForId(const QString& id) const;
  QString idAt(int row) const;
  Status statusAt(int row) const;

  void resetQueue();
  void setReused(qint64 count, qint64 bytes);
  void startItem(const QString& id, const QString& name, const QString& displayName,
                 const QString& subtitle, const QString& stage, qint64 total,
                 const QString& unit);
  void progressItem(const QString& id, qint64 completed, qint64 total, double speed,
                    const QString& unit);
  void messageItem(const QString& id, const QString& message);
  void completeItem(const QString& id);
  void failItem(const QString& id, const QString& message);
  void stopActive(const QString& reason);
  void setWaitingRequest(const QString& requestId, const QString& name,
                         const QString& detail);
  void finishWaitingRequest(const QString& requestId);

  static constexpr int MaxCompletedHistory = 1000;

private:
  struct Item {
    QString id;
    QString requestId;
    QString name;
    QString displayName;
    QString subtitle;
    QString stage;
    QString message;
    QString unit;
    qint64 completed = 0;
    qint64 total = 0;
    double speed = 0.0;
    Status status = Status::Active;
    qint64 completionSequence = 0;
  };

  void insertOrReplace(Item item);
  void updateItem(int row);
  void pruneCompletedHistory();
  void changeStatus(Item& item, Status status);
  void adjustStatusCount(Status status, int adjustment);
  static QString stageForStatus(Status status);

  QVector<Item> m_Items;
  QHash<QString, int> m_RowById;
  QHash<QString, QString> m_RequestToItem;
  qint64 m_CompletedTotal = 0;
  qint64 m_CompletedBytes = 0;
  qint64 m_ReusedCount = 0;
  qint64 m_ReusedBytes = 0;
  qint64 m_CompletionSequence = 0;
  int m_ActiveCount = 0;
  int m_FailedCount = 0;
  int m_StoppedCount = 0;
  int m_WaitingCount = 0;
  int m_CompletedHistoryCount = 0;
};
