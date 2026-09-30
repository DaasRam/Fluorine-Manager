#include "diagnosticreport.h"

#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QSaveFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>

#include <utility>

namespace
{
QString oneLine(QString value)
{
  value.replace(QLatin1Char('\r'), QLatin1Char(' '));
  value.replace(QLatin1Char('\n'), QLatin1Char(' '));
  value.replace(QLatin1Char('\t'), QLatin1Char(' '));
  return value.simplified();
}

QString categoryText(DiagnosticReportCategory category)
{
  switch (category) {
  case DiagnosticReportCategory::Bug:
    return QObject::tr("Bug");
  case DiagnosticReportCategory::SlowPreparationOrCacheReuse:
    return QObject::tr("Slow preparation or cache reuse");
  case DiagnosticReportCategory::UiOrSetup:
    return QObject::tr("UI or setup");
  }
  return QObject::tr("Bug");
}

QString backendText(DiagnosticReportBackend backend)
{
  switch (backend) {
  case DiagnosticReportBackend::Unknown: return {};
  case DiagnosticReportBackend::Fuse: return QObject::tr("FUSE");
  case DiagnosticReportBackend::Usvfs: return QObject::tr("USVFS");
  case DiagnosticReportBackend::GameManaged:
    return QObject::tr("Managed by the game");
  }
  return {};
}

QString launchModeText(DiagnosticReportLaunchMode mode)
{
  switch (mode) {
  case DiagnosticReportLaunchMode::Unknown: return {};
  case DiagnosticReportLaunchMode::Native: return QObject::tr("Native");
  case DiagnosticReportLaunchMode::Proton: return QObject::tr("Proton");
  }
  return {};
}

QString availabilityText(DiagnosticReportAvailability availability)
{
  switch (availability) {
  case DiagnosticReportAvailability::Unknown: return {};
  case DiagnosticReportAvailability::Available:
    return QObject::tr("Available");
  case DiagnosticReportAvailability::Missing: return QObject::tr("Missing");
  case DiagnosticReportAvailability::NotRequired:
    return QObject::tr("Not required");
  }
  return {};
}

QString preparationPhaseText(DiagnosticPreparationPhase phase)
{
  switch (phase) {
  case DiagnosticPreparationPhase::Unknown: return {};
  case DiagnosticPreparationPhase::Metadata:
    return QObject::tr("Checking file metadata");
  case DiagnosticPreparationPhase::Hashing:
    return QObject::tr("Hashing changed files");
  case DiagnosticPreparationPhase::Archives:
    return QObject::tr("Checking archive contents");
  case DiagnosticPreparationPhase::Duplicates:
    return QObject::tr("Comparing cached file contents");
  case DiagnosticPreparationPhase::CatalogCommit:
    return QObject::tr("Saving the file catalog");
  case DiagnosticPreparationPhase::Complete:
    return QObject::tr("File catalog complete");
  case DiagnosticPreparationPhase::IndexPublication:
    return QObject::tr("Checking or building the launch index");
  }
  return {};
}

DiagnosticReportCategory selectedCategory(const QComboBox* combo)
{
  const auto category = static_cast<DiagnosticReportCategory>(
      combo->currentData().toInt());
  switch (category) {
  case DiagnosticReportCategory::Bug:
  case DiagnosticReportCategory::SlowPreparationOrCacheReuse:
  case DiagnosticReportCategory::UiOrSetup:
    return category;
  }
  return DiagnosticReportCategory::Bug;
}

QString nonEmptyOr(const QString& value, const QString& fallback)
{
  const QString normalized = value.trimmed();
  return normalized.isEmpty() ? fallback : normalized;
}

QString buildReport(const DiagnosticReportContext& context,
                    DiagnosticReportCategory category,
                    const QString& problem, const QString& reproduction,
                    const QString& expected)
{
  QString report;
  report += QStringLiteral("# Fluorine issue report\n\n");
  report += QStringLiteral("Category: %1\n\n").arg(categoryText(category));

  report += QStringLiteral("## Problem\n\n");
  report += nonEmptyOr(problem, QObject::tr("(Please describe what happened.)"));
  report += QStringLiteral("\n\n## Steps to reproduce\n\n");
  report += nonEmptyOr(reproduction, QObject::tr("(Not provided.)"));
  report += QStringLiteral("\n\n## Expected result\n\n");
  report += nonEmptyOr(expected, QObject::tr("(Not provided.)"));

  const QString version = oneLine(context.version);
  const QString channel = oneLine(context.channel);
  const QString commit = oneLine(context.commit);
  report += QStringLiteral("\n\n## Build\n\n");
  report += QStringLiteral("- Version: %1\n")
                .arg(version.isEmpty() ? QObject::tr("unavailable") : version);
  if (!channel.isEmpty()) {
    report += QStringLiteral("- Channel: %1\n").arg(channel);
  }
  if (!commit.isEmpty()) {
    report += QStringLiteral("- Commit: %1\n").arg(commit);
  }

  const QString mode = launchModeText(context.launch_mode);
  const QString backend = backendText(context.effective_backend);
  const QString proton = availabilityText(context.proton_launcher);
  const QString prefix = availabilityText(context.wine_prefix);
  const QString slr = availabilityText(context.steam_linux_runtime);
  const QString backendPrerequisite =
      availabilityText(context.backend_prerequisite);
  if (!mode.isEmpty() || !backend.isEmpty() || !proton.isEmpty() ||
      !prefix.isEmpty() || !slr.isEmpty() || !backendPrerequisite.isEmpty()) {
    report += QStringLiteral("\n## Launch and runtime\n\n");
    if (!mode.isEmpty()) {
      report += QStringLiteral("- Launch mode: %1\n").arg(mode);
    }
    if (!backend.isEmpty()) {
      report += QStringLiteral("- Effective backend: %1\n").arg(backend);
    }
    if (!proton.isEmpty()) {
      report += QStringLiteral("- Proton launcher: %1\n").arg(proton);
    }
    if (!prefix.isEmpty()) {
      report += QStringLiteral("- Wine prefix: %1\n").arg(prefix);
    }
    if (!slr.isEmpty()) {
      report += QStringLiteral("- Steam Linux Runtime: %1\n").arg(slr);
    }
    if (!backendPrerequisite.isEmpty()) {
      report += QStringLiteral("- Backend prerequisite: %1\n")
                    .arg(backendPrerequisite);
    }
  }

  report += QStringLiteral("\n## Recent preparation\n\n");
  if (!context.last_preparation) {
    report += QObject::tr("No recent preparation summary is available.\n");
    return report;
  }

  report += QObject::tr("Last recorded file catalog preparation in this app session; it may precede a profile or program change.\n\n");
  const auto& preparation = *context.last_preparation;
  const QString phase = preparationPhaseText(preparation.phase);
  if (!phase.isEmpty()) {
    report += QStringLiteral("- Phase: %1\n").arg(phase);
  }
  report += QStringLiteral("- Files checked: %1\n")
                .arg(preparation.files_scanned);
  report += QStringLiteral("- Files hashed: %1\n")
                .arg(preparation.files_hashed);
  report += QStringLiteral("- Bytes hashed: %1\n")
                .arg(preparation.bytes_hashed);
  if (preparation.index_reused.has_value()) {
    report += QStringLiteral("- Existing launch index reused: %1\n")
                  .arg(*preparation.index_reused ? QObject::tr("yes")
                                                 : QObject::tr("no"));
  }
  if (preparation.index_reuse_reason.has_value()) {
    const auto reason = *preparation.index_reuse_reason;
    report += QStringLiteral("- Launch index decision: %1 (%2)\n")
                  .arg(QString::fromLatin1(vfsIndexReuseReasonCode(reason)),
                       QString::fromLatin1(vfsIndexReuseReasonText(reason)));
  }
  return report;
}
} // namespace

