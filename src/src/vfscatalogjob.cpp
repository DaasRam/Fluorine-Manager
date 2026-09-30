#include "vfscatalogjob.h"

#include <QtConcurrent/QtConcurrentRun>
#include <exception>
#include <mutex>
#include <stdexcept>

struct VfsCatalogJob::State
{
  mutable std::mutex mutex;
  std::optional<VfsCatalogProgress> progress;
  std::optional<VfsCatalogResult> result;
  std::exception_ptr error;
};

VfsCatalogJob::VfsCatalogJob(QObject* parent)
    : QObject(parent), m_state(std::make_shared<State>())
{
  connect(&m_watcher, &QFutureWatcher<void>::finished,
          this, &VfsCatalogJob::finished);
}

VfsCatalogJob::~VfsCatalogJob()
{
  requestCancel();
  if (m_started) m_watcher.waitForFinished();
}

void VfsCatalogJob::start(Work work)
{
  if (m_started) throw std::logic_error("Catalog job can only be started once");
  m_started = true;
  const auto state = m_state;
  const auto token = m_stop.get_token();
  m_watcher.setFuture(QtConcurrent::run([state, token, work = std::move(work)] {
    try {
      auto result = work(token, [state](const VfsCatalogProgress& progress) {
        std::scoped_lock lock(state->mutex);
        state->progress = progress;
      });
      std::scoped_lock lock(state->mutex);
      state->result.emplace(std::move(result));
    } catch (...) {
      std::scoped_lock lock(state->mutex);
      state->error = std::current_exception();
    }
  }));
}

void VfsCatalogJob::requestCancel()
{
  m_stop.request_stop();
}

bool VfsCatalogJob::isFinished() const
{
  return m_started && m_watcher.isFinished();
}

void VfsCatalogJob::waitForFinished()
{
  if (m_started) m_watcher.waitForFinished();
}

std::optional<VfsCatalogProgress> VfsCatalogJob::latestProgress() const
{
  std::scoped_lock lock(m_state->mutex);
  return m_state->progress;
}

VfsCatalogResult VfsCatalogJob::takeResult()
{
  if (!isFinished()) throw std::logic_error("Catalog job is still running");
  std::scoped_lock lock(m_state->mutex);
  if (m_state->error) std::rethrow_exception(m_state->error);
  if (!m_state->result) throw std::logic_error("Catalog result was already consumed");
  auto result = std::move(*m_state->result);
  m_state->result.reset();
  return result;
}
