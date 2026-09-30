#include "resizableheaderview.h"

#include <QMouseEvent>
#include <QStyle>

ResizableHeaderView::ResizableHeaderView(QWidget* parent)
    : QHeaderView(Qt::Horizontal, parent)
{
  setMouseTracking(true);
  setToolTip(tr("Drag a column divider to resize. Double-click a divider to fit the column. Right-click a heading to choose columns."));
}

int ResizableHeaderView::lastVisibleSection() const
{
  for (int visual = count() - 1; visual >= 0; --visual) {
    const int section = logicalIndex(visual);
    if (!isSectionHidden(section)) return section;
  }
  return -1;
}

int ResizableHeaderView::handleSectionAt(int x) const
{
  const int margin = style()->pixelMetric(QStyle::PM_HeaderGripMargin, nullptr, this);
  const int last = lastVisibleSection();
  for (int visual = 0; visual < count(); ++visual) {
    const int section = logicalIndex(visual);
    if (isSectionHidden(section)) continue;
    const int edge = sectionViewportPosition(section) +
        (isRightToLeft() ? 0 : sectionSize(section) - 1);
    // The final divider can only be reached from inside the header when it
    // borders the scrollbar. Give it an eight-pixel grip on that side too.
    const int grip = section == last ? qMax(8, margin) : margin;
    if (qAbs(x - edge) <= grip) return section;
  }
  return -1;
}

bool ResizableHeaderView::canResize(int section) const
{
  if (section < 0) return false;
  return sectionResizeMode(section) == QHeaderView::Interactive ||
         sectionResizeMode(section) == QHeaderView::Stretch ||
         (stretchLastSection() && section == lastVisibleSection());
}

void ResizableHeaderView::enableManualResize(int section)
{
  const int width = sectionSize(section);
  if (stretchLastSection() && section == lastVisibleSection())
    setStretchLastSection(false);
  setSectionResizeMode(section, QHeaderView::Interactive);
  resizeSection(section, width);
}

void ResizableHeaderView::dragSection(QMouseEvent* event)
{
  if (m_DragSection >= count() || isSectionHidden(m_DragSection)) {
    m_DragSection = -1;
    return;
  }
  int distance = qRound(event->globalPosition().x() - m_DragStart);
  if (isRightToLeft()) distance = -distance;
  resizeSection(m_DragSection,
                qBound(minimumSectionSize(), m_DragWidth + distance,
                       maximumSectionSize()));
}

void ResizableHeaderView::mouseMoveEvent(QMouseEvent* event)
{
  if (m_DragSection >= 0) {
    dragSection(event);
    event->accept();
    return;
  }
  QHeaderView::mouseMoveEvent(event);
  if (event->buttons() == Qt::NoButton) {
    if (canResize(handleSectionAt(qRound(event->position().x()))))
      viewport()->setCursor(Qt::SplitHCursor);
    else
      viewport()->unsetCursor();
  }
}

void ResizableHeaderView::mousePressEvent(QMouseEvent* event)
{
  const int section = handleSectionAt(qRound(event->position().x()));
  if (event->button() == Qt::LeftButton && canResize(section)) {
    // Releasing last-section stretch can change the horizontal scroll offset.
    // Retain the section and pointer position instead of asking Qt to hit-test
    // the moved divider again after the geometry changes.
    m_DragSection = section;
    m_DragWidth = sectionSize(section);
    m_DragStart = event->globalPosition().x();
    enableManualResize(section);
    viewport()->setCursor(Qt::SplitHCursor);
    event->accept();
    return;
  }
  QHeaderView::mousePressEvent(event);
}

void ResizableHeaderView::mouseReleaseEvent(QMouseEvent* event)
{
  if (event->button() == Qt::LeftButton && m_DragSection >= 0) {
    dragSection(event);
    m_DragSection = -1;
    event->accept();
    return;
  }
  QHeaderView::mouseReleaseEvent(event);
}

void ResizableHeaderView::mouseDoubleClickEvent(QMouseEvent* event)
{
  const int section = handleSectionAt(qRound(event->position().x()));
  if (event->button() == Qt::LeftButton && canResize(section)) {
    m_DragSection = -1;
    enableManualResize(section);
    emit sectionHandleDoubleClicked(section);
    event->accept();
    return;
  }
  QHeaderView::mouseDoubleClickEvent(event);
}
