#include "setupchecks.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
QString itemName(SetupCheckItem item)
{
  switch (item) {
  case SetupCheckItem::GameFolder: return QStringLiteral("gameFolder");
  case SetupCheckItem::Executable: return QStringLiteral("executable");
  case SetupCheckItem::ProtonLauncher: return QStringLiteral("protonLauncher");
  case SetupCheckItem::WinePrefix: return QStringLiteral("winePrefix");
  case SetupCheckItem::SteamLinuxRuntime: return QStringLiteral("steamLinuxRuntime");
  case SetupCheckItem::VirtualFilesystem: return QStringLiteral("virtualFilesystem");
  }
  return QStringLiteral("unknown");
}

QString stateText(SetupCheckState state)
{
  switch (state) {
  case SetupCheckState::Passing: return QObject::tr("Passing");
  case SetupCheckState::NeedsAttention: return QObject::tr("Needs attention");
  case SetupCheckState::NotRequired: return QObject::tr("Not required");
  case SetupCheckState::Unchecked: return QObject::tr("Unchecked");
  }
  return QObject::tr("Unchecked");
}

QString actionText(SetupCheckAction action)
{
  switch (action) {
  case SetupCheckAction::Paths: return QObject::tr("Open Paths settings");
  case SetupCheckAction::Compatibility:
    return QObject::tr("Open Compatibility settings");
  case SetupCheckAction::Executables:
    return QObject::tr("Open Executables settings");
  case SetupCheckAction::None: return {};
  }
  return {};
}

QString selectedExecutablePath(const SetupCheckInputs& inputs)
{
  QString path = inputs.selectedExecutablePath.trimmed();
  if (path.isEmpty()) return {};

  const QFileInfo info(path);
  if (info.isRelative() && !inputs.gameFolderPath.trimmed().isEmpty()) {
    path = QDir(inputs.gameFolderPath).filePath(path);
  }
  return QDir::cleanPath(path);
}

bool hasMzHeader(const QString& path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) && file.read(2) == QByteArrayLiteral("MZ");
}

enum class ProtonNeed
{
  Required,
  NotRequired,
  Unknown
};

ProtonNeed protonNeed(const SetupCheckInputs& inputs)
{
  if (!inputs.useProton.has_value()) return ProtonNeed::Unknown;
  return *inputs.useProton ? ProtonNeed::Required : ProtonNeed::NotRequired;
}

SetupCheckRow makeRow(SetupCheckItem item, const QString& title,
                      SetupCheckState state, const QString& detail,
                      SetupCheckAction action = SetupCheckAction::None)
{
  return {item, title, state, detail, action};
}

QString statusSummary(const QList<SetupCheckRow>& rows)
{
  int passing = 0;
  int needsAttention = 0;
  int notRequired = 0;
  int unchecked = 0;
  for (const auto& row : rows) {
    switch (row.state) {
    case SetupCheckState::Passing: ++passing; break;
    case SetupCheckState::NeedsAttention: ++needsAttention; break;
    case SetupCheckState::NotRequired: ++notRequired; break;
    case SetupCheckState::Unchecked: ++unchecked; break;
    }
  }

  return QObject::tr("Basic setup checks: %1 passing, %2 need attention, "
                     "%3 unchecked, %4 not required.")
      .arg(passing)
      .arg(needsAttention)
      .arg(unchecked)
      .arg(notRequired);
}
} // namespace

