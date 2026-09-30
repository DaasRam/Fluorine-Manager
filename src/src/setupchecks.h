#ifndef SETUPCHECKS_H
#define SETUPCHECKS_H

#include <QDialog>
#include <QList>
#include <QMetaType>
#include <QString>

#include <optional>

class QLabel;
class QScrollArea;
class QVBoxLayout;
class QWidget;

enum class SetupCheckState
{
  Passing,
  NeedsAttention,
  NotRequired,
  Unchecked
};

enum class SetupCheckAction
{
  None,
  Paths,
  Compatibility,
  Executables
};

enum class SetupCheckItem
{
  GameFolder,
  Executable,
  ProtonLauncher,
  WinePrefix,
  SteamLinuxRuntime,
  VirtualFilesystem
};

struct SetupCheckInputs
{
  QString gameFolderPath;
  QString selectedExecutableName; // Display label only; never used to find the file.
  QString selectedExecutablePath; // Actual selected binary path.
  std::optional<bool> useProton;
  // Accepts either a Proton installation directory or its direct launcher.
  QString protonInstallationPath;
  QString prefixPath;

  // The caller supplies the effective backend decision and any runtime probe.
  // A null availability means that no backend availability check was supplied.
  QString effectiveBackendDescription;
  bool backendRequired = false;
  std::optional<bool> backendPrerequisiteAvailable;
  QString backendPrerequisitePath;
  QString backendStatusDetail;

  // The caller may provide its read-only managed SLR installation result.
  std::optional<bool> steamRuntimeAvailable;
};

struct SetupCheckRow
{
  SetupCheckItem item = SetupCheckItem::GameFolder;
  QString title;
  SetupCheckState state = SetupCheckState::Unchecked;
  QString detail;
  SetupCheckAction action = SetupCheckAction::None;
};

/// Evaluates only basic local file and configuration state; it does not launch
/// the game, initialize a prefix, or probe a VFS backend itself.
class SetupCheckModel
{
public:
  static QList<SetupCheckRow> evaluate(const SetupCheckInputs& inputs);
};

/// Displays the current basic setup checks and forwards navigation requests.
class SetupChecksDialog : public QDialog
{
  Q_OBJECT

public:
  explicit SetupChecksDialog(QWidget* parent = nullptr);

  void setInputs(const SetupCheckInputs& inputs);
  const QList<SetupCheckRow>& rows() const { return m_rows; }

signals:
  void recheckRequested();
  void actionRequested(SetupCheckAction action);

private:
  void refreshRows();

  SetupCheckInputs m_inputs;
  QList<SetupCheckRow> m_rows;
  QLabel* m_programLabel = nullptr;
  QLabel* m_summaryLabel = nullptr;
  QScrollArea* m_scrollArea = nullptr;
  QWidget* m_rowsContainer = nullptr;
  QVBoxLayout* m_rowsLayout = nullptr;
};

Q_DECLARE_METATYPE(SetupCheckAction)

#endif // SETUPCHECKS_H
