#include "downloadresponse.h"

#include <QTemporaryFile>
#include <gtest/gtest.h>

TEST(DownloadResponse, IgnoredRangeReplacesPartialBody)
{
  QTemporaryFile output;
  ASSERT_TRUE(output.open());
  ASSERT_EQ(output.write("partial", 7), 7);
  ASSERT_TRUE(output.flush());
  const auto result = DownloadResponse::prepare(output, 7, 200, {}, 8);
  ASSERT_TRUE(result.accepted);
  EXPECT_EQ(result.offset, 0);
  EXPECT_EQ(output.size(), 0);
  ASSERT_EQ(output.write("complete", 8), 8);
  ASSERT_TRUE(output.flush());
  ASSERT_TRUE(output.seek(0));
  EXPECT_EQ(output.readAll(), "complete");
}

TEST(DownloadResponse, IgnoredRangeReplacesPartialOpenedInAppendMode)
{
  QTemporaryFile output;
  ASSERT_TRUE(output.open());
  ASSERT_EQ(output.write("partial", 7), 7);
  ASSERT_TRUE(output.flush());
  const QString path = output.fileName();
  output.close();
  QFile resumed(path);
  ASSERT_TRUE(resumed.open(QIODevice::WriteOnly | QIODevice::Append));

  const auto result = DownloadResponse::prepare(resumed, 7, 200, {}, 8);
  ASSERT_TRUE(result.accepted);
  ASSERT_EQ(resumed.write("complete", 8), 8);
  ASSERT_TRUE(resumed.flush());
  resumed.close();
  ASSERT_TRUE(resumed.open(QIODevice::ReadOnly));
  EXPECT_EQ(resumed.readAll(), "complete") << path.toStdString();
}

TEST(DownloadResponse, MatchingRangeContinuesExactlyOnce)
{
  QTemporaryFile output;
  ASSERT_TRUE(output.open());
  ASSERT_EQ(output.write("part", 4), 4);
  ASSERT_TRUE(output.flush());
  const auto result = DownloadResponse::prepare(output, 4, 206, "bytes 4-7/8", 4);
  ASSERT_TRUE(result.accepted);
  EXPECT_EQ(result.offset, 4);
  EXPECT_EQ(result.totalSize, 8);
  EXPECT_EQ(result.bodySize, 4);
  ASSERT_EQ(output.write("rest", 4), 4);
  ASSERT_TRUE(output.seek(0));
  EXPECT_EQ(output.readAll(), "partrest");
}

TEST(DownloadResponse, InconsistentRangeCannotModifyPartialFile)
{
  for (const QByteArray range : {"bytes 0-7/8", "bytes 5-7/8", "bytes 4-5/8",
                                 "bytes 4-8/8", "bytes 4-7/*", "invalid",
                                 "bytes 4-9223372036854775808/9223372036854775809"}) {
    QTemporaryFile output;
    ASSERT_TRUE(output.open());
    ASSERT_EQ(output.write("part", 4), 4);
    ASSERT_TRUE(output.flush());
    EXPECT_FALSE(DownloadResponse::prepare(output, 4, 206, range, -1).accepted);
    ASSERT_TRUE(output.seek(0));
    EXPECT_EQ(output.readAll(), "part");
  }
}

TEST(DownloadResponse, RejectsHttpErrorsAndMismatchedLength)
{
  QTemporaryFile output;
  ASSERT_TRUE(output.open());
  EXPECT_FALSE(DownloadResponse::prepare(output, 0, 403, {}, 10).accepted);
  EXPECT_FALSE(DownloadResponse::prepare(output, 0, 206, "bytes 0-7/8", 7).accepted);
  EXPECT_FALSE(DownloadResponse::prepare(output, 0, 206, "bytes 0-7/8", 8, "gzip").accepted);
  EXPECT_FALSE(DownloadResponse::prepare(output, 0, 0, {}, 8).accepted);
  EXPECT_TRUE(DownloadResponse::prepare(output, 0, 200, {}, -1).accepted);
}

TEST(DownloadResponse, StatuslessNonHttpRestartIsExplicit)
{
  QTemporaryFile output;
  ASSERT_TRUE(output.open());
  ASSERT_EQ(output.write("partial", 7), 7);
  ASSERT_TRUE(output.flush());
  EXPECT_FALSE(DownloadResponse::prepare(output, 7, 0, {}, 8, {},
                                         QStringLiteral("https"))
                   .accepted);
  EXPECT_EQ(output.size(), 7);
  const auto result = DownloadResponse::prepare(output, 7, 0, {}, 8, {},
                                                QStringLiteral("file"));
  ASSERT_TRUE(result.accepted);
  EXPECT_EQ(output.size(), 0);
}