QList<SetupCheckRow> SetupCheckModel::evaluate(const SetupCheckInputs& inputs)
{
  QList<SetupCheckRow> rows;

  const QString gameFolderPath = inputs.gameFolderPath.trimmed();
  if (gameFolderPath.isEmpty()) {
    rows.append(makeRow(SetupCheckItem::GameFolder, QObject::tr("Game folder"),
                        SetupCheckState::NeedsAttention,
                        QObject::tr("No game folder is selected."),
                        SetupCheckAction::Paths));
  } else {
    const QFileInfo gameFolder(gameFolderPath);
    if (!gameFolder.exists()) {
      rows.append(makeRow(SetupCheckItem::GameFolder, QObject::tr("Game folder"),
                          SetupCheckState::NeedsAttention,
                          QObject::tr("The selected game folder does not exist: %1")
                              .arg(gameFolderPath),
                          SetupCheckAction::Paths));
    } else if (!gameFolder.isDir()) {
      rows.append(makeRow(SetupCheckItem::GameFolder, QObject::tr("Game folder"),
                          SetupCheckState::NeedsAttention,
                          QObject::tr("The selected game path is not a folder."),
                          SetupCheckAction::Paths));
    } else if (!gameFolder.isReadable()) {
      rows.append(makeRow(SetupCheckItem::GameFolder, QObject::tr("Game folder"),
                          SetupCheckState::NeedsAttention,
                          QObject::tr("The game folder exists but is not readable."),
                          SetupCheckAction::Paths));
    } else {
      rows.append(makeRow(SetupCheckItem::GameFolder, QObject::tr("Game folder"),
                          SetupCheckState::Passing,
                          QObject::tr("The folder exists and is readable.")));
    }
  }

  const QString executablePath = selectedExecutablePath(inputs);
  if (executablePath.isEmpty()) {
    rows.append(makeRow(SetupCheckItem::Executable, QObject::tr("Selected executable"),
                        SetupCheckState::NeedsAttention,
                        QObject::tr("No executable file path is selected."),
                        SetupCheckAction::Executables));
  } else {
    const QFileInfo executable(executablePath);
    if (!executable.exists()) {
      rows.append(makeRow(SetupCheckItem::Executable,
                          QObject::tr("Selected executable"),
                          SetupCheckState::NeedsAttention,
                          QObject::tr("The selected file does not exist: %1")
                              .arg(executablePath),
                          SetupCheckAction::Executables));
    } else if (!executable.isFile()) {
      rows.append(makeRow(SetupCheckItem::Executable,
                          QObject::tr("Selected executable"),
                          SetupCheckState::NeedsAttention,
                          QObject::tr("The selected executable path is not a file."),
                          SetupCheckAction::Executables));
    } else if (!executable.isReadable()) {
      rows.append(makeRow(SetupCheckItem::Executable,
                          QObject::tr("Selected executable"),
                          SetupCheckState::NeedsAttention,
                          QObject::tr("The selected file exists but is not readable."),
                          SetupCheckAction::Executables));
    } else if (inputs.useProton.has_value() && !*inputs.useProton &&
               hasMzHeader(executablePath)) {
      rows.append(makeRow(SetupCheckItem::Executable,
                          QObject::tr("Selected executable"),
                          SetupCheckState::NeedsAttention,
                          QObject::tr("The selected file has a Windows executable "
                                      "header, but Use Proton is off. Enable it in "
                                      "Edit Executables."),
                          SetupCheckAction::Executables));
    } else if (inputs.useProton.has_value() && !*inputs.useProton &&
               !executable.isExecutable()) {
      rows.append(makeRow(SetupCheckItem::Executable,
                          QObject::tr("Selected executable"),
                          SetupCheckState::NeedsAttention,
                          QObject::tr("The selected file is readable but lacks "
                                      "execute permission."),
                          SetupCheckAction::Executables));
    } else {
      rows.append(makeRow(SetupCheckItem::Executable,
                          QObject::tr("Selected executable"),
                          SetupCheckState::Passing,
                          QObject::tr("The selected file exists and is readable.")));
    }
  }

  const ProtonNeed needsProton = protonNeed(inputs);

  if (needsProton == ProtonNeed::NotRequired) {
    rows.append(makeRow(SetupCheckItem::ProtonLauncher,
                        QObject::tr("Proton launcher"),
                        SetupCheckState::NotRequired,
                        QObject::tr("Use Proton is off for the selected executable.")));
    rows.append(makeRow(SetupCheckItem::WinePrefix, QObject::tr("Wine prefix"),
                        SetupCheckState::NotRequired,
                        QObject::tr("The selected executable does not use a Proton prefix.")));
    rows.append(makeRow(SetupCheckItem::SteamLinuxRuntime,
                        QObject::tr("Steam Linux Runtime"),
                        SetupCheckState::NotRequired,
                        QObject::tr("The selected executable does not use Proton.")));
  } else if (needsProton == ProtonNeed::Unknown) {
    const QString unknownMode = QObject::tr(
        "Use Proton for the selected executable is not available, so this check was not assessed.");
    rows.append(makeRow(SetupCheckItem::ProtonLauncher,
                        QObject::tr("Proton launcher"), SetupCheckState::Unchecked,
                        unknownMode, SetupCheckAction::Executables));
    rows.append(makeRow(SetupCheckItem::WinePrefix, QObject::tr("Wine prefix"),
                        SetupCheckState::Unchecked, unknownMode,
                        SetupCheckAction::Executables));
    rows.append(makeRow(SetupCheckItem::SteamLinuxRuntime,
                        QObject::tr("Steam Linux Runtime"),
                        SetupCheckState::Unchecked, unknownMode,
                        SetupCheckAction::Executables));
  } else {
    QString launcherPath = inputs.protonInstallationPath.trimmed();
    if (launcherPath.isEmpty()) {
      rows.append(makeRow(SetupCheckItem::ProtonLauncher,
                          QObject::tr("Proton launcher"),
                          SetupCheckState::NeedsAttention,
                          QObject::tr("No Proton installation or launcher is configured."),
                          SetupCheckAction::Compatibility));
    } else {
      const QFileInfo protonLocation(launcherPath);
      if (protonLocation.isDir()) {
        launcherPath = QDir(launcherPath).filePath(QStringLiteral("proton"));
      }
      const QFileInfo launcher(launcherPath);
      if (!launcher.exists() || !launcher.isFile()) {
        rows.append(makeRow(SetupCheckItem::ProtonLauncher,
                            QObject::tr("Proton launcher"),
                            SetupCheckState::NeedsAttention,
                            QObject::tr("The Proton launcher is missing: %1")
                                .arg(launcherPath),
                            SetupCheckAction::Compatibility));
      } else if (!launcher.isReadable() || !launcher.isExecutable()) {
        rows.append(makeRow(SetupCheckItem::ProtonLauncher,
                            QObject::tr("Proton launcher"),
                            SetupCheckState::NeedsAttention,
                            QObject::tr("The Proton launcher exists but is not "
                                        "readable and executable."),
                            SetupCheckAction::Compatibility));
      } else {
        rows.append(makeRow(SetupCheckItem::ProtonLauncher,
                            QObject::tr("Proton launcher"),
                            SetupCheckState::Passing,
                            QObject::tr("The launcher exists and is readable and "
                                        "executable. It was not launched or tested.")));
      }
    }

    const QString prefixPath = inputs.prefixPath.trimmed();
    if (prefixPath.isEmpty()) {
      rows.append(makeRow(SetupCheckItem::WinePrefix, QObject::tr("Wine prefix"),
                          SetupCheckState::NeedsAttention,
                          QObject::tr("No prefix folder is configured."),
                          SetupCheckAction::Compatibility));
    } else {
      const QFileInfo prefix(prefixPath);
      if (!prefix.exists() || !prefix.isDir()) {
        rows.append(makeRow(SetupCheckItem::WinePrefix,
                            QObject::tr("Wine prefix"),
                            SetupCheckState::NeedsAttention,
                            QObject::tr("The configured prefix folder is missing: %1")
                                .arg(prefixPath),
                            SetupCheckAction::Compatibility));
      } else {
        QStringList missingFiles;
        const QFileInfo driveC(QDir(prefixPath).filePath(QStringLiteral("drive_c")));
        if (!driveC.isDir() || !driveC.isReadable()) {
          missingFiles.append(QStringLiteral("drive_c/"));
        }
        for (const QString& registryFile : {QStringLiteral("system.reg"),
                                            QStringLiteral("user.reg")}) {
          const QFileInfo file(QDir(prefixPath).filePath(registryFile));
          if (!file.isFile() || !file.isReadable()) missingFiles.append(registryFile);
        }
        if (!missingFiles.isEmpty()) {
          rows.append(makeRow(SetupCheckItem::WinePrefix,
                              QObject::tr("Wine prefix"),
                              SetupCheckState::NeedsAttention,
                              QObject::tr("Basic prefix files are missing or unreadable: %1")
                                  .arg(missingFiles.join(QStringLiteral(", "))),
                              SetupCheckAction::Compatibility));
        } else {
          rows.append(makeRow(SetupCheckItem::WinePrefix,
                              QObject::tr("Wine prefix"),
                              SetupCheckState::Passing,
                              QObject::tr("drive_c/, system.reg, and user.reg are "
                                          "present and readable. Prefix contents and "
                                          "launch behavior were not tested.")));
        }
      }
    }

    if (!inputs.steamRuntimeAvailable.has_value()) {
      rows.append(makeRow(SetupCheckItem::SteamLinuxRuntime,
                          QObject::tr("Steam Linux Runtime"),
                          SetupCheckState::Unchecked,
                          QObject::tr("Managed runtime availability was not supplied "
                                      "and was not checked.")));
    } else if (*inputs.steamRuntimeAvailable) {
      rows.append(makeRow(SetupCheckItem::SteamLinuxRuntime,
                          QObject::tr("Steam Linux Runtime"),
                          SetupCheckState::Passing,
                          QObject::tr("The managed Steam Linux Runtime is present.")));
    } else {
      rows.append(makeRow(SetupCheckItem::SteamLinuxRuntime,
                          QObject::tr("Steam Linux Runtime"),
                          SetupCheckState::NeedsAttention,
                          QObject::tr("The managed Steam Linux Runtime is missing."),
                          SetupCheckAction::Compatibility));
    }
  }

  const QString backendName = inputs.effectiveBackendDescription.trimmed().isEmpty()
                                  ? QObject::tr("Effective virtual filesystem backend")
                                  : inputs.effectiveBackendDescription.trimmed();
  if (!inputs.backendRequired) {
    rows.append(makeRow(SetupCheckItem::VirtualFilesystem, backendName,
                        SetupCheckState::NotRequired,
                        inputs.backendStatusDetail.trimmed().isEmpty()
                            ? QObject::tr("No external backend prerequisite is required "
                                          "for this launch.")
                            : inputs.backendStatusDetail.trimmed()));
  } else if (!inputs.backendPrerequisiteAvailable.has_value()) {
    QString detail = QObject::tr("Backend availability was not checked.");
    if (!inputs.backendStatusDetail.trimmed().isEmpty()) {
      detail += QStringLiteral(" ") + inputs.backendStatusDetail.trimmed();
    }
    if (!inputs.backendPrerequisitePath.trimmed().isEmpty()) {
      detail += QObject::tr(" Prerequisite: %1.")
                    .arg(inputs.backendPrerequisitePath.trimmed());
    }
    rows.append(makeRow(SetupCheckItem::VirtualFilesystem, backendName,
                        SetupCheckState::Unchecked, detail));
  } else if (*inputs.backendPrerequisiteAvailable) {
    QString detail = inputs.backendStatusDetail.trimmed().isEmpty()
                         ? QObject::tr("The supplied prerequisite check passed.")
                         : inputs.backendStatusDetail.trimmed();
    rows.append(makeRow(SetupCheckItem::VirtualFilesystem, backendName,
                        SetupCheckState::Passing, detail));
  } else {
    QString detail = inputs.backendStatusDetail.trimmed().isEmpty()
                         ? QObject::tr("The supplied prerequisite check reported a problem.")
                         : inputs.backendStatusDetail.trimmed();
    if (!inputs.backendPrerequisitePath.trimmed().isEmpty()) {
      detail += QObject::tr(" Prerequisite: %1.")
                    .arg(inputs.backendPrerequisitePath.trimmed());
    }
    rows.append(makeRow(SetupCheckItem::VirtualFilesystem, backendName,
                        SetupCheckState::NeedsAttention, detail,
                        SetupCheckAction::Compatibility));
  }

  return rows;
}

