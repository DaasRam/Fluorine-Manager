/*
Copyright (C) 2026 Fluorine contributors. All rights reserved.

This file is part of Fluorine.

Fluorine is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Fluorine is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Fluorine.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "workspaceframe.h"

#include <QAction>
#include <QBoxLayout>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMargins>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStyle>
#include <QTabBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

WorkspaceFrame::WorkspaceFrame(QWidget* parent) : QWidget(parent)
{
  setObjectName(QStringLiteral("fluorineWorkspaceFrame"));

  m_frameLayout = new QVBoxLayout(this);
  m_frameLayout->setContentsMargins(0, 0, 0, 0);
  m_frameLayout->setSpacing(0);

  m_header = new QWidget(this);
  m_header->setObjectName(QStringLiteral("workspaceHeader"));
  // Do not let the inline controls become the window's minimum width: the
  // header can reflow them after a resize. Its height is computed below.
  m_header->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Minimum);
  m_headerLayout = new QVBoxLayout(m_header);
  m_headerLayout->setContentsMargins(16, 6, 16, 6);
  m_headerLayout->setSpacing(2);

  m_identityRow = new QWidget(m_header);
  m_identityLayout = new QHBoxLayout(m_identityRow);
  m_identityLayout->setContentsMargins(0, 0, 0, 0);
  m_identityLayout->setSpacing(8);

  m_libraryButton = new QToolButton(m_identityRow);
  m_libraryButton->setObjectName(QStringLiteral("workspaceLibraryButton"));
  m_libraryButton->setText(tr("Library"));
  m_libraryButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  m_libraryButton->setAutoRaise(true);
  m_libraryButton->setAccessibleName(tr("Library"));
  m_libraryButton->setFocusPolicy(Qt::StrongFocus);
  m_libraryButton->hide();
  m_identityLayout->addWidget(m_libraryButton, 0, Qt::AlignVCenter);

  m_headerDivider = new QFrame(m_identityRow);
  m_headerDivider->setObjectName(QStringLiteral("workspaceHeaderDivider"));
  m_headerDivider->setFrameShape(QFrame::VLine);
  m_headerDivider->setFrameShadow(QFrame::Plain);
  m_headerDivider->setLineWidth(1);
  m_headerDivider->setFixedWidth(1);
  m_headerDivider->hide();
  m_identityLayout->addWidget(m_headerDivider, 0, Qt::AlignVCenter);

  m_context = new QWidget(m_identityRow);
  m_context->setObjectName(QStringLiteral("workspaceContext"));
  m_context->setMinimumWidth(128);
  m_context->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  auto* contextLayout = new QVBoxLayout(m_context);
  contextLayout->setContentsMargins(0, 0, 0, 0);
  contextLayout->setSpacing(0);

  m_setupLabel = new QLabel(m_context);
  m_setupLabel->setObjectName(QStringLiteral("workspaceSetupName"));
  m_setupLabel->setTextFormat(Qt::PlainText);
  m_setupLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  m_gameLabel = new QLabel(m_context);
  m_gameLabel->setObjectName(QStringLiteral("workspaceGameName"));
  m_gameLabel->setTextFormat(Qt::PlainText);
  m_gameLabel->setProperty("secondary", true);
  m_gameLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  contextLayout->addWidget(m_setupLabel);
  contextLayout->addWidget(m_gameLabel);
  m_identityLayout->addWidget(m_context, 1);

  m_controlsRow = new QWidget(m_header);
  m_controlsRow->setObjectName(QStringLiteral("workspaceHeaderControls"));
  m_controlsRow->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  m_controlsLayout = new QBoxLayout(QBoxLayout::LeftToRight, m_controlsRow);
  m_controlsLayout->setContentsMargins(0, 0, 0, 0);
  m_controlsLayout->setSpacing(8);
  m_controlsLayout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  m_controlsRow->hide();

  m_headerLayout->addWidget(m_identityRow);
  m_headerLayout->addWidget(m_controlsRow);

  m_navigation = new QWidget(this);
  m_navigation->setObjectName(QStringLiteral("workspaceNavigation"));
  m_navigation->setAccessibleName(tr("Workspace destinations"));
  m_navigation->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
  m_navigationLayout = new QHBoxLayout(m_navigation);
  m_navigationLayout->setContentsMargins(12, 0, 12, 0);
  m_navigationLayout->setSpacing(8);

  m_tabs = new QTabBar(m_navigation);
  m_tabs->setObjectName(QStringLiteral("workspaceDestinations"));
  m_tabs->setAccessibleName(tr("Workspace destinations"));
  m_tabs->setFocusPolicy(Qt::StrongFocus);
  m_tabs->setDocumentMode(true);
  m_tabs->setDrawBase(false);
  m_tabs->setExpanding(false);
  m_tabs->setMovable(false);
  m_tabs->setUsesScrollButtons(true);
  m_tabs->setElideMode(Qt::ElideRight);
  m_tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  m_navigationLayout->addWidget(m_tabs, 1);

  m_navigationExtras = new QWidget(m_navigation);
  m_navigationExtras->setObjectName(QStringLiteral("workspaceNavigationExtras"));
  m_navigationExtrasLayout = new QHBoxLayout(m_navigationExtras);
  m_navigationExtrasLayout->setContentsMargins(0, 0, 0, 0);
  m_navigationExtrasLayout->setSpacing(6);
  m_navigationExtras->hide();
  m_navigationLayout->addWidget(m_navigationExtras, 0, Qt::AlignRight | Qt::AlignVCenter);

  m_contentHost = new QWidget(this);
  m_contentHost->setObjectName(QStringLiteral("workspaceContent"));
  m_contentHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  m_contentLayout = new QVBoxLayout(m_contentHost);
  m_contentLayout->setContentsMargins(0, 0, 0, 0);
  m_contentLayout->setSpacing(0);

  m_frameLayout->addWidget(m_header);
  m_frameLayout->addWidget(m_navigation);
  m_frameLayout->addWidget(m_contentHost, 1);

  connect(m_tabs, &QTabBar::currentChanged, this, [this](int index) {
    if (index >= 0) {
      emit destinationChanged(m_tabs->tabData(index).toString());
    }
  });
  connect(m_libraryButton, &QToolButton::clicked, this, [this] {
    if (m_libraryAction && m_libraryAction->isEnabled()) {
      m_libraryAction->trigger();
    }
  });

  updateHeaderMetrics();
}

WorkspaceFrame::~WorkspaceFrame()
{
  // The QAction may be owned by this widget. Disconnect before QWidget starts
  // deleting its children, since the library button can be destroyed before
  // the action's destroyed signal would otherwise call updateLibraryButton().
  if (m_libraryAction) {
    disconnect(m_libraryAction.data(), nullptr, this, nullptr);
    m_libraryAction.clear();
  }
}

void WorkspaceFrame::setContext(const QString& setupName, const QString& gameName)
{
  m_setupName = setupName;
  m_gameName = gameName;

  m_setupLabel->setAccessibleName(setupName);
  m_gameLabel->setAccessibleName(gameName);
  m_context->setAccessibleName(
      setupName.isEmpty() ? gameName
                          : gameName.isEmpty() ? setupName
                                               : tr("%1, %2").arg(setupName, gameName));

  QFont setupFont = font();
  setupFont.setWeight(QFont::DemiBold);
  if (setupFont.pointSizeF() > 0.0) {
    setupFont.setPointSizeF(setupFont.pointSizeF() + 1.0);
  }
  m_setupLabel->setFont(setupFont);
  m_gameLabel->setFont(font());

  updateContextLabels();
  updateHeaderMetrics();
  updateHeaderLayout();
}

void WorkspaceFrame::setHeaderControls(QWidget* profileControls,
                                       QWidget* launchControls)
{
  if (profileControls == launchControls) {
    launchControls = nullptr;
  }
  if (m_profileControls == profileControls && m_launchControls == launchControls) {
    return;
  }

  const auto isNewControl = [profileControls, launchControls](QWidget* widget) {
    return widget && (widget == profileControls || widget == launchControls);
  };
  for (auto* oldControl : {m_profileControls, m_launchControls}) {
    if (!oldControl) {
      continue;
    }
    m_identityLayout->removeWidget(oldControl);
    m_controlsLayout->removeWidget(oldControl);
    if (!isNewControl(oldControl)) {
      oldControl->hide();
      oldControl->setParent(nullptr);
    }
  }

  m_profileControls = profileControls;
  m_launchControls = launchControls;
  m_controlsOnSecondRow = false;
  m_controlsStacked = false;
  m_controlsLayout->setDirection(QBoxLayout::LeftToRight);
  m_controlsRow->hide();

  for (auto* control : {m_profileControls, m_launchControls}) {
    if (control) {
      m_identityLayout->addWidget(control, 0, Qt::AlignVCenter);
    }
  }

  updateHeaderLayout();
}

void WorkspaceFrame::setContent(QWidget* content)
{
  if (m_content == content) {
    return;
  }
  if (m_content) {
    m_contentLayout->removeWidget(m_content);
    m_content->setParent(nullptr);
  }
  m_content = content;
  if (m_content) {
    m_contentLayout->addWidget(m_content);
  }
}

void WorkspaceFrame::addDestination(const QString& id, const QString& text,
                                    const QIcon& icon)
{
  if (id.isEmpty()) {
    return;
  }

  for (int index = 0; index < m_tabs->count(); ++index) {
    if (m_tabs->tabData(index).toString() == id) {
      m_tabs->setTabText(index, text);
      m_tabs->setTabIcon(index, icon);
      m_tabs->setTabToolTip(index, text);
      return;
    }
  }

  const int previousIndex = m_tabs->currentIndex();
  int index = -1;
  {
    const QSignalBlocker blocker(m_tabs);
    index = icon.isNull() ? m_tabs->addTab(text) : m_tabs->addTab(icon, text);
    m_tabs->setTabData(index, id);
    m_tabs->setTabToolTip(index, text);
  }

  // QTabBar chooses the first tab automatically. Its id is assigned while
  // signals are blocked so observers never receive an empty destination.
  if (previousIndex < 0 && m_tabs->currentIndex() == index) {
    emit destinationChanged(id);
  }
}

void WorkspaceFrame::setDestination(const QString& id)
{
  for (int index = 0; index < m_tabs->count(); ++index) {
    if (m_tabs->tabData(index).toString() == id && m_tabs->isTabVisible(index)) {
      m_tabs->setCurrentIndex(index);
      return;
    }
  }
}

void WorkspaceFrame::setDestinationVisible(const QString& id, bool visible)
{
  for (int index = 0; index < m_tabs->count(); ++index) {
    if (m_tabs->tabData(index).toString() == id) {
      m_tabs->setTabVisible(index, visible);
      return;
    }
  }
}

QString WorkspaceFrame::destination() const
{
  const int index = m_tabs->currentIndex();
  return index >= 0 ? m_tabs->tabData(index).toString() : QString();
}

void WorkspaceFrame::setLibraryAction(QAction* action)
{
  if (m_libraryAction == action) {
    updateLibraryButton();
    return;
  }

  if (m_libraryAction) {
    disconnect(m_libraryAction.data(), nullptr, this, nullptr);
  }
  m_libraryAction = action;
  if (action) {
    connect(action, &QAction::changed, this, &WorkspaceFrame::updateLibraryButton);
    connect(action, &QObject::destroyed, this, [this] {
      m_libraryAction.clear();
      updateLibraryButton();
    });
  }
  updateLibraryButton();
}

void WorkspaceFrame::addNavigationWidget(QWidget* widget)
{
  if (!widget || m_navigationExtrasLayout->indexOf(widget) >= 0) {
    return;
  }
  m_navigationExtrasLayout->addWidget(widget, 0, Qt::AlignVCenter);
  m_navigationExtras->show();
}

void WorkspaceFrame::setNavigationVisible(bool visible)
{
  m_navigation->setVisible(visible);
}

void WorkspaceFrame::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);
  updateHeaderLayout();
  updateContextLabels();
}

void WorkspaceFrame::changeEvent(QEvent* event)
{
  QWidget::changeEvent(event);
  switch (event->type()) {
  case QEvent::FontChange:
  case QEvent::ApplicationFontChange:
  case QEvent::StyleChange:
    setContext(m_setupName, m_gameName);
    updateHeaderLayout();
    QTimer::singleShot(0, this, [this] {
      updateHeaderLayout();
      updateContextLabels();
      updateHeaderMetrics();
    });
    break;
  default:
    break;
  }
}

void WorkspaceFrame::updateHeaderLayout()
{
  if (!m_header || !m_identityLayout || !m_controlsLayout) {
    return;
  }

  const int profileWidth = minimumWidthFor(m_profileControls);
  const int launchWidth  = minimumWidthFor(m_launchControls);
  const int controlCount = int(m_profileControls != nullptr) + int(m_launchControls != nullptr);

  const bool showLibrary = !m_libraryButton->isHidden();
  const int baseCount = 1 + (showLibrary ? 2 : 0) + controlCount;
  const int baseWidth = m_context->minimumWidth() +
                        (showLibrary ? m_libraryButton->sizeHint().width() +
                                           m_headerDivider->sizeHint().width()
                                     : 0) +
                        profileWidth + launchWidth;
  const QMargins margins = m_headerLayout->contentsMargins();
  const int availableWidth = m_header->width() - margins.left() - margins.right();
  const int inlineWidth = baseWidth +
                          qMax(0, baseCount - 1) * m_identityLayout->spacing();
  const bool hasControls = controlCount > 0;
  const bool secondRow = hasControls && availableWidth < inlineWidth;

  if (secondRow != m_controlsOnSecondRow) {
    moveControlsToSecondRow(secondRow);
  }

  const int singleRowControlsWidth = profileWidth + launchWidth +
      qMax(0, controlCount - 1) * m_controlsLayout->spacing();
  const bool stacked = secondRow && availableWidth < singleRowControlsWidth;
  if (stacked != m_controlsStacked) {
    m_controlsLayout->setDirection(stacked ? QBoxLayout::TopToBottom
                                           : QBoxLayout::LeftToRight);
    m_controlsStacked = stacked;
  }

  m_headerLayout->invalidate();
  m_headerLayout->activate();
  updateHeaderMetrics();
  updateContextLabels();
}

void WorkspaceFrame::updateHeaderMetrics()
{
  if (!m_header || !m_headerLayout || !m_navigation) {
    return;
  }

  m_headerLayout->invalidate();
  const int headerHeight = qMax(58, m_headerLayout->sizeHint().height());
  m_header->setMinimumHeight(headerHeight);

  const int navigationHeight = qMax(38, fontMetrics().height() + 16);
  m_navigation->setMinimumHeight(navigationHeight);
  m_header->updateGeometry();
  m_navigation->updateGeometry();
}

void WorkspaceFrame::updateContextLabels()
{
  if (!m_setupLabel || !m_gameLabel) {
    return;
  }

  const int setupWidth = qMax(0, m_setupLabel->contentsRect().width());
  const int gameWidth = qMax(0, m_gameLabel->contentsRect().width());
  m_setupLabel->setText(m_setupLabel->fontMetrics().elidedText(
      m_setupName, Qt::ElideRight, setupWidth));
  m_gameLabel->setText(m_gameLabel->fontMetrics().elidedText(
      m_gameName, Qt::ElideRight, gameWidth));
  m_setupLabel->setToolTip(m_setupName);
  m_gameLabel->setToolTip(m_gameName);
}

void WorkspaceFrame::updateLibraryButton()
{
  if (!m_libraryAction) {
    m_libraryButton->setIcon(QIcon());
    m_libraryButton->setToolTip(QString());
    m_libraryButton->setAccessibleDescription(QString());
    m_libraryButton->setEnabled(false);
    m_libraryButton->hide();
    m_headerDivider->hide();
    updateHeaderLayout();
    return;
  }

  const auto* action = m_libraryAction.data();
  QIcon backIcon = style()->standardIcon(QStyle::SP_ArrowBack);
  m_libraryButton->setIcon(backIcon.isNull() ? action->icon() : backIcon);
  m_libraryButton->setCheckable(action->isCheckable());
  m_libraryButton->setChecked(action->isChecked());
  m_libraryButton->setEnabled(action->isEnabled());
  m_libraryButton->setVisible(action->isVisible());
  m_headerDivider->setVisible(action->isVisible());
  m_libraryButton->setToolTip(action->toolTip().isEmpty() ? action->text()
                                                          : action->toolTip());
  m_libraryButton->setAccessibleDescription(action->text());
  updateHeaderLayout();
}

int WorkspaceFrame::minimumWidthFor(const QWidget* widget) const
{
  if (!widget) {
    return 0;
  }
  const QSize minimum = widget->minimumSizeHint();
  const int hintedWidth = minimum.width() >= 0 ? minimum.width()
                                               : widget->sizeHint().width();
  return qMax(widget->minimumWidth(), hintedWidth);
}

void WorkspaceFrame::moveControlsToSecondRow(bool secondRow)
{
  auto move = [this, secondRow](QWidget* widget) {
    if (!widget) {
      return;
    }
    m_identityLayout->removeWidget(widget);
    m_controlsLayout->removeWidget(widget);
    if (secondRow) {
      m_controlsLayout->addWidget(widget, 0, Qt::AlignVCenter);
    } else {
      m_identityLayout->addWidget(widget, 0, Qt::AlignVCenter);
    }
  };

  move(m_profileControls);
  move(m_launchControls);
  m_controlsOnSecondRow = secondRow;
  m_controlsRow->setVisible(secondRow);
}
