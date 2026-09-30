#ifndef FILECONFLICTINSPECTOR_H
#define FILECONFLICTINSPECTOR_H

#include <QList>
#include <QString>
#include <QWidget>

#include <optional>

class QLabel;
class QScrollArea;
class QVBoxLayout;
class QWidget;

enum class FileConflictInspectorState
{
  EmptySelection,
  MultipleSelection,
  Unknown,
  NoConflict,
  Conflict
};

enum class FileConflictSourceKind
{
  Loose,
  Archive,
  Unknown
};

struct FileConflictProviderSnapshot
{
  QString modName;
  FileConflictSourceKind sourceKind = FileConflictSourceKind::Unknown;
  QString archiveName;
  std::optional<int> modPriority;
  std::optional<int> archiveOrder;
  bool isCurrentMod = false;
  bool isWinner = false;
};

/// Detached, read-only view of one selected virtual file's current providers.
struct FileConflictSnapshot
{
  FileConflictInspectorState state = FileConflictInspectorState::EmptySelection;
  QString relativePath;
  QString currentModName;
  QList<FileConflictProviderSnapshot> providers;
  QString detail;
};

/// A compact explanation of one file's current conflict state and providers.
/// Provider order is display order only; the winner comes from isWinner.
class FileConflictInspector : public QWidget
{
public:
  explicit FileConflictInspector(QWidget* parent = nullptr);

  void setSnapshot(const FileConflictSnapshot& snapshot);
  const FileConflictSnapshot& snapshot() const { return m_snapshot; }

private:
  void refresh();
  QString winnerRuleExplanation() const;
  void clearProviders();

  FileConflictSnapshot m_snapshot;
  QLabel* m_stateLabel = nullptr;
  QLabel* m_pathLabel = nullptr;
  QLabel* m_currentModLabel = nullptr;
  QLabel* m_detailLabel = nullptr;
  QLabel* m_ruleLabel = nullptr;
  QLabel* m_providerHeading = nullptr;
  QLabel* m_noProvidersLabel = nullptr;
  QScrollArea* m_providerScrollArea = nullptr;
  QWidget* m_providerContainer = nullptr;
  QVBoxLayout* m_providerLayout = nullptr;
};

#endif // FILECONFLICTINSPECTOR_H
