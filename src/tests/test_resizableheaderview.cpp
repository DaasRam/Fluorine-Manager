#include "fluorinetheme.h"
#include "resizableheaderview.h"

#include <QApplication>
#include <QDir>
#include <QSignalSpy>
#include <QScrollBar>
#include <QTest>
#include <QTreeWidget>
#include <gtest/gtest.h>

namespace
{
class ResizableHeaderTest : public ::testing::Test
{
protected:
  QTreeWidget tree;
  ResizableHeaderView* header = nullptr;

  void SetUp() override
  {
    header = new ResizableHeaderView(&tree);
    tree.setHeader(header);
    tree.setRootIsDecorated(false);
    tree.setHeaderLabels({"Mod Name", "Conflicts", "Priority"});
    new QTreeWidgetItem(&tree, {"Weather and Lighting", "", "4"});
    new QTreeWidgetItem(&tree, {"DLC: Contraptions Workshop", "", "5"});
    tree.resize(720, 220);
    header->setSectionsClickable(true);
    header->setSectionsMovable(true);
    header->setStretchLastSection(false);
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->resizeSection(1, 120);
    header->resizeSection(2, 120);
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    tree.show();
    QApplication::processEvents();
  }

  QPoint edge(int section) const
  {
    return {header->sectionViewportPosition(section) +
                (header->isRightToLeft() ? 0 : header->sectionSize(section) - 1),
            header->height() / 2};
  }

