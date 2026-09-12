#include "downloadwritelifecycle.h"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QHash>
#include <QObject>
#include <QScopedValueRollback>

#include <algorithm>
#include <memory>

namespace
{
TEST(DownloadAdmissionTest, PausedResolutionRetiresPendingBeforeNetworkAndAllowsRetry)
{
  bool suppressed = false;
  bool pending = true;
  int requests = 0;
  int retirements = 0;
  auto resolve = [&] {
    if (download_write::rejectSuppressedStart(suppressed, [&] {
          ++retirements;
          EXPECT_EQ(requests, 0);
          pending = false;
        })) {
      return;
    }
    ++requests;
    pending = false;
  };

  // The metadata reply arrives in pauseAll's nested event loop, after the
  // metadata request has retired. It must not leave an unresolvable queue row.
  {
    QScopedValueRollback<bool> pause(suppressed, true);
    resolve();
    EXPECT_FALSE(pending);
    EXPECT_EQ(requests, 0);
    EXPECT_EQ(retirements, 1);
  }

  // A later exit prompt is cancelled. The same file can be queued and started.
  EXPECT_FALSE(suppressed);
  ASSERT_FALSE(pending);
  pending = true;
  resolve();
  EXPECT_EQ(requests, 1);
  EXPECT_EQ(retirements, 1);
  EXPECT_FALSE(pending);
}

TEST(DownloadAdmissionTest, RejectionDoesNotRetireAReentrantSuccessor)
{
  bool pending = true;
  int retirements = 0;
  const bool rejected = download_write::rejectSuppressedStart(true, [&] {
    pending = false;
    ++retirements;
    // A model-update callback introduces a successor after retiring this row.
    pending = true;
  });
  EXPECT_TRUE(rejected);
  EXPECT_TRUE(pending);
  EXPECT_EQ(retirements, 1);
}

class LimitedSink : public QIODevice
{
public:
  explicit LimitedSink(qint64 limit) : m_Limit(limit)
  {
    open(QIODevice::WriteOnly);
  }

  QByteArray bytes() const { return m_Bytes; }

protected:
  qint64 readData(char*, qint64) override { return -1; }

