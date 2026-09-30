#include "fluorinetheme.h"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QWidget>

namespace
{
QColor mix(const QColor& a, const QColor& b, int percent)
{
  return QColor((a.red() * (100 - percent) + b.red() * percent) / 100,
                (a.green() * (100 - percent) + b.green() * percent) / 100,
                (a.blue() * (100 - percent) + b.blue() * percent) / 100);
}
}

void FluorineTheme::apply(QWidget* surface, bool compact)
{
  if (!surface) return;
  // Sample a fresh styled widget rather than a palette that our previous
  // application of this sheet has already modified.
  QWidget sample;
  sample.ensurePolished();
  const auto p = sample.palette();
  const auto base = p.color(QPalette::Base);
  const auto text = p.color(QPalette::Text);
  const auto window = mix(base, text, 3);
  const auto raised = mix(base, text, 6);
  const auto border = mix(base, text, 17);
  const auto divider = mix(base, text, 45);
  const auto muted = mix(base, text, 68);
  const auto accent = p.color(QPalette::Highlight);
  const auto selectedText = p.color(QPalette::HighlightedText);
  const auto hover = mix(base, accent, 15);

  surface->setAutoFillBackground(true);
  surface->setStyleSheet(QStringLiteral(R"(
    QWidget { color: %1; }
    QWidget#fluorineWorkspaceFrame, QWidget#fluorineLibraryView,
    QWidget#workspaceContent, QDialog#fluorineInstaller,
    QDialog#ProfilesDialog, QDialog#EditExecutablesDialog,
    QDialog#ProfileInputDialog { background: %2; }
    QWidget#workspaceHeader { background: %2; border-bottom: 1px solid %4; }
    QWidget#workspaceNavigation { background: %2; border-bottom: 1px solid %4; }
    QWidget#modWorkspaceTools { background: %2; }
    QToolBar { background: %2; border: none; border-bottom: 1px solid %4; spacing: 5px; padding: 3px; }
    QWidget#selectedModPanel { background: %3; border-left: 1px solid %4; }
    QLabel { background: transparent; border: none; }
    QLabel[secondary="true"] { color: %5; }
    QPushButton, QToolButton {
      background: %3; border: 1px solid %4; border-radius: 5px;
      padding: 6px 10px; min-height: 18px;
    }
    QPushButton:hover, QToolButton:hover { background: %7; border-color: %6; }
    QPushButton:pressed, QToolButton:pressed { background: %7; }
    QPushButton:focus, QToolButton:focus, QComboBox:focus, QLineEdit:focus {
      border: 1px solid %6;
    }
    QPushButton:disabled, QToolButton:disabled { color: %5; border-color: %4; }
    QPushButton#startButton, QPushButton[primary="true"] {
      background: %6; color: %8; border-color: %6; font-weight: 600;
    }
    QToolButton[workspaceDestination="true"] {
      background: transparent; border: none; border-radius: 0;
      border-bottom: 2px solid transparent; padding: 8px 14px;
    }
    QToolButton[workspaceDestination="true"]:checked {
      color: %1; background: %7; border-bottom: 2px solid %6;
    }
    QToolButton[workspaceDestination="true"]:hover { background: %3; }
    QTabBar#workspaceDestinations::tab, QTabBar#workspaceTaskTabs::tab {
      color: %5; background: transparent; border: none; border-radius: 0;
      border-bottom: 2px solid transparent; padding: 10px 16px;
    }
    QTabBar#workspaceDestinations::tab:selected, QTabBar#workspaceTaskTabs::tab:selected {
      color: %1; background: %7; border-bottom: 2px solid %6;
    }
    QTabBar#workspaceDestinations::tab:hover, QTabBar#workspaceTaskTabs::tab:hover { background: %3; }
    QTabBar#workspaceTaskTabs::tab { padding: 9px 10px; }
    QLabel#workspaceModsHeading { font-weight: 600; }
    QLineEdit, QComboBox, QSpinBox {
      background: %9; border: 1px solid %4; border-radius: 5px;
      padding: 5px 8px; min-height: 18px; selection-background-color: %6;
      selection-color: %8;
    }
    QComboBox { padding-right: 24px; }
    QComboBox::drop-down { border: none; width: 22px; }
    QAbstractItemView, QPlainTextEdit {
      background: %9; alternate-background-color: %2; border: 1px solid %11;
      border-radius: 4px; selection-background-color: %6; selection-color: %8;
    }
    QTreeView::item { padding-top: %10px; padding-bottom: %10px; }
    QDialog#ProfilesDialog QListWidget::item,
    QDialog#EditExecutablesDialog QListWidget::item { padding: 7px 8px; }
    QHeaderView { background: %3; }
    QHeaderView::section {
      background: %3; color: %5; border: none; border-radius: 0;
      border-bottom: 1px solid %11; border-right: 2px solid %11;
      padding: 7px 8px; font-weight: 600;
    }
    QHeaderView::section:hover { background: %3; border-right-color: %6; }
    QTabWidget::pane { border: none; }
    QSplitter::handle { background: %2; }
    QSplitter::handle:horizontal {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
          stop:0 %2, stop:0.39 %2, stop:0.4 %11,
          stop:0.6 %11, stop:0.61 %2, stop:1 %2);
    }
    QSplitter::handle:vertical {
      background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
          stop:0 %2, stop:0.39 %2, stop:0.4 %11,
          stop:0.6 %11, stop:0.61 %2, stop:1 %2);
    }
    QSplitter::handle:hover { background: %6; }
    QScrollArea { border: none; background: transparent; }
    QScrollBar:vertical { background: %2; border: none; width: 10px; margin: 0; }
    QScrollBar:horizontal { background: %2; border: none; height: 10px; margin: 0; }
    QScrollBar::handle:vertical { background: %4; min-height: 28px; border: none; border-radius: 4px; }
    QScrollBar::handle:horizontal { background: %4; min-width: 28px; border: none; border-radius: 4px; }
    QScrollBar::handle:hover { background: %5; }
    QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; border: none; background: transparent; }
    QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
    QScrollBar::up-arrow, QScrollBar::down-arrow, QScrollBar::left-arrow, QScrollBar::right-arrow { width: 0; height: 0; background: transparent; border: none; }
    QGroupBox { border: 1px solid %4; border-radius: 5px; margin-top: 12px; padding-top: 8px; }
    QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
    QMenu { background: %3; border: 1px solid %4; padding: 5px; }
    QMenu::item { padding: 6px 24px 6px 10px; border-radius: 3px; }
    QMenu::item:selected { background: %6; color: %8; }
  )")
      .arg(text.name(), window.name(), raised.name(), border.name(), muted.name(),
           accent.name(), hover.name(), selectedText.name(), base.name())
      .arg(compact ? 1 : 4)
      .arg(divider.name()));
}
