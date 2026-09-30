#pragma once

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <functional>

class QSplitter;
class QTabWidget;
class QWidget;

// Keeps the mod list beside the existing task tabs without recreating models.
// Optional details and mods-only views retain independent splitter geometry.
class WorkspaceLayout final : public QObject
{
public:
  enum class Mode { Details, Split, Full };

  WorkspaceLayout(QSplitter* categories, QSplitter* panes, QWidget* mods,
                  QWidget* filters, QWidget* rightPane, QTabWidget* tabs,
                  QWidget* plugins, QWidget* inspector, QWidget* modTools,
                  QObject* parent = nullptr);

  void initialize(Mode mode, bool filtersVisible);
  void showMods();
  bool showPage(QWidget* page);
  void setMode(Mode mode);
  Mode mode() const { return m_mode; }
  bool changing() const { return m_changing; }
  bool inspecting() const { return m_mode == Mode::Details; }
  void setFiltersVisible(bool visible);
  bool filtersVisible() const { return m_filtersVisible; }
  QByteArray detailsState() const;
  void setDetailsState(const QByteArray& state) { m_detailsState = state; }

  // Existing geometry settings expect the original two-pane layout. Invoke
  // their serializer against that layout without emitting navigation signals.
  void withStoredLayout(const std::function<void()>& save);

private:
  void capture();
  void apply();

  QPointer<QSplitter> m_categories;
  QPointer<QSplitter> m_panes;
  QPointer<QWidget> m_mods, m_filters, m_rightPane, m_plugins, m_inspector, m_tools;
  QPointer<QTabWidget> m_tabs;
  QPointer<QWidget> m_page;
  QByteArray m_splitState, m_detailsState;
  Mode m_mode = Mode::Split;
  bool m_filtersVisible = false;
  bool m_changing = false;
  bool m_initialized = false;
};
