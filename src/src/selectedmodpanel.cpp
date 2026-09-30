#include "selectedmodpanel.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStringList>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

SelectedModPanel::SelectedModPanel(QWidget* parent) : QWidget(parent)
{
  setObjectName(QStringLiteral("selectedModPanel"));
  setMinimumWidth(260);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);

  auto* outer = new QVBoxLayout(this);
  outer->setContentsMargins(12, 10, 12, 10);
  outer->setSpacing(8);

  auto* header = new QHBoxLayout;
  header->setContentsMargins(0, 0, 0, 0);
  m_heading = new QLabel(tr("Selected mod"), this);
  m_heading->setObjectName(QStringLiteral("selectedModPanelHeading"));
  m_heading->setTextFormat(Qt::PlainText);
  m_heading->setProperty("secondary", true);
  header->addWidget(m_heading, 1);

  auto* close = new QToolButton(this);
  close->setObjectName(QStringLiteral("selectedModPanelClose"));
  close->setText(QString::fromUtf8("×"));
  close->setAccessibleName(tr("Close selected mod panel"));
  close->setToolTip(tr("Close details"));
  close->setAutoRaise(true);
  close->setToolButtonStyle(Qt::ToolButtonTextOnly);
  close->setMinimumSize(34, 34);
  header->addWidget(close);
  outer->addLayout(header);
  connect(close, &QToolButton::clicked, this,
          &SelectedModPanel::closeRequested);

  m_name = new QLabel(this);
  m_name->setObjectName(QStringLiteral("selectedModPanelName"));
  m_name->setTextFormat(Qt::PlainText);
  m_name->setWordWrap(true);
  m_name->setMinimumWidth(0);
  m_name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  auto nameFont = m_name->font();
  nameFont.setBold(true);
  m_name->setFont(nameFont);
  outer->addWidget(m_name);

  m_status = new QLabel(this);
  m_status->setObjectName(QStringLiteral("selectedModPanelStatus"));
  m_status->setTextFormat(Qt::PlainText);
  m_status->setWordWrap(true);
  outer->addWidget(m_status);

  auto* scroll = new QScrollArea(this);
  scroll->setObjectName(QStringLiteral("selectedModPanelScrollArea"));
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setWidgetResizable(true);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

  auto* body = new QWidget(scroll);
  body->setObjectName(QStringLiteral("selectedModPanelBody"));
  body->setMinimumWidth(0);
  auto* bodyLayout = new QVBoxLayout(body);
  bodyLayout->setContentsMargins(0, 2, 0, 2);
  bodyLayout->setSpacing(8);

  m_empty = new QLabel(body);
  m_empty->setObjectName(QStringLiteral("selectedModPanelEmpty"));
  m_empty->setTextFormat(Qt::PlainText);
  m_empty->setWordWrap(true);
  m_empty->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  bodyLayout->addWidget(m_empty);

  m_profile = addField(bodyLayout, tr("Profile"),
                       QStringLiteral("selectedModPanelProfile"));
  m_version = addField(bodyLayout, tr("Version"),
                       QStringLiteral("selectedModPanelVersion"));
  m_category = addField(bodyLayout, tr("Category"),
                        QStringLiteral("selectedModPanelCategory"));
  m_author = addField(bodyLayout, tr("Author"),
                      QStringLiteral("selectedModPanelAuthor"));
  m_priority = addField(bodyLayout, tr("Mod file priority"),
                        QStringLiteral("selectedModPanelPriority"));
  m_description = addField(bodyLayout, tr("Description"),
                            QStringLiteral("selectedModPanelDescription"));
  m_notes = addField(bodyLayout, tr("Notes"),
                     QStringLiteral("selectedModPanelNotes"));
  m_conflicts = addField(bodyLayout, tr("Loose-file conflicts"),
                         QStringLiteral("selectedModPanelConflicts"));
  bodyLayout->addStretch(1);
  scroll->setWidget(body);
  outer->addWidget(scroll, 1);

  auto* actions = new QVBoxLayout;
  actions->setContentsMargins(0, 0, 0, 0);
  actions->setSpacing(6);
  m_detailsButton = new QPushButton(tr("Open details"), this);
  m_detailsButton->setObjectName(QStringLiteral("selectedModPanelDetails"));
  m_detailsButton->setMinimumHeight(34);
  m_conflictsButton = new QPushButton(tr("Inspect file conflicts"), this);
  m_conflictsButton->setObjectName(QStringLiteral("selectedModPanelInspectConflicts"));
  m_conflictsButton->setMinimumHeight(34);
  actions->addWidget(m_detailsButton);
  actions->addWidget(m_conflictsButton);
  outer->addLayout(actions);

  connect(m_detailsButton, &QPushButton::clicked,
          this, &SelectedModPanel::detailsRequested);
  connect(m_conflictsButton, &QPushButton::clicked,
          this, &SelectedModPanel::conflictsRequested);

  refresh();
}

