// Test-only probe loaded into the packaged app with an isolated offline engine.
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTableView>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>

namespace {
QString output;
QJsonArray checks;
QPointer<QMainWindow> window;
int stage = 0;
int ticks = 0;

void check(bool pass, const QString& text)
{ checks.append(QJsonObject{{"check", text}, {"pass", pass}}); }

void finish()
{
  QFile file(output + "/checks.json");
  if (file.open(QIODevice::WriteOnly))
    file.write(QJsonDocument(QJsonObject{{"checks", checks}}).toJson());
  bool ok = true;
  for (const auto& item : checks) ok &= item.toObject()["pass"].toBool();
  qApp->exit(ok ? 0 : 91);
}

void click(QPushButton* button)
{
  check(button && button->isEnabled(), "Action available: " + (button ? button->objectName() : "missing"));
  if (button) QTimer::singleShot(0, button, &QPushButton::click);
}

void capture(QWidget* widget, const QString& name)
{ check(widget->grab().save(output + "/" + name + ".png"), "Capture " + name); }

QDialog* installer()
{
  for (auto* widget : QApplication::topLevelWidgets())
    if (widget->objectName() == "fluorineInstaller" && widget->isVisible())
      return qobject_cast<QDialog*>(widget);
  return nullptr;
}

void tick()
{
  if (++ticks > 250) {
    check(false, QString("Installer smoke timed out at stage %1").arg(stage));
    if (auto* dialog = installer()) capture(dialog, "timeout");
    finish();
    return;
  }
  // Explicitly skip sign-in for the public/cached-only synthetic fixture.
  for (auto* widget : QApplication::topLevelWidgets()) {
    if (!widget->isVisible()) continue;
    if (widget->windowTitle() == "Connect to Nexus") {
      QTimer::singleShot(0, widget, [widget] { qobject_cast<QDialog*>(widget)->reject(); });
      return;
    }
    if (widget->windowTitle() == "Review installation checks") {
      for (auto* button : widget->findChildren<QPushButton*>())
        if (button->text() == "Continue") { click(button); return; }
    }
  }
  if (!window) {
    for (auto* widget : QApplication::topLevelWidgets())
      if (QByteArray(widget->metaObject()->className()) == "MainWindow" && widget->isVisible())
        window = qobject_cast<QMainWindow*>(widget);
    return;
  }
  auto* dialog = installer();
  if (stage == 0) {
    auto* action = window->findChild<QAction*>("actionChange_Game");
    if (!action || !action->isEnabled()) return;
    action->trigger();
    stage = 1;
    return;
  }
  if (stage == 1) {
    stage = 2;
    click(window->findChild<QPushButton*>("libraryInstallModlistButton"));
    return;
  }
  if (stage == 2 && dialog) {
    auto* gallery = dialog->findChild<QListWidget*>("modlistGallery");
    if (!gallery || gallery->count() != 1) return;
    auto* details = dialog->findChild<QLabel*>("modlistGalleryDetails");
    check(gallery->item(0)->text().contains("Updated"), "Gallery card shows source update date");
    check(details && details->toolTip().contains("2026-09-27"), "Gallery detail exposes the precise authored update date");
    capture(dialog, "installer-gallery");
    stage = 3;
    click(dialog->findChild<QPushButton*>("modlistConfigure"));
    return;
  }
  if (stage == 3 && dialog) {
    auto* pages = dialog->findChild<QStackedWidget*>("modlistInstallPages");
    if (!pages || pages->currentIndex() != 1) return;
    dialog->findChild<QLineEdit*>("modlistOutputPath")->setText(output + "/Installed fixture");
    dialog->findChild<QLineEdit*>("modlistDownloadsPath")->setText(output + "/fixture-downloads");
    dialog->findChild<QLineEdit*>("modlistGamePath")->setText(output + "/game");
    capture(dialog, "installer-review");
    stage = 4;
    click(dialog->findChild<QPushButton*>("modlistBeginInstall"));
    return;
  }
  if (stage == 4 && dialog) {
    auto* active = dialog->findChild<QTableView*>("installActiveView");
    auto* attention = dialog->findChild<QTableView*>("installAttentionView");
    auto* completed = dialog->findChild<QTableView*>("installCompletedView");
    if (!active || active->model()->rowCount() != 24 || attention->model()->rowCount() != 3) return;
    check(completed->model()->rowCount() == 5, "Completed downloads remain in history");
    check(active->findChildren<QProgressBar*>().isEmpty(), "24 active downloads use delegate painting");
    auto* reuse = dialog->findChild<QLabel*>("installQueueReuse");
    check(reuse && reuse->text().contains("6"), "Reused archives are visible separately");
    check(!dialog->findChild<QPlainTextEdit*>("modlistInstallLog")->isVisible(), "Detailed engine log starts collapsed");
    auto* tabs = dialog->findChild<QTabWidget*>("installQueueTabs");
    tabs->setCurrentIndex(0);
    check(active->horizontalHeader()->sectionSize(0) > 300
              && active->horizontalHeader()->sectionSize(2) > 300,
          "Name and progress columns use the available queue width");
    capture(dialog, "installer-active");
    tabs->setCurrentIndex(1);
    capture(dialog, "installer-attention");
    stage = 5;
    click(dialog->findChild<QPushButton*>("modlistStopInstall"));
    return;
  }
  if (stage == 5 && dialog) {
    auto* resume = dialog->findChild<QPushButton*>("modlistResumeInstall");
    if (!resume || !resume->isVisible()) return;
    check(dialog->findChild<QTableView*>("installActiveView")->model()->rowCount() == 0,
          "Stopping removes all running rows from Active");
    auto* attention = dialog->findChild<QTableView*>("installAttentionView");
    int failed = 0, stopped = 0;
    for (int i = 0; i < attention->model()->rowCount(); ++i) {
      const int status = attention->model()->index(i, 0).data(Qt::UserRole + 3).toInt();
      failed += status == 1; stopped += status == 2;
    }
    check(failed == 1 && stopped == 24, "Stopped rows stay distinct from the one real failure");
    check(QFile::exists(output + "/fixture-downloads/retained-archive.bin"), "Stop preserves cached downloads");
    capture(dialog, "installer-stopped");
    stage = 6;
    click(dialog->findChild<QPushButton*>("modlistCloseInstall"));
    return;
  }
  if (stage == 6 && !dialog) {
    auto* pending = window->findChild<QWidget*>("libraryPendingInstall");
    if (!pending || !pending->isVisible()) return;
    check(pending->isVisible(), "Library exposes the unfinished installation");
    capture(window, "installer-library-resume");
    stage = 7;
    click(window->findChild<QPushButton*>("libraryResumeInstallButton"));
    return;
  }
  if (stage == 7 && dialog) {
    auto* close = dialog->findChild<QPushButton*>("modlistCloseInstall");
    auto* phase = dialog->findChild<QLabel*>("installPhaseLabel");
    if (!close || !close->isEnabled() || !phase->text().contains("complete")) return;
    check(dialog->findChild<QTableView*>("installCompletedView")->model()->rowCount() == 1,
          "Resumed installation completed through the real controller and setup stage");
    QFile receipt(output + "/Installed fixture/fluorine-modlist.json");
    check(receipt.open(QIODevice::ReadOnly), "Successful install writes local update receipt");
    const auto metadata = QJsonDocument::fromJson(receipt.readAll()).object();
    check(metadata.value("version").toString() == "2.4.1", "Receipt preserves the selected list version across resume");
    capture(dialog, "installer-complete");
    for (auto* box : dialog->findChildren<QCheckBox*>())
      if (box->text().contains("Open this setup")) box->setChecked(false);
    stage = 8;
    click(close);
    return;
  }
  if (stage == 8 && !dialog) {
    check(!window->findChild<QWidget*>("libraryPendingInstall")->isVisible(), "Completion removes the Library resume banner");
    auto* metadata = window->findChild<QLabel*>("setupModlistMetadata");
    check(metadata && metadata->isVisible() && metadata->text().contains("List updated")
              && metadata->text().contains("Installed / updated locally"),
          "Library distinguishes the author's update from the local installation date");
    capture(window, "installer-library-complete");
    finish();
  }
}

void installProbe()
{
  output = qEnvironmentVariable("FLUORINE_UI_SMOKE_DIR");
  if (output.isEmpty() || !QFile::exists(output + "/isolated-test-fixture")) return;
  QTimer::singleShot(0, qApp, [] {
    auto* timer = new QTimer(qApp);
    timer->setInterval(200);
    QObject::connect(timer, &QTimer::timeout, qApp, tick);
    timer->start();
  });
}
}
Q_COREAPP_STARTUP_FUNCTION(installProbe)
