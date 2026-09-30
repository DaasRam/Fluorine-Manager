#include "installprogressview.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QEvent>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QSortFilterProxyModel>
#include <QStyle>
#include <QStyleOptionProgressBar>
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>
#include <QStringList>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace
{
QString formatBytes(qint64 bytes)
{
  static const QStringList suffixes = {QStringLiteral("B"), QStringLiteral("KiB"),
                                       QStringLiteral("MiB"), QStringLiteral("GiB"),
                                       QStringLiteral("TiB"), QStringLiteral("PiB")};
  double value = static_cast<double>(std::max<qint64>(0, bytes));
  int suffix = 0;
  while (value >= 1024.0 && suffix + 1 < suffixes.size()) {
    value /= 1024.0;
    ++suffix;
  }
  const int precision = suffix == 0 || value >= 10.0 ? 0 : 1;
  return QStringLiteral("%1 %2").arg(QString::number(value, 'f', precision),
                                       suffixes.at(suffix));
}

QString formatPhaseProgress(qint64 completed, qint64 total, const QString& unit)
{
  completed = std::max<qint64>(0, completed);
  total = std::max<qint64>(0, total);
  QString readableUnit = unit;
  if (unit.compare(QStringLiteral("directives"), Qt::CaseInsensitive) == 0)
    readableUnit = QStringLiteral("operations");
  if (unit.compare(QStringLiteral("bytes"), Qt::CaseInsensitive) == 0) {
    if (total > 0) return QStringLiteral("%1 / %2").arg(formatBytes(completed), formatBytes(total));
    return formatBytes(completed);
  }
  if (total > 0) {
    const QString counts = QStringLiteral("%1 / %2").arg(completed).arg(total);
    return readableUnit.isEmpty() ? counts : counts + QLatin1Char(' ') + readableUnit;
  }
  const QString count = QString::number(completed);
  return readableUnit.isEmpty() ? count : count + QLatin1Char(' ') + readableUnit;
}

int progressPercent(qint64 completed, qint64 total)
{
  if (total <= 0) return 0;
  const long double fraction = static_cast<long double>(std::max<qint64>(0, completed)) /
                               static_cast<long double>(total);
  return static_cast<int>(std::clamp(fraction * 100.0L, 0.0L, 100.0L));
}

}

class InstallProgressView::QueueFilter final : public QSortFilterProxyModel
{
public:
  enum class View { Active, Attention, Completed };

  QueueFilter(View view, QObject* parent) : QSortFilterProxyModel(parent), m_View(view)
  {
    setDynamicSortFilter(true);
    setFilterRole(InstallQueueModel::StatusRole);
  }

protected:
  bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override
  {
    if (!sourceModel()) return false;
    const auto index = sourceModel()->index(sourceRow, InstallQueueModel::NameColumn, sourceParent);
    const auto value = index.data(InstallQueueModel::StatusRole);
    if (!value.isValid()) return false;
    const auto status = static_cast<InstallQueueModel::Status>(value.toInt());
    switch (m_View) {
    case View::Active:
      return status == InstallQueueModel::Status::Active;
    case View::Attention:
      return status == InstallQueueModel::Status::Failed ||
             status == InstallQueueModel::Status::Stopped ||
             status == InstallQueueModel::Status::Waiting;
    case View::Completed:
      return status == InstallQueueModel::Status::Completed;
    }
    return false;
  }

private:
  View m_View;
};

namespace
{
class InstallProgressDelegate final : public QStyledItemDelegate
{
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override
  {
    if (index.column() != InstallQueueModel::ProgressColumn ||
        index.data(InstallQueueModel::StatusRole).toInt() !=
            static_cast<int>(InstallQueueModel::Status::Active)) {
      QStyledItemDelegate::paint(painter, option, index);
      return;
    }

    QStyleOptionViewItem itemOption(option);
    initStyleOption(&itemOption, index);
    const QString text = index.data(Qt::DisplayRole).toString();
    itemOption.text.clear();
    QStyle* style = itemOption.widget ? itemOption.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &itemOption, painter, itemOption.widget);

    QStyleOptionProgressBar bar;
    bar.rect = itemOption.rect.adjusted(5, 4, -5, -4);
    bar.state = itemOption.state;
    bar.direction = itemOption.direction;
    bar.palette = itemOption.palette;
    bar.fontMetrics = itemOption.fontMetrics;
    bar.minimum = 0;
    const qint64 total = index.data(InstallQueueModel::TotalRole).toLongLong();
    const qint64 completed = index.data(InstallQueueModel::CompletedRole).toLongLong();
    if (total <= 0) {
      bar.maximum = 0;
      bar.progress = 0;
    } else {
      bar.maximum = 100;
      bar.progress = progressPercent(completed, total);
    }
    bar.text = text;
    bar.textVisible = true;
    bar.textAlignment = Qt::AlignCenter;
    style->drawControl(QStyle::CE_ProgressBar, &bar, painter, itemOption.widget);
  }
};