DiagnosticReportDialog::DiagnosticReportDialog(DiagnosticReportContext context,
                                               QWidget* parent)
    : QDialog(parent), m_context(std::move(context))
{
  setWindowTitle(tr("Review issue report"));
  resize(1020, 760);

  auto* layout = new QVBoxLayout(this);
  auto* explanation = new QLabel(
      tr("Review the draft before sharing it. No logs are scanned and nothing is sent automatically."),
      this);
  explanation->setWordWrap(true);
  layout->addWidget(explanation);

  auto* categoryRow = new QHBoxLayout;
  auto* categoryLabel = new QLabel(tr("Report category:"), this);
  m_category = new QComboBox(this);
  m_category->setObjectName(QStringLiteral("reportCategoryCombo"));
  categoryLabel->setBuddy(m_category);
  m_category->addItem(categoryText(DiagnosticReportCategory::Bug),
                      static_cast<int>(DiagnosticReportCategory::Bug));
  m_category->addItem(
      categoryText(DiagnosticReportCategory::SlowPreparationOrCacheReuse),
      static_cast<int>(
          DiagnosticReportCategory::SlowPreparationOrCacheReuse));
  m_category->addItem(categoryText(DiagnosticReportCategory::UiOrSetup),
                      static_cast<int>(DiagnosticReportCategory::UiOrSetup));
  categoryRow->addWidget(categoryLabel);
  categoryRow->addWidget(m_category, 1);
  layout->addLayout(categoryRow);

  auto* splitter = new QSplitter(Qt::Horizontal, this);
  auto* editPane = new QWidget(splitter);
  auto* editLayout = new QVBoxLayout(editPane);
  editLayout->setContentsMargins(0, 0, 0, 0);
  auto* problemLabel = new QLabel(tr("What happened?"), editPane);
  m_problem = new QPlainTextEdit(editPane);
  m_problem->setObjectName(QStringLiteral("reportProblemField"));
  problemLabel->setBuddy(m_problem);
  m_problem->setPlaceholderText(tr("Describe the behavior you observed."));
  m_problem->setMinimumHeight(100);
  editLayout->addWidget(problemLabel);
  editLayout->addWidget(m_problem, 1);

  auto* reproductionLabel = new QLabel(tr("Steps to reproduce"), editPane);
  m_reproduction = new QPlainTextEdit(editPane);
  m_reproduction->setObjectName(QStringLiteral("reportReproductionField"));
  reproductionLabel->setBuddy(m_reproduction);
  m_reproduction->setPlaceholderText(tr("List the steps that lead to the problem."));
  m_reproduction->setMinimumHeight(90);
  editLayout->addWidget(reproductionLabel);
  editLayout->addWidget(m_reproduction, 1);

  auto* expectedLabel = new QLabel(tr("Expected result"), editPane);
  m_expected = new QPlainTextEdit(editPane);
  m_expected->setObjectName(QStringLiteral("reportExpectedField"));
  expectedLabel->setBuddy(m_expected);
  m_expected->setPlaceholderText(tr("Describe what you expected to happen."));
  m_expected->setMinimumHeight(80);
  editLayout->addWidget(expectedLabel);
  editLayout->addWidget(m_expected, 1);

  auto* previewPane = new QWidget(splitter);
  auto* previewLayout = new QVBoxLayout(previewPane);
  previewLayout->setContentsMargins(0, 0, 0, 0);
  auto* previewLabel = new QLabel(tr("Preview"), previewPane);
  m_preview = new QTextBrowser(previewPane);
  m_preview->setObjectName(QStringLiteral("reportPreview"));
  m_preview->setOpenLinks(false);
  m_preview->setOpenExternalLinks(false);
  previewLayout->addWidget(previewLabel);
  previewLayout->addWidget(m_preview, 1);
  splitter->addWidget(editPane);
  splitter->addWidget(previewPane);
  splitter->setStretchFactor(0, 1);
  splitter->setStretchFactor(1, 1);
  layout->addWidget(splitter, 1);

  m_status = new QLabel(this);
  m_status->setObjectName(QStringLiteral("reportStatus"));
  m_status->setWordWrap(true);
  m_status->setTextFormat(Qt::PlainText);
  layout->addWidget(m_status);

  auto* buttons = new QDialogButtonBox(this);
  auto* copy = buttons->addButton(tr("Copy report"), QDialogButtonBox::ActionRole);
  copy->setObjectName(QStringLiteral("copyReportButton"));
  auto* save = buttons->addButton(tr("Save report…"), QDialogButtonBox::ActionRole);
  save->setObjectName(QStringLiteral("saveReportButton"));
  auto* openIssue = buttons->addButton(tr("Open issue forms"),
                                       QDialogButtonBox::ActionRole);
  openIssue->setObjectName(QStringLiteral("openIssueFormsButton"));
  auto* close = buttons->addButton(QDialogButtonBox::Close);
  close->setObjectName(QStringLiteral("closeReportButton"));
  layout->addWidget(buttons);

  connect(m_category, qOverload<int>(&QComboBox::currentIndexChanged), this,
          &DiagnosticReportDialog::updatePreview);
  connect(m_problem, &QPlainTextEdit::textChanged, this,
          &DiagnosticReportDialog::updatePreview);
  connect(m_reproduction, &QPlainTextEdit::textChanged, this,
          &DiagnosticReportDialog::updatePreview);
  connect(m_expected, &QPlainTextEdit::textChanged, this,
          &DiagnosticReportDialog::updatePreview);
  connect(copy, &QPushButton::clicked, this, [this] {
    if (QGuiApplication::clipboard() != nullptr) {
      QGuiApplication::clipboard()->setText(reportText());
      m_status->setText(tr("Report copied. Review it before sharing."));
    }
  });
  connect(save, &QPushButton::clicked, this, [this] {
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save issue report"), QString(),
        tr("Markdown report (*.md);;Text file (*.txt);;All files (*)"));
    if (path.isEmpty()) return;
    QString error;
    if (saveReportToFile(path, &error)) {
      m_status->setText(tr("Report saved to %1").arg(path));
    } else {
      QMessageBox::warning(this, tr("Could not save report"), error);
    }
  });
  connect(openIssue, &QPushButton::clicked, this,
          &DiagnosticReportDialog::openIssueFormsRequested);
  connect(close, &QPushButton::clicked, this, &QDialog::accept);

  updatePreview();
}

QString DiagnosticReportDialog::reportText() const
{
  return buildReport(m_context, selectedCategory(m_category),
                     m_problem->toPlainText(), m_reproduction->toPlainText(),
                     m_expected->toPlainText());
}

bool DiagnosticReportDialog::saveReportToFile(const QString& path,
                                              QString* error) const
{
  if (error != nullptr) error->clear();
  if (path.trimmed().isEmpty()) {
    if (error != nullptr) *error = tr("Choose a file name first.");
    return false;
  }

  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    if (error != nullptr) *error = file.errorString();
    return false;
  }
  const QByteArray contents = reportText().toUtf8();
  if (file.write(contents) != contents.size() || !file.commit()) {
    if (error != nullptr) *error = file.errorString();
    return false;
  }
  return true;
}

void DiagnosticReportDialog::updatePreview()
{
  m_preview->setMarkdown(reportText());
}
