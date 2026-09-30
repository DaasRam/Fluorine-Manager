#pragma once

#include "vfs/vfscatalog.h"

#include <QFutureWatcher>
#include <QObject>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>

// Runs one catalog reconciliation with owned cancellation and a bounded latest
// progress snapshot. No worker callback touches a QObject or a widget.
class VfsCatalogJob : public QObject
{
  Q_OBJECT
public:
  using Work = std::function<VfsCatalogResult(
      std::stop_token, const VfsCatalog::ProgressCallback&)>;

  explicit VfsCatalogJob(QObject* parent = nullptr);
  ~VfsCatalogJob() override;
  void start(Work work);
  void requestCancel();
  bool isFinished() const;
  void waitForFinished();
  std::optional<VfsCatalogProgress> latestProgress() const;
  VfsCatalogResult takeResult();

signals:
  void finished();

private:
  struct State;
  std::shared_ptr<State> m_state;
  std::stop_source m_stop;
  QFutureWatcher<void> m_watcher;
  bool m_started = false;
};