  void drag(int section, int distance)
  {
    const QPoint from = edge(section);
    QTest::mouseMove(header->viewport(), from);
    QTest::mousePress(header->viewport(), Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(header->viewport(), from + QPoint(distance, 0));
    QTest::mouseRelease(header->viewport(), Qt::LeftButton, Qt::NoModifier,
                        from + QPoint(distance, 0));
    QApplication::processEvents();
  }
};

TEST_F(ResizableHeaderTest, StretchColumnCanBeDraggedAndSavedAsAManualWidth)
{
  const int before = header->sectionSize(0);
  const int other = header->sectionSize(1);
  QTest::mouseMove(header->viewport(), edge(0));
  EXPECT_EQ(header->viewport()->cursor().shape(), Qt::SplitHCursor);
  drag(0, -80);
  EXPECT_EQ(header->sectionResizeMode(0), QHeaderView::Interactive);
  EXPECT_LT(header->sectionSize(0), before - 60);
  EXPECT_EQ(header->sectionSize(1), other);
  const int manual = header->sectionSize(0);
  const auto saved = header->saveState();
  header->setSectionResizeMode(0, QHeaderView::Stretch);
  ASSERT_TRUE(header->restoreState(saved));
  EXPECT_EQ(header->sectionResizeMode(0), QHeaderView::Interactive);
  EXPECT_EQ(header->sectionSize(0), manual);
}

TEST_F(ResizableHeaderTest, HeadingClicksKeepAutomaticSizingAndClickSignals)
{
  QSignalSpy clicked(header, &QHeaderView::sectionClicked);
  QTest::mouseClick(header->viewport(), Qt::LeftButton, Qt::NoModifier,
                    QPoint(header->sectionSize(0) / 2, header->height() / 2));
  EXPECT_EQ(header->sectionResizeMode(0), QHeaderView::Stretch);
  ASSERT_EQ(clicked.count(), 1);
  EXPECT_EQ(clicked.front().front().toInt(), 0);
  const int before = header->sectionSize(0);
  tree.resize(tree.width() + 100, tree.height());
  QApplication::processEvents();
  EXPECT_GT(header->sectionSize(0), before);
}

TEST_F(ResizableHeaderTest, DoubleClickFitsTheFormerlyStretchedColumn)
{
  const int before = header->sectionSize(0);
  QSignalSpy fitted(header, &QHeaderView::sectionHandleDoubleClicked);
  QTest::mouseDClick(header->viewport(), Qt::LeftButton, Qt::NoModifier, edge(0));
  QApplication::processEvents();
  EXPECT_EQ(header->sectionResizeMode(0), QHeaderView::Interactive);
  ASSERT_EQ(fitted.count(), 1);
  EXPECT_EQ(fitted.front().front().toInt(), 0);
  EXPECT_LT(header->sectionSize(0), before);
}

TEST_F(ResizableHeaderTest, ReorderedAndHiddenSectionsKeepTheirCorrectHandles)
{
  header->hideSection(1);
  header->moveSection(header->visualIndex(2), 0);
  const int before = header->sectionSize(0);
  drag(0, -60);
  EXPECT_EQ(header->sectionResizeMode(0), QHeaderView::Interactive);
  EXPECT_LT(header->sectionSize(0), before - 40);
  EXPECT_TRUE(header->isSectionHidden(1));
  EXPECT_EQ(header->logicalIndex(0), 2);
}

TEST_F(ResizableHeaderTest, RightToLeftHandlesResizeTheCorrectColumn)
{
  tree.setLayoutDirection(Qt::RightToLeft);
  QApplication::processEvents();
  const int before = header->sectionSize(0);
  drag(0, 70);
  EXPECT_EQ(header->sectionResizeMode(0), QHeaderView::Interactive);
  EXPECT_LT(header->sectionSize(0), before - 50);
}

TEST_F(ResizableHeaderTest, ImplicitLastSectionStretchCanBeReleased)
{
  header->setSectionResizeMode(0, QHeaderView::Interactive);
  header->resizeSection(0, 300);
  header->setStretchLastSection(true);
  QApplication::processEvents();
  const int before = header->sectionSize(2);
  drag(2, -60);
  EXPECT_FALSE(header->stretchLastSection());
  EXPECT_LT(header->sectionSize(2), before - 40);
}

TEST_F(ResizableHeaderTest, LastVisibleSectionCanShrinkAfterHorizontalScrolling)
{
  tree.setHeaderLabels({"Mod Name", "Conflicts", "Priority", "Notes", "Game"});
  header->hideSection(3);
  header->hideSection(4);
  tree.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  FluorineTheme::apply(&tree);
  for (const auto scrollMode : {QAbstractItemView::ScrollPerItem,
                                QAbstractItemView::ScrollPerPixel}) {
    SCOPED_TRACE(static_cast<int>(scrollMode));
    tree.setHorizontalScrollMode(scrollMode);
    header->setStretchLastSection(false);
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->resizeSection(0, 900);
    header->resizeSection(1, 120);
    header->resizeSection(2, 240);
    header->setStretchLastSection(true);
    QApplication::processEvents();
    ASSERT_GT(tree.horizontalScrollBar()->maximum(), 0);
    tree.horizontalScrollBar()->setValue(tree.horizontalScrollBar()->maximum());
    QApplication::processEvents();
    const int before = header->sectionSize(2);
    const int name = header->sectionSize(0);
    const QPoint from(qMin(edge(2).x(), header->viewport()->rect().right() - 1),
                      header->height() / 2);
    ASSERT_TRUE(header->viewport()->rect().contains(from));
    QTest::mouseMove(header->viewport(), from);
    QTest::mousePress(header->viewport(), Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseMove(header->viewport(), from - QPoint(70, 0));
    QTest::mouseRelease(header->viewport(), Qt::LeftButton, Qt::NoModifier,
                        from - QPoint(70, 0));
    QApplication::processEvents();
    EXPECT_LT(header->sectionSize(2), before - 50);
    EXPECT_EQ(header->sectionSize(0), name);
    EXPECT_FALSE(header->stretchLastSection());
  }
}

TEST_F(ResizableHeaderTest, FinalDividerCanBeGrabbedInsideTheVisibleHeader)
{
  FluorineTheme::apply(&tree);
  header->setSectionResizeMode(0, QHeaderView::Interactive);
  header->resizeSection(0, 300);
  header->setStretchLastSection(true);
  QApplication::processEvents();
  const int before = header->sectionSize(2);
  // The final divider has no neighbouring section on its other side. The
  // pointer must be able to grab it from inside the header, beside the scrollbar.
  const QPoint from(header->viewport()->rect().right() - 5, header->height() / 2);
  QTest::mouseMove(header->viewport(), from);
  EXPECT_EQ(header->viewport()->cursor().shape(), Qt::SplitHCursor);
  QTest::mousePress(header->viewport(), Qt::LeftButton, Qt::NoModifier, from);
  QTest::mouseMove(header->viewport(), from - QPoint(70, 0));
  QTest::mouseRelease(header->viewport(), Qt::LeftButton, Qt::NoModifier,
                      from - QPoint(70, 0));
  QApplication::processEvents();
  EXPECT_LT(header->sectionSize(2), before - 50);
}

TEST_F(ResizableHeaderTest, FinalDividerRespectsMinimumAndStopsResizingOnRelease)
{
  header->setSectionResizeMode(0, QHeaderView::Interactive);
  header->resizeSection(0, 300);
  header->setStretchLastSection(true);
  QApplication::processEvents();
  drag(2, -1000);
  EXPECT_EQ(header->sectionSize(2), header->minimumSectionSize());
  EXPECT_FALSE(header->stretchLastSection());
  const auto saved = header->saveState();
  QTest::mouseMove(header->viewport(), QPoint(100, header->height() / 2));
  EXPECT_EQ(header->sectionSize(2), header->minimumSectionSize());
  header->setStretchLastSection(true);
  ASSERT_TRUE(header->restoreState(saved));
  EXPECT_FALSE(header->stretchLastSection());
  EXPECT_EQ(header->sectionSize(2), header->minimumSectionSize());
}

TEST_F(ResizableHeaderTest, FinalDividerCanBeGrabbedFromInsideInRightToLeftLayout)
{
  tree.setLayoutDirection(Qt::RightToLeft);
  header->setSectionResizeMode(0, QHeaderView::Interactive);
  header->resizeSection(0, 300);
  header->setStretchLastSection(true);
  QApplication::processEvents();
  const int before = header->sectionSize(2);
  const auto from = edge(2) + QPoint(5, 0);
  ASSERT_TRUE(header->viewport()->rect().contains(from));
  QTest::mousePress(header->viewport(), Qt::LeftButton, Qt::NoModifier, from);
  QTest::mouseMove(header->viewport(), from + QPoint(70, 0));
  QTest::mouseRelease(header->viewport(), Qt::LeftButton, Qt::NoModifier,
                      from + QPoint(70, 0));
  EXPECT_LT(header->sectionSize(2), before - 50);
}

TEST_F(ResizableHeaderTest, DoubleClickFitsFinalColumnFromItsInsetGrip)
{
  header->setSectionResizeMode(0, QHeaderView::Interactive);
  header->resizeSection(0, 300);
  header->setStretchLastSection(true);
  QApplication::processEvents();
  const int before = header->sectionSize(2);
  QSignalSpy fitted(header, &QHeaderView::sectionHandleDoubleClicked);
  QTest::mouseDClick(header->viewport(), Qt::LeftButton, Qt::NoModifier,
                     edge(2) - QPoint(5, 0));
  QApplication::processEvents();
  ASSERT_EQ(fitted.count(), 1);
  EXPECT_EQ(fitted.front().front().toInt(), 2);
  EXPECT_FALSE(header->stretchLastSection());
  EXPECT_LT(header->sectionSize(2), before);
}

TEST_F(ResizableHeaderTest, LightAndDarkHeadersHaveVisibleDividerLines)
{
  const auto original = QApplication::palette();
  const QString output = qEnvironmentVariable("FLUORINE_HEADER_CAPTURE_DIR");
  for (const bool dark : {false, true}) {
    QPalette palette = original;
    palette.setColor(QPalette::Base, dark ? QColor("#141719") : QColor("#ffffff"));
    palette.setColor(QPalette::Text, dark ? QColor("#e0e0e0") : QColor("#202020"));
    QApplication::setPalette(palette);
    FluorineTheme::apply(&tree);
    QApplication::processEvents();
    const auto picture = header->viewport()->grab().toImage();
    const int x = edge(0).x();
    const int y = header->height() / 2;
    ASSERT_LT(x, picture.width());
    ASSERT_LT(y, picture.height());
    // Away from label glyphs, the column boundary must differ from its fill.
    const auto line = picture.pixelColor(x, y);
    const auto fill = picture.pixelColor(x - 5, y);
    const int contrast = qAbs(line.red() - fill.red()) +
        qAbs(line.green() - fill.green()) + qAbs(line.blue() - fill.blue());
    EXPECT_GT(contrast, 90);
    if (!output.isEmpty()) {
      ASSERT_TRUE(QDir().mkpath(output));
      EXPECT_TRUE(tree.grab().save(output +
          (dark ? "/dark-column-fixture.png" : "/light-column-fixture.png")));
    }
  }
  QApplication::setPalette(original);
}
}

int main(int argc, char** argv)
{
  QApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
