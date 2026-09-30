/*
Copyright (C) 2012 Sebastian Herbord. All rights reserved.

This file is part of Mod Organizer.

Mod Organizer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Mod Organizer is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Mod Organizer.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef DOWNLOADPROGRESSRENDERER_H
#define DOWNLOADPROGRESSRENDERER_H

#include <QApplication>
#include <QPainter>
#include <QProgressBar>
#include <QStyle>
#include <QStyleOption>

namespace DownloadProgressRenderer
{

class StyleContext final : public QProgressBar
{
public:
  using QProgressBar::QProgressBar;

  void initializeStyleOption(QStyleOptionProgressBar* option) const
  {
    QProgressBar::initStyleOption(option);
  }
};

inline void draw(QPainter* painter, const QStyleOptionViewItem& itemOption,
                 StyleContext* styleContext, int progress, const QString& text)
{
  QStyleOptionProgressBar progressOption;
  if (styleContext->layoutDirection() != itemOption.direction) {
    styleContext->setLayoutDirection(itemOption.direction);
  }
  styleContext->initializeStyleOption(&progressOption);
  progressOption.rect = itemOption.rect;
  progressOption.progress = progress;
  progressOption.text = text;
  progressOption.textVisible = true;
  progressOption.textAlignment = Qt::AlignCenter;
  progressOption.styleObject = styleContext;

  painter->save();
  painter->setLayoutDirection(itemOption.direction);
  styleContext->style()->drawControl(QStyle::CE_ProgressBar, &progressOption,
                                     painter, styleContext);
  painter->restore();
}

}  // namespace DownloadProgressRenderer

#endif  // DOWNLOADPROGRESSRENDERER_H