QTableView* createQueueView(QWidget* parent, const QString& objectName)
{
  class QueueTableView final : public QTableView
  {
  public:
    explicit QueueTableView(QWidget* parent) : QTableView(parent) {}

    void updateCompactRowHeight()
    {
      verticalHeader()->setDefaultSectionSize(
          std::max(34, fontMetrics().height() + 12));
    }

  protected:
    void changeEvent(QEvent* event) override
    {
      QTableView::changeEvent(event);
      if (event->type() == QEvent::FontChange ||
          event->type() == QEvent::ApplicationFontChange)
        updateCompactRowHeight();
    }
  };

  auto* view = new QueueTableView(parent);
  view->setObjectName(objectName);
  view->setAlternatingRowColors(true);
  view->setShowGrid(false);
  view->setWordWrap(false);
  view->setSortingEnabled(false);
  view->setSelectionBehavior(QAbstractItemView::SelectRows);
  view->setSelectionMode(QAbstractItemView::SingleSelection);
  view->setEditTriggers(QAbstractItemView::NoEditTriggers);
  view->verticalHeader()->hide();
  view->updateCompactRowHeight();
  view->horizontalHeader()->setStretchLastSection(false);
  view->setItemDelegateForColumn(InstallQueueModel::ProgressColumn,
                                 new InstallProgressDelegate(view));
  return view;
}
}

