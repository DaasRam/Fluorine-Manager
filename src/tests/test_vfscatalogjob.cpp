#include "vfscatalogjob.h"

#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>

TEST(VfsCatalogJob, KeepsGuiEventsRunningAndCoalescesProgress)
{
  VfsCatalogJob job;
  std::atomic<bool> release{false};
  std::condition_variable_any ready;
  std::mutex mutex;
  bool ranOffGuiThread = false;
  job.start([&](std::stop_token token, const auto& report) {
    ranOffGuiThread = QThread::currentThread() != QCoreApplication::instance()->thread();
    for (int i = 1; i <= 5000; ++i) {
      VfsCatalogProgress progress;
      progress.files_scanned = i;
      report(progress);
    }
    std::unique_lock lock(mutex);
    ready.wait(lock, token, [&] { return release.load(); });
    if (token.stop_requested()) throw VfsCatalogCancelled();
    return VfsCatalogResult{};
  });
  QEventLoop loop;
  QTimer pulse;
  QTimer watchdog;
  int ticks = 0;
  bool timedOut = false;
  QObject::connect(&pulse, &QTimer::timeout, &loop, [&] {
    if (++ticks == 5) {
      EXPECT_FALSE(job.isFinished());
      {
        std::scoped_lock lock(mutex);
        release = true;
      }
      ready.notify_all();
    }
  });
  QObject::connect(&watchdog, &QTimer::timeout, &loop, [&] {
    timedOut = true;
    job.requestCancel();
    loop.quit();
  });
  QObject::connect(&job, &VfsCatalogJob::finished, &loop, &QEventLoop::quit);
  pulse.start(5);
  watchdog.start(5000);
  loop.exec();
  job.waitForFinished();
  ASSERT_FALSE(timedOut);
  EXPECT_GE(ticks, 5);
  EXPECT_TRUE(ranOffGuiThread);
  ASSERT_TRUE(job.latestProgress().has_value());
  EXPECT_EQ(job.latestProgress()->files_scanned, 5000);
  EXPECT_NO_THROW(job.takeResult());
  EXPECT_THROW(job.takeResult(), std::logic_error);
}

TEST(VfsCatalogJob, PropagatesOriginalFailureType)
{
  struct FixtureFailure : std::runtime_error {
    FixtureFailure() : std::runtime_error("fixture failure") {}
  };
  VfsCatalogJob job;
  job.start([](std::stop_token, const auto&) -> VfsCatalogResult { throw FixtureFailure(); });
  job.waitForFinished();
  EXPECT_THROW(job.takeResult(), FixtureFailure);
}

TEST(VfsCatalogJob, PropagatesCooperativeCancellation)
{
  VfsCatalogJob job;
  job.requestCancel();
  job.start([](std::stop_token token, const auto&) {
    if (token.stop_requested()) throw VfsCatalogCancelled();
    return VfsCatalogResult{};
  });
  job.waitForFinished();
  EXPECT_THROW(job.takeResult(), VfsCatalogCancelled);
}

TEST(VfsCatalogJob, DestructionStopsAndJoinsOwnedWorker)
{
  std::promise<void> started;
  bool exited = false;
  {
    VfsCatalogJob job;
    job.start([&](std::stop_token token, const auto&) {
      std::mutex mutex;
      std::condition_variable_any stopped;
      std::unique_lock lock(mutex);
      started.set_value();
      stopped.wait(lock, token, [] { return false; });
      exited = true;
      throw VfsCatalogCancelled();
      return VfsCatalogResult{};
    });
    started.get_future().wait();
  }
  EXPECT_TRUE(exited);
}

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