SelectedModPanel::Field SelectedModPanel::addField(
    QVBoxLayout* layout, const QString& label, const QString& objectName)
{
  auto* row = new QWidget(this);
  row->setObjectName(objectName + QStringLiteral("Row"));
  auto* rowLayout = new QVBoxLayout(row);
  rowLayout->setContentsMargins(0, 2, 0, 2);
  rowLayout->setSpacing(1);

  auto* title = new QLabel(label, row);
  title->setObjectName(objectName + QStringLiteral("Label"));
  title->setTextFormat(Qt::PlainText);
  title->setProperty("secondary", true);

  auto* value = new QLabel(row);
  value->setObjectName(objectName + QStringLiteral("Value"));
  value->setTextFormat(Qt::PlainText);
  value->setWordWrap(true);
  value->setMinimumWidth(0);
  value->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  value->setTextInteractionFlags(Qt::TextSelectableByMouse);

  rowLayout->addWidget(title);
  rowLayout->addWidget(value);
  row->setVisible(false);
  layout->addWidget(row);
  return {title, value};
}

void SelectedModPanel::setSummary(
    std::optional<SelectedModSummary> summary)
{
  // The parent supplies a fresh snapshot for the current selection. Treat it
  // as authoritative so that returning from a multi-selection can show the
  // newly selected mod without requiring a separate count-reset call.
  m_summary = std::move(summary);
  m_selectionCount = m_summary ? 1 : 0;
  refresh();
}

void SelectedModPanel::setMultipleSelection(int count)
{
  m_selectionCount = std::max(0, count);
  if (m_selectionCount != 1) m_summary.reset();
  refresh();
}

void SelectedModPanel::refresh()
{
  const auto setField = [](const Field& field, const QString& value) {
    if (!field.label || !field.value) return;
    field.value->setText(value);
    const bool visible = !value.trimmed().isEmpty();
    field.label->parentWidget()->setVisible(visible);
  };

  setField(m_profile, {});
  setField(m_version, {});
  setField(m_category, {});
  setField(m_author, {});
  setField(m_priority, {});
  setField(m_description, {});
  setField(m_notes, {});
  setField(m_conflicts, {});
  m_name->clear();
  m_status->clear();

  if (m_selectionCount > 1) {
    m_heading->setText(tr("Selection"));
    m_name->setVisible(false);
    m_status->setVisible(false);
    m_empty->setText(tr("%n mods selected. Select one mod to see its details.",
                        nullptr, m_selectionCount));
    m_empty->setVisible(true);
    m_detailsButton->setVisible(false);
    m_conflictsButton->setVisible(false);
    return;
  }

  if (!m_summary) {
    m_heading->setText(tr("Selected mod"));
    m_name->setVisible(false);
    m_status->setVisible(false);
    m_empty->setText(m_selectionCount == 0
                         ? tr("Select a mod to see its details.")
                         : tr("Mod details are unavailable."));
    m_empty->setVisible(true);
    m_detailsButton->setVisible(false);
    m_conflictsButton->setVisible(false);
    return;
  }

  const SelectedModSummary& summary = *m_summary;
  m_heading->setText(summary.separator ? tr("Separator") :
                      summary.external ? tr("Game content") : tr("Selected mod"));
  m_name->setText(summary.name.trimmed().isEmpty()
                      ? tr("Name unavailable")
                      : summary.name);
  m_name->setVisible(true);
  m_empty->clear();
  m_empty->setVisible(false);

  if (summary.separator) {
    m_status->setText(tr("Organizes the mod list."));
    m_status->setVisible(true);
    m_detailsButton->setVisible(false);
    m_conflictsButton->setVisible(false);
    return;
  }

  m_status->setText(summary.external ? tr("Managed outside Fluorine") :
                     summary.enabled ? tr("Enabled") : tr("Disabled"));
  m_status->setVisible(true);
  if (!summary.profile.trimmed().isEmpty())
    setField(m_profile, summary.profile);
  if (!summary.version.trimmed().isEmpty())
    setField(m_version, summary.version);
  if (!summary.category.trimmed().isEmpty())
    setField(m_category, summary.category);
  if (!summary.author.trimmed().isEmpty())
    setField(m_author, summary.author);
  if (summary.priority.has_value())
    setField(m_priority, QString::number(*summary.priority));
  if (!summary.description.trimmed().isEmpty())
    setField(m_description, summary.description);
  if (!summary.notes.trimmed().isEmpty())
    setField(m_notes, summary.notes);

  if (summary.winningMods >= 0 && summary.losingMods >= 0) {
    if (summary.winningMods == 0 && summary.losingMods == 0) {
      setField(m_conflicts, tr("No loose-file conflicts reported."));
    } else {
      QStringList relationships;
      if (summary.winningMods > 0) {
        relationships.append(
            tr("This mod overwrites %n other mod(s).", nullptr,
               summary.winningMods));
      }
      if (summary.losingMods > 0) {
        relationships.append(
            tr("Files from %n other mod(s) overwrite this mod.", nullptr,
               summary.losingMods));
      }
      setField(m_conflicts, relationships.join(QLatin1Char('\n')));
    }
  }

  m_detailsButton->setVisible(!summary.external);
  m_conflictsButton->setVisible(!summary.external);
}
