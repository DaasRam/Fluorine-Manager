#pragma once

#include "installqueuemodel.h"

#include <QHash>
#include <QWidget>

class QLabel;
class QProgressBar;
class QTableView;
class QTabWidget;
class QTimer;

class InstallProgressView final : public QWidget
{
  Q_OBJECT

public:
  explicit InstallProgressView(QWidget* parent = nullptr);

  void reset();
  void setPhase(const QString& phase);
  void setPhaseEnded(const QString& phase);
  void setProgress(qint64 completed, qint64 total, const QString& unit);
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

  InstallQueueModel* model() const;
  InstallQueueModel::Counts counts() const;
  int activeCount() const;
  int needsAttentionCount() const;
  qint64 completedCount() const;
  int waitingRequestCount() const;

  QTabWidget* tabs() const;
  QTableView* activeView() const;
  QTableView* attentionView() const;
  QTableView* completedView() const;

signals:
  void attentionActivated(const QString& requestId);

private:
  class QueueFilter;

  void queueProgressUpdate(const QString& id, qint64 completed, qint64 total,
                           double speed, const QString& unit);
  void flushPendingUpdatesFor(const QString& id);
  void flushPendingUpdates();
  void applyPhaseProgress(qint64 completed, qint64 total, const QString& unit);
  void updateSummary();
  void handleActivation(const QModelIndex& proxyIndex, QueueFilter* proxy);

  InstallQueueModel* m_Model = nullptr;
  QLabel* m_PhaseLabel = nullptr;
  QProgressBar* m_PhaseProgress = nullptr;
  QLabel* m_ReuseLabel = nullptr;
  QLabel* m_CountLabel = nullptr;
  QTabWidget* m_Tabs = nullptr;
  QTableView* m_ActiveView = nullptr;
  QTableView* m_AttentionView = nullptr;
  QTableView* m_CompletedView = nullptr;
  QueueFilter* m_ActiveFilter = nullptr;
  QueueFilter* m_AttentionFilter = nullptr;
  QueueFilter* m_CompletedFilter = nullptr;
  QTimer* m_UpdateTimer = nullptr;

  struct PendingProgress {
    qint64 completed = 0;
    qint64 total = 0;
    double speed = 0.0;
    QString unit;
  };
  QHash<QString, PendingProgress> m_PendingProgress;
  bool m_HasPendingPhaseProgress = false;
  bool m_PhaseEnded = false;
  PendingProgress m_PendingPhaseProgress;
  QString m_Phase;
  QString m_PhaseUnit;
  qint64 m_PhaseCompleted = 0;
  qint64 m_PhaseTotal = 0;
};
