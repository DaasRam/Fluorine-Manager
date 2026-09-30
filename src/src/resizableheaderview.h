#pragma once

#include <QHeaderView>

// Keep automatic column sizing until the user drags a divider. QHeaderView's
// Stretch mode normally disables that divider, including double-click to fit.
class ResizableHeaderView final : public QHeaderView
{
public:
  explicit ResizableHeaderView(QWidget* parent = nullptr);

protected:
  void mouseMoveEvent(QMouseEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
  int lastVisibleSection() const;
  int handleSectionAt(int x) const;
  bool canResize(int section) const;
  void enableManualResize(int section);
  void dragSection(QMouseEvent* event);

  int m_DragSection = -1;
  int m_DragWidth = 0;
  qreal m_DragStart = 0;
};
