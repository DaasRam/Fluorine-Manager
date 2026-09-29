#ifndef MODORGANIZER_DOWNLOADTAB_INCLUDED
#define MODORGANIZER_DOWNLOADTAB_INCLUDED

#include <filterwidget.h>
#include <QWidget>

namespace Ui
{
class DownloadsTab;
}
class OrganizerCore;
class DownloadListView;

class DownloadsTab : public QWidget
{
  Q_OBJECT;

public:
  explicit DownloadsTab(OrganizerCore& core, QWidget* parent = nullptr);
  ~DownloadsTab() override;

  void update();
  DownloadListView* view() const { return ui.list; }
  QLineEdit* filterEdit() const { return ui.filter; }

private:
  struct DownloadsTabUi
  {
    QPushButton* refresh;
    QPushButton* queryInfos;
    DownloadListView* list;
    QCheckBox* showHidden;
    QLineEdit* filter;
  };

  OrganizerCore& m_core;
  Ui::DownloadsTab* m_form;
  DownloadsTabUi ui;
  MOBase::FilterWidget m_filter;

  void refresh();

  /**
   * @brief Handle click on the "Query infos" button
   **/
  void queryInfos();

  void resumeDownload(int downloadIndex);
};

#endif  // MODORGANIZER_DOWNLOADTAB_INCLUDED
