#include "downloadprogressrenderer.h"

#include <QImage>
#include <QPainter>
#include <gtest/gtest.h>

namespace
{

class DownloadProgressRendererTest : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    if (qApp == nullptr) {
      static int argc = 1;
      static char name[] = "test_downloadprogressrenderer";
      static char* argv[] = {name, nullptr};
      static QApplication application(argc, argv);
      Q_UNUSED(application);
    }

    // QWidget::render can use LCD subpixel text while painting directly to an
    // image uses grayscale text. Match the font strategy so pixel comparisons
    // test the renderer's layout and styling across fontconfig environments.
    auto font = qApp->font();
    font.setStyleStrategy(QFont::NoSubpixelAntialias);
    qApp->setFont(font);
    qApp->setStyleSheet(
        "QProgressBar { background-color: #e6e6e6; color: #000; "
        "border: 1px solid #bcbcbc; text-align: center; }"
        "QProgressBar[downloadView=compact] { background-color: #e000e0; }"
        "QProgressBar[downloadView=standard] { background-color: #e6e6e6; }"
        "QProgressBar::chunk { background: #06b025; }");
  }

  void compareWithWidgetPaint(Qt::LayoutDirection direction)
  {
    constexpr QSize size(240, 28);
    constexpr int value = 63;
    const QString format = QStringLiteral("63% - 12.3 MB/s - ~2 min");

    DownloadProgressRenderer::StyleContext reference;
    reference.setRange(0, 100);
    reference.setValue(value);
    reference.setFormat(format);
    reference.setTextVisible(true);
    reference.setAlignment(Qt::AlignCenter);
    reference.setLayoutDirection(direction);
    reference.setProperty("downloadProgress", true);
    reference.setProperty("downloadView", "standard");
    reference.setStyle(QApplication::style());
    reference.resize(size);

    QImage expected(size, QImage::Format_ARGB32_Premultiplied);
    expected.fill(Qt::transparent);
    reference.render(&expected);

    QImage actual(size, QImage::Format_ARGB32_Premultiplied);
    actual.fill(Qt::transparent);
    QPainter painter(&actual);
    DownloadProgressRenderer::StyleContext styleContext;
    styleContext.setRange(0, 100);
    styleContext.setTextVisible(true);
    styleContext.setAlignment(Qt::AlignCenter);
    styleContext.setLayoutDirection(direction);
    styleContext.setProperty("downloadProgress", true);
    styleContext.setProperty("downloadView", "standard");
    styleContext.setStyle(QApplication::style());
    styleContext.ensurePolished();
    QStyleOptionViewItem itemOption;
    itemOption.rect = QRect(QPoint(0, 0), size);
    itemOption.direction = direction;
    itemOption.palette = reference.palette();
    DownloadProgressRenderer::draw(&painter, itemOption, &styleContext, value, format);
    painter.end();

  EXPECT_EQ(actual, expected);
  }

  void repolishAfterDownloadViewChange(QProgressBar* bar, const QString& view)
  {
    bar->setProperty("downloadView", view);
    QStyle* const style = bar->style();
    style->unpolish(bar);
    style->polish(bar);
  }
};

TEST_F(DownloadProgressRendererTest, MatchesStyledWidgetInLeftToRightLayout)
{
  compareWithWidgetPaint(Qt::LeftToRight);
}

TEST_F(DownloadProgressRendererTest, MatchesStyledWidgetInRightToLeftLayout)
{
  compareWithWidgetPaint(Qt::RightToLeft);
}

TEST_F(DownloadProgressRendererTest, ReappliesStylesWhenViewModeChanges)
{
  DownloadProgressRenderer::StyleContext context;
  context.setRange(0, 100);
  context.setTextVisible(true);
  context.setAlignment(Qt::AlignCenter);
  context.setProperty("downloadProgress", true);
  context.setStyle(QApplication::style());
  context.ensurePolished();

  QStyleOptionViewItem itemOption;
  itemOption.rect = QRect(0, 0, 240, 28);
  itemOption.direction = Qt::LeftToRight;
  const QString format = QStringLiteral("63% - 12.3 MB/s - ~2 min");
  auto render = [&](const QString& view) {
    repolishAfterDownloadViewChange(&context, view);
    QImage image(itemOption.rect.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    DownloadProgressRenderer::draw(&painter, itemOption, &context, 63, format);
    painter.end();
    return image;
  };

  const QImage standard = render(QStringLiteral("standard"));
  const QImage compact = render(QStringLiteral("compact"));
  EXPECT_NE(standard.pixelColor(230, 14), compact.pixelColor(230, 14));
}

}  // namespace
