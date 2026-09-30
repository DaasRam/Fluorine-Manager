#include "installprogressview.h"

#include <QApplication>
#include <QHeaderView>
#include <QFontMetrics>
#include <QFont>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QImage>
#include <QLabel>
#include <QPersistentModelIndex>
#include <QProgressBar>
#include <QSignalSpy>
#include <QTableView>
#include <QTabWidget>
#include <QTest>
#include <gtest/gtest.h>

#include <algorithm>

namespace
{
void start(InstallProgressView& view, const QString& id,
           const QString& stage = QStringLiteral("Downloading"),
           qint64 total = 100, const QString& unit = QStringLiteral("bytes"))
{
  view.startItem(id, id + QStringLiteral(".zip"), id, QStringLiteral("Game files"),
                 stage, total, unit);
}

TEST(InstallProgressView, PhaseChangeClearsPriorTotalsAndUnknownPhaseIsIndeterminate)
{
  InstallProgressView view;
  auto* phase = view.findChild<QLabel*>(QStringLiteral("installPhaseLabel"));
  auto* progress = view.findChild<QProgressBar*>(QStringLiteral("installPhaseProgress"));
  ASSERT_NE(phase, nullptr);
  ASSERT_NE(progress, nullptr);

  view.setPhase(QStringLiteral("Downloading archives"));
  view.setProgress(4, 10, QStringLiteral("archives"));
  QTRY_COMPARE_WITH_TIMEOUT(progress->format(), QStringLiteral("4 / 10 archives"), 1500);
  EXPECT_EQ(progress->maximum(), 100);
  EXPECT_EQ(progress->value(), 40);
  EXPECT_EQ(progress->format(), QStringLiteral("4 / 10 archives"));

  view.setPhase(QStringLiteral("Applying directives"));
  EXPECT_EQ(phase->text(), QStringLiteral("Applying directives"));
  EXPECT_EQ(progress->maximum(), 0);
  EXPECT_FALSE(progress->format().contains(QStringLiteral("4 / 10")));
  view.setProgress(3, 0, QStringLiteral("directives"));
  QTRY_COMPARE_WITH_TIMEOUT(progress->format(), QStringLiteral("3 operations"), 1500);
  EXPECT_EQ(progress->maximum(), 0);
}

TEST(InstallProgressView, EndedPhaseKeepsItsLabelWithoutAnIndeterminateAnimation)
{
  InstallProgressView view;
  auto* phase = view.findChild<QLabel*>(QStringLiteral("installPhaseLabel"));
  auto* progress = view.findChild<QProgressBar*>(QStringLiteral("installPhaseProgress"));
  ASSERT_NE(phase, nullptr);
  ASSERT_NE(progress, nullptr);

  view.setPhase(QStringLiteral("Downloading"));
  view.setProgress(4, 0, QStringLiteral("archives"));
  QTRY_VERIFY_WITH_TIMEOUT(progress->maximum() == 0, 1500);
  view.stopActive(QStringLiteral("Install stopped"));
  view.setPhaseEnded(QStringLiteral("Stopped — ready to resume"));

  EXPECT_EQ(phase->text(), QStringLiteral("Stopped — ready to resume"));
  EXPECT_TRUE(progress->isHidden());
  EXPECT_EQ(progress->maximum(), 1);
  EXPECT_EQ(view.activeCount(), 0);
  view.setProgress(9, 10, QStringLiteral("archives"));
  QTest::qWait(120);
  EXPECT_TRUE(progress->isHidden());
}

TEST(InstallProgressView, FailuresAndStopsLeaveTheActiveCount)
{
  InstallProgressView view;
  start(view, QStringLiteral("one"));
  start(view, QStringLiteral("two"), QStringLiteral("Extracting"), 10,
        QStringLiteral("files"));
  ASSERT_EQ(view.activeCount(), 2);

  view.failItem(QStringLiteral("one"), QStringLiteral("archive checksum mismatch"));
  EXPECT_EQ(view.activeCount(), 1);
  EXPECT_EQ(view.counts().failed, 1);
  EXPECT_EQ(view.needsAttentionCount(), 1);
  EXPECT_EQ(view.activeView()->model()->rowCount(), 1);
  EXPECT_EQ(view.attentionView()->model()->rowCount(), 1);
  const int failedRow = view.model()->rowForId(QStringLiteral("one"));
  ASSERT_GE(failedRow, 0);
  EXPECT_EQ(view.model()->index(failedRow, InstallQueueModel::StageColumn)
                .data().toString(),
            QStringLiteral("Failed"));
  EXPECT_EQ(view.model()->index(failedRow, InstallQueueModel::ProgressColumn)
                .data().toString(),
            QStringLiteral("archive checksum mismatch"));
  EXPECT_TRUE(view.model()->index(failedRow, InstallQueueModel::NameColumn)
                  .data(Qt::ToolTipRole).toString().contains(QStringLiteral("one.zip")));

  view.stopActive(QStringLiteral("Install cancelled"));
  EXPECT_EQ(view.activeCount(), 0);
  EXPECT_EQ(view.counts().failed, 1);
  EXPECT_EQ(view.counts().stopped, 1);
  EXPECT_EQ(view.needsAttentionCount(), 2);
  const int stoppedRow = view.model()->rowForId(QStringLiteral("two"));
  ASSERT_GE(stoppedRow, 0);
  EXPECT_EQ(view.model()->statusAt(stoppedRow), InstallQueueModel::Status::Stopped);
}

TEST(InstallProgressView, CompletedHistoryIsBoundedWithoutLosingAggregateCounts)
{
  InstallProgressView view;
  constexpr int completed = InstallQueueModel::MaxCompletedHistory + 7;
  for (int i = 0; i < completed; ++i) {
    const auto id = QStringLiteral("archive-%1").arg(i);
    start(view, id, QStringLiteral("Downloading"), 1024, QStringLiteral("bytes"));
    view.progressItem(id, 1024, 1024, 0.0, QStringLiteral("bytes"));
    view.completeItem(id);
  }

  const auto counts = view.counts();
  EXPECT_EQ(counts.active, 0);
  EXPECT_EQ(counts.completedInHistory, InstallQueueModel::MaxCompletedHistory);
  EXPECT_EQ(counts.completedTotal, completed);
  EXPECT_EQ(counts.completedBytes, static_cast<qint64>(completed) * 1024);
  EXPECT_EQ(view.completedView()->model()->rowCount(),
            InstallQueueModel::MaxCompletedHistory);
  EXPECT_EQ(view.model()->rowForId(QStringLiteral("archive-0")), -1);
  EXPECT_GE(view.model()->rowForId(QStringLiteral("archive-7")), 0);
  EXPECT_GE(view.model()->rowForId(QStringLiteral("archive-1006")), 0);
}

TEST(InstallProgressView, ProgressBurstsCoalesceAndKeepSelectionAndResumeValues)
{
  InstallProgressView view;
  view.resize(900, 420);
  start(view, QStringLiteral("resumed"), QStringLiteral("Downloading"), 1000,
        QStringLiteral("bytes"));
  start(view, QStringLiteral("other"), QStringLiteral("Extracting"), 20,
        QStringLiteral("files"));
  view.show();
  QApplication::processEvents();
  for (auto* table : {view.activeView(), view.attentionView(), view.completedView()}) {
    view.tabs()->setCurrentWidget(table);
    QApplication::processEvents();
    auto* header = table->horizontalHeader();
    EXPECT_GT(header->sectionSize(InstallQueueModel::NameColumn), 250);
    EXPECT_GT(header->sectionSize(InstallQueueModel::ProgressColumn), 250);
    EXPECT_GE(header->length(), table->viewport()->width() - 2);
  }
  view.tabs()->setCurrentWidget(view.activeView());

  const QModelIndex selected = view.activeView()->model()->index(0, InstallQueueModel::NameColumn);
  ASSERT_TRUE(selected.isValid());
  view.activeView()->selectionModel()->setCurrentIndex(
      selected, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  const QPersistentModelIndex persistentSelection(selected);

  QSignalSpy changed(view.model(), &QAbstractItemModel::dataChanged);
  ASSERT_TRUE(changed.isValid());
  for (qint64 value = 1; value <= 500; ++value) {
    view.progressItem(QStringLiteral("resumed"), value, 1000,
                      2048.0, QStringLiteral("bytes"));
  }
  QTRY_COMPARE_WITH_TIMEOUT(changed.count(), 1, 1500);
  EXPECT_EQ(view.model()->rowCount(), 2);
  EXPECT_EQ(view.model()->index(view.model()->rowForId(QStringLiteral("resumed")),
                                InstallQueueModel::ProgressColumn)
                .data(InstallQueueModel::CompletedRole).toLongLong(),
            500);
  EXPECT_TRUE(persistentSelection.isValid());
  EXPECT_EQ(persistentSelection.data(InstallQueueModel::IdRole).toString(),
            QStringLiteral("resumed"));
  EXPECT_EQ(view.model()->index(view.model()->rowForId(QStringLiteral("resumed")),
                                InstallQueueModel::SpeedColumn)
                .data().toString(),
            QStringLiteral("2.0 KiB/s"));

  view.progressItem(QStringLiteral("other"), 3, 20, 99.0, QStringLiteral("files"));
  QTRY_VERIFY_WITH_TIMEOUT(
      view.model()->index(view.model()->rowForId(QStringLiteral("other")),
                          InstallQueueModel::SpeedColumn).data().toString().isEmpty(),
      1500);
}

TEST(InstallProgressView, UnknownItemSizesStayIndeterminateAndDoNotLoseSelection)
{
  InstallProgressView view;
  view.resize(800, 380);
  start(view, QStringLiteral("unknown"), QStringLiteral("Extracting"), 0,
        QStringLiteral("bytes"));
  view.show();
  QApplication::processEvents();

  const int row = view.model()->rowForId(QStringLiteral("unknown"));
  ASSERT_GE(row, 0);
  EXPECT_TRUE(view.model()->index(row, InstallQueueModel::ProgressColumn)
                  .data(InstallQueueModel::IndeterminateRole).toBool());
  EXPECT_EQ(view.model()->index(row, InstallQueueModel::ProgressColumn)
                .data().toString(),
            QStringLiteral("0 B"));

  auto* activeModel = view.activeView()->model();
  const QModelIndex selected = activeModel->index(0, InstallQueueModel::NameColumn);
  view.activeView()->selectionModel()->setCurrentIndex(
      selected, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
  QPersistentModelIndex persistentSelection(selected);
  view.progressItem(QStringLiteral("unknown"), 14, 0, 500.0, QStringLiteral("bytes"));
  QTRY_VERIFY_WITH_TIMEOUT(
      view.model()->index(row, InstallQueueModel::ProgressColumn)
              .data(InstallQueueModel::CompletedRole).toLongLong() == 14,
      1500);
  EXPECT_TRUE(view.model()->index(row, InstallQueueModel::ProgressColumn)
                  .data(InstallQueueModel::IndeterminateRole).toBool());
  EXPECT_TRUE(persistentSelection.isValid());
}

TEST(InstallProgressView, WaitingRequestsCanBeActivatedAndNeverCountAsCompletedFiles)
{
  InstallProgressView view;
  QSignalSpy activated(&view, &InstallProgressView::attentionActivated);
  ASSERT_TRUE(activated.isValid());
  view.setWaitingRequest(QStringLiteral("manual-17"), QStringLiteral("Archive 17"),
                         QStringLiteral("Choose a local archive to continue"));

  EXPECT_EQ(view.activeCount(), 0);
  EXPECT_EQ(view.waitingRequestCount(), 1);
  EXPECT_EQ(view.needsAttentionCount(), 1);
  EXPECT_EQ(view.completedCount(), 0);
  EXPECT_EQ(view.model()->counts().completedBytes, 0);
  EXPECT_EQ(view.attentionView()->model()->rowCount(), 1);
  EXPECT_EQ(view.tabs()->currentWidget(), view.attentionView());

  const QModelIndex waiting = view.attentionView()->model()->index(0, 0);
  ASSERT_TRUE(waiting.isValid());
  view.attentionView()->activated(waiting);
  ASSERT_EQ(activated.count(), 1);
  EXPECT_EQ(activated.front().front().toString(), QStringLiteral("manual-17"));

  view.finishWaitingRequest(QStringLiteral("manual-17"));
  EXPECT_EQ(view.waitingRequestCount(), 0);
  EXPECT_EQ(view.needsAttentionCount(), 0);
  EXPECT_EQ(view.completedCount(), 0);
  EXPECT_EQ(view.model()->rowCount(), 0);

  start(view, QStringLiteral("real-archive"));
  view.completeItem(QStringLiteral("real-archive"));
  const auto completedBeforeResolve = view.completedCount();
  view.setWaitingRequest(QStringLiteral("manual-18"), QStringLiteral("Another archive"),
                         QStringLiteral("Select a file"));
  view.finishWaitingRequest(QStringLiteral("manual-18"));
  EXPECT_EQ(view.completedCount(), completedBeforeResolve);
  EXPECT_EQ(view.model()->rowCount(), 1);
}

TEST(InstallProgressView, SyntheticQueueSeparatesAllThreeViewsAndRenders)
{
  InstallProgressView view;
  view.resize(920, 520);
  for (int i = 0; i < 24; ++i)
    start(view, QStringLiteral("active-%1").arg(i), QStringLiteral("Downloading"), 100,
          QStringLiteral("archives"));
  for (int i = 0; i < 12; ++i) {
    const auto id = QStringLiteral("failed-%1").arg(i);
    start(view, id);
    view.failItem(id, QStringLiteral("Synthetic extraction error for item %1").arg(i));
  }
  for (int i = 0; i < 7; ++i) {
    const auto id = QStringLiteral("complete-%1").arg(i);
    start(view, id);
    view.completeItem(id);
  }
  for (int i = 0; i < 4; ++i)
    view.setWaitingRequest(QStringLiteral("request-%1").arg(i),
                           QStringLiteral("Needs file %1").arg(i),
                           QStringLiteral("Choose a local file"));

  EXPECT_EQ(view.activeView()->model()->rowCount(), 24);
  EXPECT_EQ(view.attentionView()->model()->rowCount(), 16);
  EXPECT_EQ(view.completedView()->model()->rowCount(), 7);
  EXPECT_EQ(view.activeCount(), 24);
  EXPECT_EQ(view.counts().failed, 12);
  EXPECT_EQ(view.waitingRequestCount(), 4);
  EXPECT_EQ(view.completedCount(), 7);

  view.show();
  QApplication::processEvents();
  QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  view.render(&image);
  EXPECT_FALSE(image.isNull());
  EXPECT_EQ(view.findChildren<QProgressBar*>().size(), 1);
}

TEST(InstallProgressView, LargeFontsExpandQueueRowsAndStillRender)
{
  InstallProgressView view;
  view.resize(1024, 600);
  start(view, QStringLiteral("large-font-row"), QStringLiteral("Downloading"), 100,
        QStringLiteral("archives"));
  const int compactHeight =
      view.activeView()->verticalHeader()->defaultSectionSize();

  QFont largeFont = view.font();
  largeFont.setPixelSize(std::max(40, QFontMetrics(largeFont).height() * 2));
  view.setFont(largeFont);
  QApplication::processEvents();

  const int expectedHeight = std::max(
      34, QFontMetrics(view.activeView()->font()).height() + 12);
  EXPECT_GT(expectedHeight, compactHeight);
  for (auto* queueView : {view.activeView(), view.attentionView(), view.completedView()})
    EXPECT_EQ(queueView->verticalHeader()->defaultSectionSize(), expectedHeight);

  view.show();
  QApplication::processEvents();
  const auto index = view.activeView()->model()->index(0, InstallQueueModel::NameColumn);
  ASSERT_TRUE(index.isValid());
  EXPECT_EQ(view.activeView()->visualRect(index).height(), expectedHeight);

  QImage image(view.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  view.render(&image);
  EXPECT_FALSE(image.isNull());
}
} // namespace

int main(int argc, char** argv)
{
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