InstallProgressView::InstallProgressView(QWidget* parent) : QWidget(parent)
{
  setObjectName(QStringLiteral("installProgressView"));
  setMinimumSize(540, 260);

  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(8, 8, 8, 8);
  outer->setSpacing(6);

  auto* summary = new QWidget(this);
  summary->setObjectName(QStringLiteral("installProgressSummary"));
  auto* summaryLayout = new QVBoxLayout(summary);
  summaryLayout->setContentsMargins(0, 0, 0, 0);
  summaryLayout->setSpacing(4);

  m_PhaseLabel = new QLabel(summary);
  m_PhaseLabel->setObjectName(QStringLiteral("installPhaseLabel"));
  m_PhaseLabel->setProperty("secondary", true);
  m_PhaseProgress = new QProgressBar(summary);
  m_PhaseProgress->setObjectName(QStringLiteral("installPhaseProgress"));
  m_PhaseProgress->setTextVisible(true);
  m_PhaseProgress->setMinimumHeight(18);
  m_PhaseProgress->hide();
  summaryLayout->addWidget(m_PhaseLabel);
  summaryLayout->addWidget(m_PhaseProgress);

  auto* details = new QHBoxLayout;
  details->setContentsMargins(0, 0, 0, 0);
  details->setSpacing(12);
  m_CountLabel = new QLabel(summary);
  m_CountLabel->setObjectName(QStringLiteral("installQueueCounts"));
  m_ReuseLabel = new QLabel(summary);
  m_ReuseLabel->setObjectName(QStringLiteral("installQueueReuse"));
  m_ReuseLabel->setProperty("secondary", true);
  details->addWidget(m_CountLabel, 1);
  details->addWidget(m_ReuseLabel, 0, Qt::AlignRight);
  summaryLayout->addLayout(details);
  outer->addWidget(summary);

  m_Model = new InstallQueueModel(this);
  m_Tabs = new QTabWidget(this);
  m_Tabs->setObjectName(QStringLiteral("installQueueTabs"));

  m_ActiveFilter = new QueueFilter(QueueFilter::View::Active, this);
  m_AttentionFilter = new QueueFilter(QueueFilter::View::Attention, this);
  m_CompletedFilter = new QueueFilter(QueueFilter::View::Completed, this);
  m_ActiveFilter->setSourceModel(m_Model);
  m_AttentionFilter->setSourceModel(m_Model);
  m_CompletedFilter->setSourceModel(m_Model);

  m_ActiveView = createQueueView(m_Tabs, QStringLiteral("installActiveView"));
  m_AttentionView = createQueueView(m_Tabs, QStringLiteral("installAttentionView"));
  m_CompletedView = createQueueView(m_Tabs, QStringLiteral("installCompletedView"));
  m_ActiveView->setModel(m_ActiveFilter);
  m_AttentionView->setModel(m_AttentionFilter);
  m_CompletedView->setModel(m_CompletedFilter);
  // Per-section resize modes require a model: setting them while the header
  // has zero sections silently leaves all columns at Qt's default width.
  for (auto* view : {m_ActiveView, m_AttentionView, m_CompletedView}) {
    auto* header = view->horizontalHeader();
    header->setResizeContentsPrecision(64);
    header->setSectionResizeMode(InstallQueueModel::NameColumn, QHeaderView::Stretch);
    header->setSectionResizeMode(InstallQueueModel::StageColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(InstallQueueModel::ProgressColumn, QHeaderView::Stretch);
    header->setSectionResizeMode(InstallQueueModel::SpeedColumn, QHeaderView::ResizeToContents);
  }

  m_Tabs->addTab(m_ActiveView, tr("Active (0)"));
  m_Tabs->addTab(m_AttentionView, tr("Needs attention (0)"));
  m_Tabs->addTab(m_CompletedView, tr("Completed (0)"));
  outer->addWidget(m_Tabs, 1);

  m_UpdateTimer = new QTimer(this);
  m_UpdateTimer->setSingleShot(true);
  m_UpdateTimer->setInterval(100);
  connect(m_UpdateTimer, &QTimer::timeout, this, &InstallProgressView::flushPendingUpdates);
  connect(m_AttentionView, &QTableView::activated, this,
          [this](const QModelIndex& index) { handleActivation(index, m_AttentionFilter); });

  updateSummary();
}

void InstallProgressView::reset()
{
  m_UpdateTimer->stop();
  m_PendingProgress.clear();
  m_HasPendingPhaseProgress = false;
  m_PhaseEnded = false;
  m_Model->resetQueue();
  m_Phase.clear();
  m_PhaseUnit.clear();
  m_PhaseCompleted = 0;
  m_PhaseTotal = 0;
  m_PhaseLabel->clear();
  m_PhaseProgress->hide();
  updateSummary();
}

void InstallProgressView::setPhase(const QString& phase)
{
  m_HasPendingPhaseProgress = false;
  m_PhaseEnded = false;
  if (m_PendingProgress.isEmpty()) m_UpdateTimer->stop();
  m_Phase = phase;
  m_PhaseUnit.clear();
  m_PhaseCompleted = 0;
  m_PhaseTotal = 0;
  m_PhaseLabel->setText(m_Phase);
  m_PhaseProgress->setRange(0, 0);
  m_PhaseProgress->setValue(0);
  m_PhaseProgress->setFormat(QString());
  m_PhaseProgress->setVisible(!m_Phase.isEmpty());
}

void InstallProgressView::setPhaseEnded(const QString& phase)
{
  flushPendingUpdates();
  m_PhaseEnded = true;
  m_Phase = phase;
  m_PhaseUnit.clear();
  m_PhaseCompleted = 0;
  m_PhaseTotal = 0;
  m_PhaseLabel->setText(m_Phase);
  m_PhaseProgress->setRange(0, 1);
  m_PhaseProgress->setValue(0);
  m_PhaseProgress->setFormat(QString());
  m_PhaseProgress->hide();
}

void InstallProgressView::setProgress(qint64 completed, qint64 total, const QString& unit)
{
  if (m_PhaseEnded) return;
  m_PendingPhaseProgress = {completed, total, 0.0, unit};
  m_HasPendingPhaseProgress = true;
  if (!m_UpdateTimer->isActive()) m_UpdateTimer->start();
}

void InstallProgressView::setReused(qint64 count, qint64 bytes)
{
  m_Model->setReused(count, bytes);
  updateSummary();
}

void InstallProgressView::startItem(const QString& id, const QString& name,
                                    const QString& displayName, const QString& subtitle,
                                    const QString& stage, qint64 total, const QString& unit)
{
  flushPendingUpdatesFor(id);
  m_Model->startItem(id, name, displayName, subtitle, stage, total, unit);
  updateSummary();
}

void InstallProgressView::progressItem(const QString& id, qint64 completed, qint64 total,
                                       double speed, const QString& unit)
{
  queueProgressUpdate(id, completed, total, speed, unit);
}

void InstallProgressView::messageItem(const QString& id, const QString& message)
{
  flushPendingUpdatesFor(id);
  m_Model->messageItem(id, message);
}

void InstallProgressView::completeItem(const QString& id)
{
  flushPendingUpdatesFor(id);
  m_Model->completeItem(id);
  updateSummary();
}

void InstallProgressView::failItem(const QString& id, const QString& message)
{
  flushPendingUpdatesFor(id);
  m_Model->failItem(id, message);
  updateSummary();
}

void InstallProgressView::stopActive(const QString& reason)
{
  flushPendingUpdates();
  m_Model->stopActive(reason);
  updateSummary();
}

void InstallProgressView::setWaitingRequest(const QString& requestId, const QString& name,
                                            const QString& detail)
{
  m_Model->setWaitingRequest(requestId, name, detail);
  updateSummary();
  if (!requestId.isEmpty()) m_Tabs->setCurrentWidget(m_AttentionView);
}

void InstallProgressView::finishWaitingRequest(const QString& requestId)
{
  m_Model->finishWaitingRequest(requestId);
  updateSummary();
}

InstallQueueModel* InstallProgressView::model() const { return m_Model; }

InstallQueueModel::Counts InstallProgressView::counts() const { return m_Model->counts(); }

int InstallProgressView::activeCount() const { return counts().active; }

int InstallProgressView::needsAttentionCount() const { return counts().needsAttention; }

qint64 InstallProgressView::completedCount() const { return counts().completedTotal; }

int InstallProgressView::waitingRequestCount() const { return counts().waitingRequests; }

QTabWidget* InstallProgressView::tabs() const { return m_Tabs; }

QTableView* InstallProgressView::activeView() const { return m_ActiveView; }

QTableView* InstallProgressView::attentionView() const { return m_AttentionView; }

QTableView* InstallProgressView::completedView() const { return m_CompletedView; }

void InstallProgressView::queueProgressUpdate(const QString& id, qint64 completed,
                                              qint64 total, double speed,
                                              const QString& unit)
{
  if (id.isEmpty()) return;
  m_PendingProgress.insert(id, PendingProgress{completed, total, speed, unit});
  if (!m_UpdateTimer->isActive()) m_UpdateTimer->start();
}

void InstallProgressView::flushPendingUpdates()
{
  if (m_UpdateTimer->isActive()) m_UpdateTimer->stop();
  QHash<QString, PendingProgress> pending;
  pending.swap(m_PendingProgress);
  for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
    const auto& update = it.value();
    m_Model->progressItem(it.key(), update.completed, update.total, update.speed, update.unit);
  }
  if (m_HasPendingPhaseProgress) {
    const auto update = m_PendingPhaseProgress;
    m_HasPendingPhaseProgress = false;
    applyPhaseProgress(update.completed, update.total, update.unit);
  }
}

