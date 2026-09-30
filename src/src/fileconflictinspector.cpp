#include "fileconflictinspector.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStringList>
#include <QVBoxLayout>

namespace
{
QString selectionStateText(FileConflictInspectorState state)
{
  switch (state) {
  case FileConflictInspectorState::EmptySelection:
    return QObject::tr("No file selected");
  case FileConflictInspectorState::MultipleSelection:
    return QObject::tr("Select one file to inspect its conflicts");
  case FileConflictInspectorState::Unknown:
    return QObject::tr("Conflict status is unavailable");
  case FileConflictInspectorState::NoConflict:
    return QObject::tr("No conflict reported");
  case FileConflictInspectorState::Conflict:
    return QObject::tr("Conflict reported");
  }
  return QObject::tr("Conflict status is unknown");
}

QString providerMarkers(const FileConflictProviderSnapshot& provider)
{
  if (provider.isWinner && provider.isCurrentMod) {
    return QObject::tr("Current winner · Current mod");
  }
  if (provider.isWinner) return QObject::tr("Current winner");
  if (provider.isCurrentMod) return QObject::tr("Current mod · Does not currently win");
  return QObject::tr("Other active provider");
}

QString providerSourceText(const FileConflictProviderSnapshot& provider)
{
  switch (provider.sourceKind) {
  case FileConflictSourceKind::Loose:
    return QObject::tr("Loose file");
  case FileConflictSourceKind::Archive:
    return provider.archiveName.isEmpty()
               ? QObject::tr("Archive entry · archive name not supplied")
               : QObject::tr("Archive entry · %1").arg(provider.archiveName);
  case FileConflictSourceKind::Unknown:
    return provider.archiveName.isEmpty()
               ? QObject::tr("Source kind unknown")
               : QObject::tr("Source kind unknown · archive: %1")
                     .arg(provider.archiveName);
  }
  return QObject::tr("Source kind unknown");
}

void makePlainText(QLabel* label)
{
  label->setTextFormat(Qt::PlainText);
  label->setWordWrap(true);
}
} // namespace

FileConflictInspector::FileConflictInspector(QWidget* parent) : QWidget(parent)
{
  setObjectName(QStringLiteral("fileConflictInspector"));
  setAccessibleName(tr("Selected file conflict inspector"));
  auto* layout = new QVBoxLayout(this);

  auto* heading = new QLabel(tr("Selected file conflict"), this);
  heading->setObjectName(QStringLiteral("fileConflictHeading"));
  makePlainText(heading);
  QFont headingFont = heading->font();
  headingFont.setBold(true);
  heading->setFont(headingFont);
  layout->addWidget(heading);

  m_stateLabel = new QLabel(this);
  m_stateLabel->setObjectName(QStringLiteral("fileConflictState"));
  makePlainText(m_stateLabel);
  m_stateLabel->setAccessibleName(tr("Conflict inspection state"));
  layout->addWidget(m_stateLabel);

  m_pathLabel = new QLabel(this);
  m_pathLabel->setObjectName(QStringLiteral("fileConflictPath"));
  makePlainText(m_pathLabel);
  m_pathLabel->setAccessibleName(tr("Selected file path"));
  layout->addWidget(m_pathLabel);

  m_currentModLabel = new QLabel(this);
  m_currentModLabel->setObjectName(QStringLiteral("fileConflictCurrentMod"));
  makePlainText(m_currentModLabel);
  m_currentModLabel->setAccessibleName(tr("Selected mod"));
  layout->addWidget(m_currentModLabel);

  m_detailLabel = new QLabel(this);
  m_detailLabel->setObjectName(QStringLiteral("fileConflictDetail"));
  makePlainText(m_detailLabel);
  layout->addWidget(m_detailLabel);

  m_ruleLabel = new QLabel(this);
  m_ruleLabel->setObjectName(QStringLiteral("fileConflictWinnerRule"));
  makePlainText(m_ruleLabel);
  m_ruleLabel->setAccessibleName(tr("How the current winner is determined"));
  layout->addWidget(m_ruleLabel);

  m_providerHeading = new QLabel(tr("Active sources for this file"), this);
  m_providerHeading->setObjectName(QStringLiteral("fileConflictProviderHeading"));
  makePlainText(m_providerHeading);
  QFont providerFont = m_providerHeading->font();
  providerFont.setBold(true);
  m_providerHeading->setFont(providerFont);
  layout->addWidget(m_providerHeading);

  m_providerScrollArea = new QScrollArea(this);
  m_providerScrollArea->setObjectName(QStringLiteral("fileConflictProviderScrollArea"));
  m_providerScrollArea->setWidgetResizable(true);
  m_providerContainer = new QWidget(m_providerScrollArea);
  m_providerLayout = new QVBoxLayout(m_providerContainer);
  m_providerLayout->setContentsMargins(0, 0, 0, 0);
  m_providerLayout->setSpacing(4);
  m_providerScrollArea->setWidget(m_providerContainer);
  m_providerScrollArea->setMinimumHeight(90);
  layout->addWidget(m_providerScrollArea, 1);

  refresh();
}