SetupChecksDialog::SetupChecksDialog(QWidget* parent) : QDialog(parent)
{
  setWindowTitle(tr("Setup checks"));
  setMinimumSize(520, 350);
  resize(780, 560);

  auto* layout = new QVBoxLayout(this);
  auto* heading = new QLabel(tr("Basic setup checks"), this);
  heading->setTextFormat(Qt::PlainText);
  QFont headingFont = heading->font();
  headingFont.setBold(true);
  headingFont.setPointSize(headingFont.pointSize() + 1);
  heading->setFont(headingFont);
  layout->addWidget(heading);

  auto* scope = new QLabel(
      tr("Check the selected program's required folders, files and permissions. "
         "Full compatibility is checked when the program launches."), this);
  scope->setTextFormat(Qt::PlainText);
  scope->setWordWrap(true);
  scope->setObjectName(QStringLiteral("setupChecksScope"));
  layout->addWidget(scope);

  m_programLabel = new QLabel(this);
  m_programLabel->setObjectName(QStringLiteral("setupChecksProgram"));
  m_programLabel->setTextFormat(Qt::PlainText);
  m_programLabel->setWordWrap(true);
  layout->addWidget(m_programLabel);

  m_summaryLabel = new QLabel(this);
  m_summaryLabel->setObjectName(QStringLiteral("setupChecksSummary"));
  m_summaryLabel->setTextFormat(Qt::PlainText);
  m_summaryLabel->setWordWrap(true);
  layout->addWidget(m_summaryLabel);

  m_scrollArea = new QScrollArea(this);
  m_scrollArea->setObjectName(QStringLiteral("setupCheckScrollArea"));
  m_scrollArea->setWidgetResizable(true);
  m_rowsContainer = new QWidget(m_scrollArea);
  m_rowsLayout = new QVBoxLayout(m_rowsContainer);
  m_rowsLayout->setContentsMargins(0, 0, 0, 0);
  m_rowsLayout->setSpacing(4);
  m_scrollArea->setWidget(m_rowsContainer);
  layout->addWidget(m_scrollArea, 1);

  auto* buttons = new QHBoxLayout;
  buttons->addStretch();
  auto* recheck = new QPushButton(tr("Re-check"), this);
  recheck->setObjectName(QStringLiteral("setupChecksRecheck"));
  connect(recheck, &QPushButton::clicked, this,
          &SetupChecksDialog::recheckRequested);
  buttons->addWidget(recheck);
  auto* close = new QPushButton(tr("Close"), this);
  close->setObjectName(QStringLiteral("setupChecksClose"));
  connect(close, &QPushButton::clicked, this, &QDialog::accept);
  buttons->addWidget(close);
  layout->addLayout(buttons);
}

