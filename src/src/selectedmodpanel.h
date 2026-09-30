#pragma once

#include <QString>
#include <QWidget>

#include <optional>

class QLabel;
class QPushButton;
class QVBoxLayout;

/**
 * A plain-text snapshot of information the parent already has for one selected
 * mod. Empty strings mean that the value is unavailable. Conflict counts are
 * optional: -1 means the parent has not computed them.
 * `description` must also be plain text; the widget does not render or sanitize
 * HTML supplied by a source model.
 *
 * `winningMods` and `losingMods` describe loose-file relationships only:
 * distinct mods whose files this mod overwrites, and distinct mods that
 * overwrite files from this mod. They are not archive-conflict counts.
 */
struct SelectedModSummary
{
  QString name;
  QString version;
  QString category;
  QString author;
  QString description;
  QString notes;
  QString profile;
  std::optional<int> priority;
  bool enabled = false;
  int winningMods = -1;
  int losingMods = -1;
  bool separator = false;
  bool external = false;
};

/**
 * Compact selected-mod inspector. It owns no organizer/model state; callers
 * provide an inexpensive snapshot and handle navigation through the signals.
 */
class SelectedModPanel : public QWidget
{
  Q_OBJECT

public:
  explicit SelectedModPanel(QWidget* parent = nullptr);

  void setSummary(std::optional<SelectedModSummary> summary);
  void setMultipleSelection(int count);

signals:
  void detailsRequested();
  void conflictsRequested();
  void closeRequested();

private:
  struct Field
  {
    QLabel* label = nullptr;
    QLabel* value = nullptr;
  };

  Field addField(QVBoxLayout* layout, const QString& label,
                 const QString& objectName);
  void refresh();

  QLabel* m_heading = nullptr;
  QLabel* m_name = nullptr;
  QLabel* m_status = nullptr;
  QLabel* m_empty = nullptr;
  Field m_profile;
  Field m_version;
  Field m_category;
  Field m_author;
  Field m_priority;
  Field m_description;
  Field m_notes;
  Field m_conflicts;
  QPushButton* m_detailsButton = nullptr;
  QPushButton* m_conflictsButton = nullptr;
  std::optional<SelectedModSummary> m_summary;
  int m_selectionCount = 0;
};
