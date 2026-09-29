#include "modconflicticondelegate.h"
#include "modlist.h"
#include "modlistview.h"
#include <QHelpEvent>
#include <QList>
#include <QToolTip>
#include <log.h>

using namespace MOBase;

ModConflictIconDelegate::ModConflictIconDelegate(ModListView* view, int logicalIndex,
                                                 int compactSize)
    : IconDelegate(view, logicalIndex, compactSize), m_view(view)
{}

QList<QString>
ModConflictIconDelegate::getIconsForFlags(std::vector<ModInfo::EConflictFlag> flags,
                                          bool compact)
{
  QList<QString> result;

  // Don't do flags for overwrite
  if (std::find(flags.begin(), flags.end(), ModInfo::FLAG_OVERWRITE_CONFLICT) !=
      flags.end())
    return result;

  // insert conflict icons to provide nicer alignment
  {  // insert loose file conflicts first
    auto iter = std::find_first_of(flags.begin(), flags.end(), s_ConflictFlags.begin(),
                                   s_ConflictFlags.end());
    if (iter != flags.end()) {
      result.append(getFlagIcon(*iter));
      flags.erase(iter);
    } else if (!compact) {
      result.append(QString());
    }
  }

  {  // insert loose vs archive overwrite second
    auto iter = std::find(flags.begin(), flags.end(),
                          ModInfo::FLAG_ARCHIVE_LOOSE_CONFLICT_OVERWRITE);
    if (iter != flags.end()) {
      result.append(getFlagIcon(*iter));
      flags.erase(iter);
    } else if (!compact) {
      result.append(QString());
    }
  }

  {  // insert loose vs archive overwritten third
    auto iter = std::find(flags.begin(), flags.end(),
                          ModInfo::FLAG_ARCHIVE_LOOSE_CONFLICT_OVERWRITTEN);
    if (iter != flags.end()) {
      result.append(getFlagIcon(*iter));
      flags.erase(iter);
    } else if (!compact) {
      result.append(QString());
    }
  }

  {  // insert archive conflicts last
    auto iter =
        std::find_first_of(flags.begin(), flags.end(), s_ArchiveConflictFlags.begin(),
                           s_ArchiveConflictFlags.end());
    if (iter != flags.end()) {
      result.append(getFlagIcon(*iter));
      flags.erase(iter);
    } else if (!compact) {
      result.append(QString());
    }
  }

  for (auto iter = flags.begin(); iter != flags.end(); ++iter) {
    auto iconPath = getFlagIcon(*iter);
    if (!iconPath.isEmpty())
      result.append(iconPath);
  }

  return result;
}

QList<QString> ModConflictIconDelegate::getIcons(const QModelIndex& index) const
{
  QVariant const modIndex = index.data(ModList::IndexRole);

  if (!modIndex.isValid()) {
    return {};
  }

  bool compact;
  auto flags = m_view->conflictFlags(index, &compact);
  return getIconsForFlags(flags, compact || this->compact());
}

QString ModConflictIconDelegate::getFlagIcon(ModInfo::EConflictFlag flag)
{
  switch (flag) {
  case ModInfo::FLAG_CONFLICT_MIXED:
    return QStringLiteral(":/MO/gui/emblem_conflict_mixed");
  case ModInfo::FLAG_CONFLICT_OVERWRITE:
    return QStringLiteral(":/MO/gui/emblem_conflict_overwrite");
  case ModInfo::FLAG_CONFLICT_OVERWRITTEN:
    return QStringLiteral(":/MO/gui/emblem_conflict_overwritten");
  case ModInfo::FLAG_CONFLICT_REDUNDANT:
    return QStringLiteral(":/MO/gui/emblem_conflict_redundant");
  case ModInfo::FLAG_ARCHIVE_LOOSE_CONFLICT_OVERWRITE:
    return QStringLiteral(":/MO/gui/archive_loose_conflict_overwrite");
  case ModInfo::FLAG_ARCHIVE_LOOSE_CONFLICT_OVERWRITTEN:
    return QStringLiteral(":/MO/gui/archive_loose_conflict_overwritten");
  case ModInfo::FLAG_ARCHIVE_CONFLICT_MIXED:
    return QStringLiteral(":/MO/gui/archive_conflict_mixed");
  case ModInfo::FLAG_ARCHIVE_CONFLICT_OVERWRITE:
    return QStringLiteral(":/MO/gui/archive_conflict_winner");
  case ModInfo::FLAG_ARCHIVE_CONFLICT_OVERWRITTEN:
    return QStringLiteral(":/MO/gui/archive_conflict_loser");
  case ModInfo::FLAG_OVERWRITE_CONFLICT:
    return {};
  default:
    log::warn("ModInfo flag {} has no defined icon", flag);
    return {};
  }
}

size_t ModConflictIconDelegate::getNumIcons(const QModelIndex& index) const
{
  QVariant const modIndex = index.data(ModList::IndexRole);

  if (!modIndex.isValid()) {
    return 0;
  }

  return m_view->conflictFlags(index, nullptr).size();
}

QSize ModConflictIconDelegate::sizeHint(const QStyleOptionViewItem& option,
                                        const QModelIndex& modelIndex) const
{
  Q_UNUSED(option);
  if (!modelIndex.data(ModList::IndexRole).isValid()) {
    return {1, 20};
  }

  bool forceCompact = false;
  const auto flags = m_view->conflictFlags(modelIndex, &forceCompact);
  const int iconSlots = static_cast<int>(getIconsForFlags(flags, forceCompact).size());
  // IconDelegate paints 16px icons with 4px gaps and a 4px leading margin.
  // Reserve the empty alignment slots in expanded rows, too.
  return {4 + iconSlots * 20, 20};
}

bool ModConflictIconDelegate::helpEvent(QHelpEvent* event,
                                        QAbstractItemView* view,
                                        const QStyleOptionViewItem& option,
                                        const QModelIndex& index)
{
  if (event && view && index.isValid() && event->type() == QEvent::ToolTip) {
    bool includesChildren = false;
    m_view->conflictFlags(index, &includesChildren);
    if (includesChildren) {
      QStringList details;
      for (int row = 0; row < m_view->model()->rowCount(index); ++row) {
        const auto child = m_view->model()->index(row, index.column(), index);
        const auto childText = child.data(Qt::ToolTipRole).toString();
        for (const auto& line : childText.split(QStringLiteral("<br>"),
                                               Qt::SkipEmptyParts)) {
          if (!details.contains(line)) {
            details.append(line);
          }
        }
      }
      const QString summary = tr("Conflict types among mods in this collapsed group:");
      QToolTip::showText(event->globalPos(),
                         details.isEmpty() ? summary
                                           : summary + QStringLiteral("<br>") +
                                                 details.join(QStringLiteral("<br>")),
                         view);
      return true;
    }
  }
  return IconDelegate::helpEvent(event, view, option, index);
}