  qint64 writeData(const char* data, qint64 size) override
  {
    const qint64 accepted = std::min(size, m_Limit);
    if (accepted > 0) {
      m_Bytes.append(data, accepted);
    }
    return accepted;
  }

private:
  qint64 m_Limit;
  QByteArray m_Bytes;
};

struct Entry
{
  QPointer<QObject> reply;
  bool canceled{false};
};

using Identity = download_write::Identity<QObject>;

Entry* reacquire(const Identity& identity, const QHash<unsigned int, Entry*>& entries)
{
  return download_write::reacquire<Entry>(
      identity,
      [&entries](unsigned int id) { return entries.value(id, nullptr); },
      [](const Entry& entry) { return entry.reply.data(); });
}

Entry* reacquireSameOrRetired(
    const Identity& identity, const QHash<unsigned int, Entry*>& entries)
{
  return download_write::reacquireSameOrRetired<Entry>(
      identity,
      [&entries](unsigned int id) { return entries.value(id, nullptr); },
      [](const Entry& entry) { return entry.reply.data(); });
}

TEST(DownloadWriteLifecycleTest, CompleteWriteReportsExactByteCounts)
{
  QByteArray bytes("abcdef");
  QBuffer source(&bytes);
  ASSERT_TRUE(source.open(QIODevice::ReadOnly));
  LimitedSink destination(bytes.size());

  const auto result = download_write::drain(source, destination);

  EXPECT_TRUE(result.complete());
  EXPECT_EQ(result.requested, bytes.size());
  EXPECT_EQ(result.written, bytes.size());
  EXPECT_EQ(destination.bytes(), bytes);
}

TEST(DownloadWriteLifecycleTest, ShortAndZeroWritesFailExplicitly)
{
  QByteArray partialBytes("abcdef");
  QBuffer partialSource(&partialBytes);
  ASSERT_TRUE(partialSource.open(QIODevice::ReadOnly));
  LimitedSink partialDestination(3);

  const auto partial = download_write::drain(partialSource, partialDestination);
  EXPECT_FALSE(partial.complete());
  EXPECT_EQ(partial.requested, 6);
  EXPECT_EQ(partial.written, 3);
  EXPECT_EQ(partialDestination.bytes(), QByteArray("abc"));

  QByteArray zeroBytes("abcdef");
  QBuffer zeroSource(&zeroBytes);
  ASSERT_TRUE(zeroSource.open(QIODevice::ReadOnly));
  LimitedSink zeroDestination(0);
  const auto zero = download_write::drain(zeroSource, zeroDestination);
  EXPECT_FALSE(zero.complete());
  EXPECT_EQ(zero.requested, 6);
  EXPECT_EQ(zero.written, 0);
}

TEST(DownloadWriteLifecycleTest, EmptyWriteIsACompleteNoOp)
{
  QByteArray bytes;
  QBuffer source(&bytes);
  ASSERT_TRUE(source.open(QIODevice::ReadOnly));
  LimitedSink destination(0);

  const auto result = download_write::drain(source, destination);

  EXPECT_TRUE(result.complete());
  EXPECT_EQ(result.requested, 0);
  EXPECT_EQ(result.written, 0);
}

TEST(DownloadWriteLifecycleTest, MissingSourceIsNotACompletedWrite)
{
  const download_write::Result result;
  EXPECT_FALSE(result.complete());
  EXPECT_EQ(result.status, download_write::Status::Unavailable);
}

TEST(DownloadWriteLifecycleTest, ExactLiveIdentityIsRequired)
{
  auto expectedReply = std::make_unique<QObject>();
  auto replacementReply = std::make_unique<QObject>();
  Entry entry{expectedReply.get()};
  QHash<unsigned int, Entry*> entries{{17, &entry}};
  const Identity identity{17, expectedReply.get()};

  EXPECT_EQ(reacquire(identity, entries), &entry);

  entries.remove(17);
  EXPECT_EQ(reacquire(identity, entries), nullptr);

  entries.insert(17, &entry);
  entry.reply = replacementReply.get();
  EXPECT_EQ(reacquire(identity, entries), nullptr);
}

TEST(DownloadWriteLifecycleTest, DestroyedExpectedReplyCannotAuthenticate)
{
  auto expectedReply = std::make_unique<QObject>();
  Entry entry{expectedReply.get()};
  QHash<unsigned int, Entry*> entries{{29, &entry}};
  const Identity identity{29, expectedReply.get()};

  expectedReply.reset();

  EXPECT_TRUE(identity.reply.isNull());
  EXPECT_EQ(reacquire(identity, entries), nullptr);
}

TEST(DownloadWriteLifecycleTest, SameOrRetiredReplyRejectsReplacement)
{
  auto expectedReply = std::make_unique<QObject>();
  auto replacementReply = std::make_unique<QObject>();
  Entry entry{expectedReply.get()};
  QHash<unsigned int, Entry*> entries{{41, &entry}};
  const Identity identity{41, expectedReply.get()};

  EXPECT_EQ(reacquireSameOrRetired(identity, entries), &entry);

  entry.reply.clear();
  EXPECT_EQ(reacquireSameOrRetired(identity, entries), &entry);

  entry.reply = replacementReply.get();
  EXPECT_EQ(reacquireSameOrRetired(identity, entries), nullptr);

  entry.reply.clear();
  entries.remove(41);
  EXPECT_EQ(reacquireSameOrRetired(identity, entries), nullptr);
}

TEST(DownloadWriteLifecycleTest, ShortWriteRemovalRejectsContinuation)
{
  QByteArray bytes("abcdef");
  QBuffer source(&bytes);
  ASSERT_TRUE(source.open(QIODevice::ReadOnly));
  LimitedSink destination(2);

  auto expectedReply = std::make_unique<QObject>();
  Entry entry{expectedReply.get()};
  QHash<unsigned int, Entry*> entries{{53, &entry}};
  const Identity identity{53, expectedReply.get()};
  int replacementRequests = 0;

  const auto continuation = download_write::continueAfterWrite<Entry>(
      [&] {
        const auto result = download_write::drain(source, destination);
        if (!result.complete()) {
          // Models a direct failure callback erasing the original row before
          // writeData returns to downloadFinished or resumeDownloadInt.
          entries.remove(identity.downloadID);
        }
        return result;
      },
      [&] { return reacquire(identity, entries); });

  if (continuation.download != nullptr) {
    ++replacementRequests;
  }
  EXPECT_FALSE(continuation.result.complete());
  EXPECT_EQ(continuation.download, nullptr);
  EXPECT_EQ(replacementRequests, 0);
}

TEST(DownloadWriteLifecycleTest, CallbackReplacementCannotContinueResume)
{
  QByteArray bytes("abcdef");
  QBuffer source(&bytes);
  ASSERT_TRUE(source.open(QIODevice::ReadOnly));
  LimitedSink destination(0);

  auto expectedReply = std::make_unique<QObject>();
  auto replacementReply = std::make_unique<QObject>();
  Entry original{expectedReply.get()};
  Entry replacement{replacementReply.get()};
  QHash<unsigned int, Entry*> entries{{67, &original}};
  const Identity identity{67, expectedReply.get()};
  int replacementRequests = 0;

  const auto continuation = download_write::continueAfterWrite<Entry>(
      [&] {
        const auto result = download_write::drain(source, destination);
        entries.insert(identity.downloadID, &replacement);
        return result;
      },
      [&] { return reacquire(identity, entries); });

  if (continuation.download != nullptr) {
    ++replacementRequests;
  }
  EXPECT_FALSE(continuation.result.complete());
  EXPECT_EQ(continuation.download, nullptr);
  EXPECT_EQ(replacementRequests, 0);
}

TEST(DownloadWriteLifecycleTest, SurvivingShortWriteRemainsCanceled)
{
  QByteArray bytes("abcdef");
  QBuffer source(&bytes);
  ASSERT_TRUE(source.open(QIODevice::ReadOnly));
  LimitedSink destination(0);

  auto expectedReply = std::make_unique<QObject>();
  Entry entry{expectedReply.get()};
  QHash<unsigned int, Entry*> entries{{71, &entry}};
  const Identity identity{71, expectedReply.get()};

  const auto continuation = download_write::continueAfterWrite<Entry>(
      [&] {
        const auto result = download_write::drain(source, destination);
        if (!result.complete()) {
          entry.canceled = true;
        }
        return result;
      },
      [&] { return reacquire(identity, entries); });

  ASSERT_EQ(continuation.download, &entry);
  EXPECT_FALSE(continuation.result.complete());
  EXPECT_TRUE(continuation.download->canceled);
}
}  // namespace
