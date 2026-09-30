#include "../src/modlistmetadata.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QTemporaryDir>
#include <gtest/gtest.h>

using namespace ModlistMetadata;

TEST(ModlistMetadata, ParsesFractionalIsoDatesAndOffsets)
{
  const auto fractional = parseIsoDateTime(QStringLiteral("2005-03-04T10:31:00.8081925Z"));
  ASSERT_TRUE(fractional.has_value());
  EXPECT_EQ(fractional->timeSpec(), Qt::UTC);
  EXPECT_EQ(fractional->date(), QDate(2005, 3, 4));
  EXPECT_EQ(fractional->time().msec(), 808);

  const auto offset = parseIsoDateTime(QStringLiteral("2005-03-04T10:31:00.8+0230"));
  ASSERT_TRUE(offset.has_value());
  EXPECT_EQ(offset->date(), QDate(2005, 3, 4));
  EXPECT_EQ(offset->time(), QTime(8, 1, 0, 800));

  EXPECT_FALSE(parseIsoDateTime(QStringLiteral("2005-03-04")));
}

TEST(ModlistMetadata, RejectsInvalidSentinelAndFarFutureDates)
{
  EXPECT_FALSE(parseIsoDateTime(QString{}));
  EXPECT_FALSE(parseIsoDateTime(QStringLiteral("not a date")));
  EXPECT_FALSE(parseIsoDateTime(QStringLiteral("2005-03-04T10:31:00")));
  EXPECT_FALSE(parseIsoDateTime(QStringLiteral("1970-01-01T00:00:00Z")));
  EXPECT_FALSE(parseIsoDateTime(QStringLiteral("1960-01-01T00:00:00Z")));
  EXPECT_FALSE(parseIsoDateTime(QDateTime::currentDateTimeUtc().addDays(3)
                                  .toString(Qt::ISODateWithMs)));
}

TEST(ModlistMetadata, UsesAuthoritativeGalleryDateAndCollectionAlias)
{
  const auto expected = parseIsoDateTime(QStringLiteral("2020-06-01T12:00:00Z"));
  ASSERT_TRUE(expected.has_value());
  EXPECT_EQ(sourceUpdated({ { "dateUpdated", "2020-06-01T12:00:00Z" },
                            { "date_updated", "2021-06-01T12:00:00Z" },
                            { "updatedAt", "2022-06-01T12:00:00Z" } }), expected);
  EXPECT_EQ(sourceUpdated({ { "date_updated", "2020-06-01T12:00:00Z" } }), expected);
  EXPECT_EQ(sourceUpdated({ { "updatedAt", "2020-06-01T12:00:00Z" } }), expected);
  EXPECT_FALSE(sourceUpdated({ { "dateUpdated", "1970-01-01T00:00:00Z" },
                               { "updatedAt", "2020-06-01T12:00:00Z" } }));
}

TEST(ModlistMetadata, FormatsLocalDateAndPreciseTimezoneTooltip)
{
  const auto date = parseIsoDateTime(QStringLiteral("2020-06-01T12:34:56.789Z"));
  ASSERT_TRUE(date.has_value());
  EXPECT_EQ(localDateLabel(*date),
            QLocale().toString(date->toLocalTime().date(), QLocale::ShortFormat));
  const auto tooltip = preciseLocalTooltip(*date);
  EXPECT_TRUE(tooltip.contains(QStringLiteral("2020-")));
  EXPECT_TRUE(tooltip.contains(QStringLiteral("UTC")));
}

TEST(ModlistMetadata, WritesAndReadsAtomicReceiptWithoutRetainingUrlCredentials)
{
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  Receipt receipt;
  receipt.sourceUrl = QStringLiteral("https://name:secret@example.org/list.wabbajack?token=private#section");
  receipt.title = QStringLiteral("Example List");
  receipt.author = QStringLiteral("Example Author");
  receipt.version = QStringLiteral("4.2.1");
  receipt.sourceUpdatedUtc = *parseIsoDateTime(QStringLiteral("2025-06-01T12:00:00.123456Z"));
  receipt.installedAtUtc = QDateTime::currentDateTimeUtc().addSecs(-30);

  QString error;
  ASSERT_TRUE(writeReceipt(directory.filePath("setup"), receipt, &error)) << error.toStdString();
  EXPECT_TRUE(error.isEmpty());
  QFile file(receiptPath(directory.filePath("setup")));
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  const auto saved = file.readAll();
  EXPECT_FALSE(saved.contains("secret"));
  EXPECT_FALSE(saved.contains("private"));

  const auto loaded = readReceipt(directory.filePath("setup"), &error);
  ASSERT_TRUE(loaded.has_value()) << error.toStdString();
  EXPECT_TRUE(error.isEmpty());
  EXPECT_EQ(loaded->sourceUrl, QStringLiteral("https://example.org/list.wabbajack"));
  EXPECT_EQ(loaded->title, receipt.title);
  EXPECT_EQ(loaded->author, receipt.author);
  EXPECT_EQ(loaded->version, receipt.version);
  EXPECT_EQ(loaded->sourceUpdatedUtc, receipt.sourceUpdatedUtc);
  EXPECT_EQ(loaded->installedAtUtc, receipt.installedAtUtc);
}

TEST(ModlistMetadata, SurfacesAtomicSaveErrorsWithoutReplacingExistingReceipt)
{
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const auto setup = directory.filePath("setup");
  Receipt receipt;
  receipt.sourceUrl = QStringLiteral("https://example.org/list.wabbajack");
  receipt.installedAtUtc = QDateTime::currentDateTimeUtc().addSecs(-30);
  QString error;
  ASSERT_TRUE(writeReceipt(setup, receipt, &error)) << error.toStdString();
  QFile existing(receiptPath(setup));
  ASSERT_TRUE(existing.open(QIODevice::ReadOnly));
  const QByteArray before = existing.readAll();
  existing.close();

  Receipt invalid = receipt;
  invalid.installedAtUtc = {};
  EXPECT_FALSE(writeReceipt(setup, invalid, &error));
  EXPECT_FALSE(error.isEmpty());
  QFile afterInvalid(receiptPath(setup));
  ASSERT_TRUE(afterInvalid.open(QIODevice::ReadOnly));
  EXPECT_EQ(afterInvalid.readAll(), before);

  const auto blockedSetup = directory.filePath("blocked");
  ASSERT_TRUE(QDir().mkpath(blockedSetup + "/fluorine-modlist.json"));
  EXPECT_FALSE(writeReceipt(blockedSetup, receipt, &error));
  EXPECT_FALSE(error.isEmpty());
  EXPECT_TRUE(QFileInfo::exists(blockedSetup + "/fluorine-modlist.json"));
}

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
