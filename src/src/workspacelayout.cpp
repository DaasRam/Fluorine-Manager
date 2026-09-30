#include "workspacelayout.h"

#include <QScopedValueRollback>
#include <QScopeGuard>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabBar>
#include <QTabWidget>
#include <QWidget>

WorkspaceLayout::WorkspaceLayout(QSplitter* categories, QSplitter* panes,
    QWidget* mods, QWidget* filters, QWidget* rightPane, QTabWidget* tabs,
    QWidget* plugins, QWidget* inspector, QWidget* modTools, QObject* parent)
    : QObject(parent), m_categories(categories), m_panes(panes), m_mods(mods),
      m_filters(filters), m_rightPane(rightPane), m_plugins(plugins),
      m_inspector(inspector), m_tools(modTools), m_tabs(tabs)
{}

void WorkspaceLayout::initialize(Mode mode, bool filtersVisible)
{
  m_filtersVisible = filtersVisible;
  m_splitState = m_panes->saveState();
  m_page = m_tabs->currentWidget();
  m_mode = mode;
  m_initialized = true;
  apply();
}

void WorkspaceLayout::capture()
{
  if (!m_initialized) return;
  if (m_mode == Mode::Split) m_splitState = m_panes->saveState();
  if (m_mode == Mode::Details) m_detailsState = m_panes->saveState();
}

void WorkspaceLayout::apply()
{
  if (!m_initialized) return;
  const QScopedValueRollback changing(m_changing, true);
  const bool updatesEnabled = m_categories->updatesEnabled();
  m_categories->setUpdatesEnabled(false);
  const auto restoreUpdates = qScopeGuard([this, updatesEnabled] {
    m_categories->setUpdatesEnabled(updatesEnabled);
  });
  const bool details = m_mode == Mode::Details;
  const bool split = m_mode == Mode::Split;
  // A metadata inspector should retain its chosen width when the window grows;
  // the expert split shares extra space between two full tables.
  m_panes->setStretchFactor(0, split ? 3 : 1);
  m_panes->setStretchFactor(1, split ? 2 : 0);
  m_tabs->tabBar()->setVisible(split);
  m_tools->show();
  m_mods->show();
  m_inspector->setVisible(details);
  m_tabs->setVisible(split);
  m_rightPane->setVisible(m_mode != Mode::Full);

  // Every workspace mode keeps the mod list and its filter splitter in place.
  // Showing a hidden filter just to restore geometry briefly squeezes both
  // panes, so leave that splitter's live geometry alone.
  m_filters->setVisible(m_filtersVisible);
  if (split) {
    if (!m_page || m_tabs->indexOf(m_page) < 0)
      m_page = m_plugins && m_tabs->indexOf(m_plugins) >= 0
                   ? m_plugins.data() : m_tabs->widget(0);
    if (m_page) m_tabs->setCurrentWidget(m_page);
  }
  const auto& state = details ? m_detailsState : m_splitState;
  if (m_mode != Mode::Full) {
    if (!state.isEmpty()) m_panes->restoreState(state);
    else {
      const int width = m_panes->width();
      m_panes->setSizes(details ? QList<int>{qMax(330, width - 292), 292}
                               : QList<int>{width * 3 / 5, width * 2 / 5});
    }
  }
}

void WorkspaceLayout::showMods()
{
  // All views retain the mod list. Returning from Library needs no page reset.
  m_mods->show();
}

bool WorkspaceLayout::showPage(QWidget* page)
{
  if (!page || !m_tabs || m_tabs->indexOf(page) < 0) return false;
  if (!m_initialized) return false;
  if (m_mode == Mode::Split) {
    // QTabWidget already owns the page transition. Reapplying the whole layout
    // here causes transient visibility/resize events on otherwise stable panes.
    m_page = page;
    const QScopedValueRollback changing(m_changing, true);
    if (m_tabs->currentWidget() != page) m_tabs->setCurrentWidget(page);
    return true;
  }
  capture();
  m_page = page;
  m_mode = Mode::Split;
  apply();
  return true;
}

void WorkspaceLayout::setMode(Mode mode)
{
  if (mode == Mode::Split && m_tabs->count() == 0)
    mode = Mode::Full;
  if (mode == m_mode) return;
  capture();
  m_mode = mode;
  apply();
}

void WorkspaceLayout::setFiltersVisible(bool visible)
{
  m_filtersVisible = visible;
  m_filters->setVisible(visible);
}

QByteArray WorkspaceLayout::detailsState() const
{
  return m_initialized && inspecting() ? m_panes->saveState() : m_detailsState;
}

void WorkspaceLayout::withStoredLayout(const std::function<void()>& save)
{
  if (!m_initialized) { save(); return; }
  capture();
  if (m_mode == Mode::Split) { save(); return; }
  const auto mode = m_mode;
  const bool updatesEnabled = m_categories->updatesEnabled();
  m_categories->setUpdatesEnabled(false);
  const QSignalBlocker tabSignals(m_tabs);
  const auto restoreLayout = qScopeGuard([this, mode, updatesEnabled] {
    m_mode = mode;
    apply();
    m_categories->setUpdatesEnabled(updatesEnabled);
  });
  m_mode = Mode::Split;
  apply();
  save();
}