void FileConflictInspector::setSnapshot(const FileConflictSnapshot& snapshot)
{
  m_snapshot = snapshot;
  refresh();
}

void FileConflictInspector::clearProviders()
{
  while (QLayoutItem* item = m_providerLayout->takeAt(0)) {
    if (QWidget* widget = item->widget()) delete widget;
    delete item;
  }
  m_noProvidersLabel = nullptr;
}

QString FileConflictInspector::winnerRuleExplanation() const
{
  switch (m_snapshot.state) {
  case FileConflictInspectorState::EmptySelection:
    return {};
  case FileConflictInspectorState::MultipleSelection:
    return {};
  case FileConflictInspectorState::Unknown:
    return tr("Some source details are unavailable. The current winner marker comes "
              "from the file's current directory state.");
  case FileConflictInspectorState::NoConflict:
    break;
  case FileConflictInspectorState::Conflict:
    break;
  }

  bool hasLoose = false;
  bool hasArchive = false;
  bool hasUnknown = false;
  for (const FileConflictProviderSnapshot& provider : m_snapshot.providers) {
    switch (provider.sourceKind) {
    case FileConflictSourceKind::Loose: hasLoose = true; break;
    case FileConflictSourceKind::Archive: hasArchive = true; break;
    case FileConflictSourceKind::Unknown: hasUnknown = true; break;
    }
  }

  QString rule;
  if (hasUnknown || (!hasLoose && !hasArchive)) {
    rule = tr("Some source details are unavailable, so this view cannot explain how "
              "the current winner was chosen.");
  } else if (hasLoose && hasArchive) {
    rule = tr("Loose files take precedence over archive contents. Between two loose "
              "files, the higher mod priority wins. Between archive contents, archive "
              "order follows plugin load order.");
  } else if (hasLoose) {
    rule = tr("These are loose files. The higher mod priority wins between them.");
  } else {
    rule = tr("These are archive contents. Archive order follows plugin load order "
              "to determine which archive wins.");
  }

  if (m_snapshot.state == FileConflictInspectorState::NoConflict) {
    rule.prepend(tr("The current file state reports no conflict for this file. "));
  }
  rule += tr(" The current winner marker shows the directory's result. Changing mod "
             "priority can change loose-file winners.");
  return rule;
}