void InstallProgressView::flushPendingUpdatesFor(const QString& id)
{
  auto it = m_PendingProgress.find(id);
  if (it == m_PendingProgress.end()) return;
  const PendingProgress update = it.value();
  m_PendingProgress.erase(it);
  m_Model->progressItem(id, update.completed, update.total, update.speed, update.unit);
  if (m_PendingProgress.isEmpty() && !m_HasPendingPhaseProgress) m_UpdateTimer->stop();
}

void InstallProgressView::applyPhaseProgress(qint64 completed, qint64 total,
                                             const QString& unit)
{
  m_PhaseCompleted = std::max<qint64>(0, completed);
  m_PhaseTotal = std::max<qint64>(0, total);
  m_PhaseUnit = unit;
  m_PhaseProgress->setVisible(!m_Phase.isEmpty() && !m_PhaseEnded);
  if (m_PhaseTotal <= 0) {
    m_PhaseProgress->setRange(0, 0);
  } else {
    m_PhaseProgress->setRange(0, 100);
    m_PhaseProgress->setValue(progressPercent(m_PhaseCompleted, m_PhaseTotal));
  }
  m_PhaseProgress->setFormat(formatPhaseProgress(m_PhaseCompleted, m_PhaseTotal, m_PhaseUnit));
}

void InstallProgressView::updateSummary()
{
  const auto current = counts();
  m_Tabs->setTabText(0, tr("Active (%1)").arg(current.active));
  m_Tabs->setTabText(1, tr("Needs attention (%1)").arg(current.needsAttention));
  m_Tabs->setTabText(2, tr("Completed (%1)").arg(current.completedInHistory));
  m_CountLabel->setText(tr("%1 active  ·  %2 need attention  ·  %3 completed")
                            .arg(current.active)
                            .arg(current.needsAttention)
                            .arg(current.completedTotal));
  m_ReuseLabel->setText(tr("Reused: %1 archives  ·  %2")
                            .arg(current.reusedCount)
                            .arg(formatBytes(current.reusedBytes)));
}

void InstallProgressView::handleActivation(const QModelIndex& proxyIndex,
                                           QueueFilter* proxy)
{
  if (!proxy || !proxyIndex.isValid()) return;
  const QModelIndex sourceIndex = proxy->mapToSource(proxyIndex);
  if (sourceIndex.data(InstallQueueModel::StatusRole).toInt() !=
      static_cast<int>(InstallQueueModel::Status::Waiting))
    return;
  const QString requestId = sourceIndex.data(InstallQueueModel::RequestIdRole).toString();
  if (!requestId.isEmpty()) emit attentionActivated(requestId);
}
