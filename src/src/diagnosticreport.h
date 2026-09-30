#ifndef DIAGNOSTICREPORT_H
#define DIAGNOSTICREPORT_H

#include "vfs/vfsindex.h"

#include <QDialog>
#include <QString>
#include <QtGlobal>

#include <optional>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QTextBrowser;

enum class DiagnosticReportCategory
{
  Bug,
  SlowPreparationOrCacheReuse,
  UiOrSetup
};

enum class DiagnosticReportAvailability
{
  Unknown,
  Available,
  Missing,
  NotRequired
};

enum class DiagnosticReportBackend
{
  Unknown,
  Fuse,
  Usvfs,
  GameManaged
};

enum class DiagnosticReportLaunchMode
{
  Unknown,
  Native,
  Proton
};

enum class DiagnosticPreparationPhase
{
  Unknown,
  Metadata,
  Hashing,
  Archives,
  Duplicates,
  CatalogCommit,
  Complete,
  IndexPublication
};

struct DiagnosticPreparationSummary
{
  DiagnosticPreparationPhase phase = DiagnosticPreparationPhase::Unknown;
  quint64 files_scanned = 0;
  quint64 files_hashed = 0;
  quint64 bytes_hashed = 0;
  std::optional<bool> index_reused;
  std::optional<VfsIndexReuseReason> index_reuse_reason;
};

// Keep this detached from environment snapshots and filesystem paths. Callers
// provide build labels and typed runtime states only; no logs are scanned.
struct DiagnosticReportContext
{
  QString version;
  QString channel;
  QString commit;
  DiagnosticReportBackend effective_backend =
      DiagnosticReportBackend::Unknown;
  DiagnosticReportLaunchMode launch_mode =
      DiagnosticReportLaunchMode::Unknown;
  DiagnosticReportAvailability proton_launcher =
      DiagnosticReportAvailability::Unknown;
  DiagnosticReportAvailability wine_prefix =
      DiagnosticReportAvailability::Unknown;
  DiagnosticReportAvailability steam_linux_runtime =
      DiagnosticReportAvailability::Unknown;
  DiagnosticReportAvailability backend_prerequisite =
      DiagnosticReportAvailability::Unknown;
  std::optional<DiagnosticPreparationSummary> last_preparation;
};

class DiagnosticReportDialog : public QDialog
{
  Q_OBJECT

public:
  explicit DiagnosticReportDialog(DiagnosticReportContext context,
                                  QWidget* parent = nullptr);

  QString reportText() const;
  bool saveReportToFile(const QString& path, QString* error = nullptr) const;

signals:
  void openIssueFormsRequested();

private:
  void updatePreview();

  DiagnosticReportContext m_context;
  QComboBox* m_category{};
  QPlainTextEdit* m_problem{};
  QPlainTextEdit* m_reproduction{};
  QPlainTextEdit* m_expected{};
  QTextBrowser* m_preview{};
  QLabel* m_status{};
};

#endif // DIAGNOSTICREPORT_H