void FileConflictInspector::refresh()
{
  const bool hasSingleSelection =
      m_snapshot.state != FileConflictInspectorState::EmptySelection &&
      m_snapshot.state != FileConflictInspectorState::MultipleSelection;
  m_stateLabel->setText(selectionStateText(m_snapshot.state));
  m_pathLabel->setText(m_snapshot.relativePath.isEmpty()
                           ? tr("File path: not supplied")
                           : tr("File: %1").arg(m_snapshot.relativePath));
  m_currentModLabel->setText(m_snapshot.currentModName.isEmpty()
                                 ? tr("Current mod: not supplied")
                                 : tr("Current mod: %1").arg(m_snapshot.currentModName));
  m_detailLabel->setText(m_snapshot.detail);
  m_detailLabel->setVisible(hasSingleSelection && !m_snapshot.detail.isEmpty());
  m_ruleLabel->setText(winnerRuleExplanation());
  m_pathLabel->setVisible(hasSingleSelection);
  m_currentModLabel->setVisible(hasSingleSelection);
  m_ruleLabel->setVisible(hasSingleSelection);
  m_providerHeading->setVisible(hasSingleSelection);
  m_providerScrollArea->setVisible(hasSingleSelection);

  clearProviders();
  if (hasSingleSelection && m_snapshot.providers.isEmpty()) {
    m_noProvidersLabel = new QLabel(tr("No active source details are available."),
                                    m_providerContainer);
    m_noProvidersLabel->setObjectName(QStringLiteral("fileConflictNoProviders"));
    makePlainText(m_noProvidersLabel);
    m_providerLayout->addWidget(m_noProvidersLabel);
  }

  for (qsizetype i = 0; i < m_snapshot.providers.size(); ++i) {
    const FileConflictProviderSnapshot& provider = m_snapshot.providers.at(i);
    const QString indexText = QString::number(static_cast<qlonglong>(i));
    auto* card = new QFrame(m_providerContainer);
    card->setObjectName(QStringLiteral("fileConflictProvider_%1").arg(indexText));
    card->setFrameShape(QFrame::StyledPanel);
    card->setFrameShadow(QFrame::Plain);
    card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(8, 6, 8, 6);
    cardLayout->setSpacing(2);

    auto* name = new QLabel(provider.modName.isEmpty()
                                ? tr("Mod name not supplied")
                                : provider.modName,
                            card);
    name->setObjectName(QStringLiteral("fileConflictProviderName_%1").arg(indexText));
    makePlainText(name);
    QFont nameFont = name->font();
    nameFont.setBold(true);
    name->setFont(nameFont);
    cardLayout->addWidget(name);

    auto* markers = new QLabel(providerMarkers(provider), card);
    markers->setObjectName(QStringLiteral("fileConflictProviderMarkers_%1").arg(indexText));
    makePlainText(markers);
    cardLayout->addWidget(markers);

    auto* source = new QLabel(providerSourceText(provider), card);
    source->setObjectName(QStringLiteral("fileConflictProviderSource_%1").arg(indexText));
    makePlainText(source);
    cardLayout->addWidget(source);

    QStringList orderDetails;
    if (provider.sourceKind == FileConflictSourceKind::Loose &&
        provider.modPriority.has_value()) {
      orderDetails.append(tr("Mod priority: %1").arg(*provider.modPriority));
    } else if (provider.sourceKind == FileConflictSourceKind::Archive &&
               provider.archiveOrder.has_value()) {
      orderDetails.append(tr("Archive order: %1").arg(*provider.archiveOrder));
    }
    if (!orderDetails.isEmpty()) {
      auto* order = new QLabel(orderDetails.join(QStringLiteral(" · ")), card);
      order->setObjectName(QStringLiteral("fileConflictProviderOrder_%1").arg(indexText));
      makePlainText(order);
      cardLayout->addWidget(order);
    }

    QString accessibleDescription = providerMarkers(provider) + QStringLiteral(". ") +
                                    providerSourceText(provider);
    if (!orderDetails.isEmpty()) {
      accessibleDescription += QStringLiteral(". ") + orderDetails.join(QStringLiteral(". "));
    }
    card->setAccessibleName(
        tr("Provider %1: %2")
            .arg(QString::number(static_cast<qlonglong>(i + 1)))
            .arg(name->text()));
    card->setAccessibleDescription(accessibleDescription);
    m_providerLayout->addWidget(card);
  }
  m_providerLayout->addStretch();
}