void SetupChecksDialog::setInputs(const SetupCheckInputs& inputs)
{
  m_inputs = inputs;
  refreshRows();
}

void SetupChecksDialog::refreshRows()
{
  m_rows = SetupCheckModel::evaluate(m_inputs);
  const QString programName = m_inputs.selectedExecutableName.trimmed().isEmpty()
                                  ? tr("Not selected")
                                  : m_inputs.selectedExecutableName.trimmed();
  m_programLabel->setText(tr("Program: %1").arg(programName));
  m_summaryLabel->setText(statusSummary(m_rows));
  while (QLayoutItem* layoutItem = m_rowsLayout->takeAt(0)) {
    if (QWidget* widget = layoutItem->widget()) delete widget;
    delete layoutItem;
  }

  for (const SetupCheckRow& row : m_rows) {
    auto* contents = new QWidget(m_rowsContainer);
    contents->setObjectName(QStringLiteral("setupCheckRow_%1").arg(itemName(row.item)));
    contents->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    auto* rowLayout = new QVBoxLayout(contents);
    rowLayout->setContentsMargins(8, 7, 8, 7);
    rowLayout->setSpacing(4);

    auto* detailRow = new QHBoxLayout;
    detailRow->setContentsMargins(0, 0, 0, 0);
    detailRow->setSpacing(8);

    auto* status = new QLabel(stateText(row.state), contents);
    status->setObjectName(QStringLiteral("setupCheckState_%1").arg(itemName(row.item)));
    status->setTextFormat(Qt::PlainText);
    status->setMinimumWidth(100);
    status->setWordWrap(true);
    status->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    QFont statusFont = status->font();
    statusFont.setBold(true);
    status->setFont(statusFont);
    // The state is written out in text so meaning does not depend on color or
    // a platform-specific status icon.
    status->setAccessibleName(stateText(row.state));
    detailRow->addWidget(status, 0, Qt::AlignTop);

    auto* textColumn = new QVBoxLayout;
    textColumn->setContentsMargins(0, 0, 0, 0);
    textColumn->setSpacing(2);
    auto* title = new QLabel(row.title, contents);
    title->setObjectName(QStringLiteral("setupCheckTitle_%1").arg(itemName(row.item)));
    title->setTextFormat(Qt::PlainText);
    QFont titleFont = title->font();
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setWordWrap(true);
    textColumn->addWidget(title);
    auto* detail = new QLabel(row.detail, contents);
    detail->setObjectName(QStringLiteral("setupCheckDetail_%1").arg(itemName(row.item)));
    detail->setTextFormat(Qt::PlainText);
    detail->setWordWrap(true);
    textColumn->addWidget(detail);
    detailRow->addLayout(textColumn, 1);
    rowLayout->addLayout(detailRow);

    if (row.action != SetupCheckAction::None) {
      auto* actionRow = new QHBoxLayout;
      actionRow->setContentsMargins(0, 0, 0, 0);
      actionRow->addStretch();
      auto* action = new QPushButton(actionText(row.action), contents);
      action->setObjectName(QStringLiteral("setupCheckAction_%1").arg(itemName(row.item)));
      action->setToolTip(actionText(row.action));
      connect(action, &QPushButton::clicked, this,
              [this, requested = row.action] { emit actionRequested(requested); });
      actionRow->addWidget(action);
      rowLayout->addLayout(actionRow);
    }

    m_rowsLayout->addWidget(contents);
  }
  m_rowsLayout->addStretch();
}
