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

#ifndef WORKSPACEFRAME_H
#define WORKSPACEFRAME_H

#include <QIcon>
#include <QPointer>
#include <QString>
#include <QWidget>

class QAction;
class QBoxLayout;
class QFrame;
class QHBoxLayout;
class QLabel;
class QResizeEvent;
class QTabBar;
class QVBoxLayout;
class QToolButton;

// A compact, context-aware frame for the main workspace. The owning window
// supplies the profile/launch controls and the destination content.
class WorkspaceFrame : public QWidget
{
  Q_OBJECT

public:
  explicit WorkspaceFrame(QWidget* parent = nullptr);
  ~WorkspaceFrame() override;

  void setContext(const QString& setupName, const QString& gameName);
  void setHeaderControls(QWidget* profileControls, QWidget* launchControls);
  void setContent(QWidget* content);
  void addDestination(const QString& id, const QString& text,
                      const QIcon& icon = QIcon());
  void setDestination(const QString& id);
  void setDestinationVisible(const QString& id, bool visible);
  QString destination() const;
  void setLibraryAction(QAction* action);

  // Adds a compact utility such as a view-density menu beside the destinations.
  void addNavigationWidget(QWidget* widget);
  void setNavigationVisible(bool visible);

signals:
  void destinationChanged(const QString& id);

protected:
  void resizeEvent(QResizeEvent* event) override;
  void changeEvent(QEvent* event) override;

private:
  QWidget* m_header = nullptr;
  QWidget* m_identityRow = nullptr;
  QWidget* m_controlsRow = nullptr;
  QWidget* m_navigation = nullptr;
  QWidget* m_navigationExtras = nullptr;
  QWidget* m_contentHost = nullptr;
  QWidget* m_content = nullptr;

  QVBoxLayout* m_frameLayout = nullptr;
  QVBoxLayout* m_headerLayout = nullptr;
  QHBoxLayout* m_identityLayout = nullptr;
  QBoxLayout* m_controlsLayout = nullptr;
  QHBoxLayout* m_navigationLayout = nullptr;
  QHBoxLayout* m_navigationExtrasLayout = nullptr;
  QVBoxLayout* m_contentLayout = nullptr;

  QToolButton* m_libraryButton = nullptr;
  QFrame* m_headerDivider = nullptr;
  QWidget* m_context = nullptr;
  QLabel* m_setupLabel = nullptr;
  QLabel* m_gameLabel = nullptr;
  QTabBar* m_tabs = nullptr;
  QPointer<QAction> m_libraryAction;
  QWidget* m_profileControls = nullptr;
  QWidget* m_launchControls = nullptr;

  QString m_setupName;
  QString m_gameName;
  bool m_controlsOnSecondRow = false;
  bool m_controlsStacked = false;

  void updateHeaderLayout();
  void updateHeaderMetrics();
  void updateContextLabels();
  void updateLibraryButton();
  int minimumWidthFor(const QWidget* widget) const;
  void moveControlsToSecondRow(bool secondRow);
};

#endif  // WORKSPACEFRAME_H
