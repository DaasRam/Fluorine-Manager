// Opt-in integration probe for the packaged application's real dialogs.
// Runs only in the isolated run_workspace_smoke.py fixture and is never shipped.
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <functional>

namespace {
QString output;
QJsonArray checks;

void check(bool result, const QString& text)
{
  checks.append(QJsonObject{{"pass", result}, {"check", text}});
}

void finish()
{
  QFile result(output + (qEnvironmentVariableIsSet("FLUORINE_UI_SMOKE_RESTORE_MANAGEMENT")
                             ? "/checks-restore.json" : "/checks.json"));
  if (result.open(QIODevice::WriteOnly))
    result.write(QJsonDocument(QJsonObject{{"checks", checks}}).toJson());
  bool success = true;
  for (const auto& value : checks) success &= value.toObject()["pass"].toBool();
  qApp->exit(success ? 0 : 91);
}

template<class T> T* control(QObject* parent, const char* name)
{
  auto* result = parent->findChild<T*>(name);
  check(result != nullptr, QString("Control available: %1").arg(name));
  return result;
}

void capture(QWidget* widget, const QString& name)
{
  QApplication::processEvents();
  check(widget->grab().save(output + '/' + name + ".png"), "Capture " + name);
}

QListWidgetItem* namedItem(QListWidget* list, const QString& name)
{
  if (!list) return nullptr;
  // Active markers may be drawn separately; profile names stay unmodified.
  for (int i = 0; i < list->count(); ++i)
    if (list->item(i)->text() == name) return list->item(i);
  return nullptr;
}

QByteArray read(const QString& path)
{
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QByteArray modSelection(const QString& path)
{
  QByteArray result;
  // The profile writer normalizes comments/newlines and adds automatic DLC
  // entries. Preserve the user-controlled enabled states and their exact order.
  for (auto line : read(path).split('\n')) {
    if (line.endsWith('\r')) line.chop(1);
    if (line.startsWith('+') || line.startsWith('-')) result += line + '\n';
  }
  return result;
}

void answerName(const QString& name)
{
  QTimer::singleShot(60, qApp, [name] {
    auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
    check(input, "Profile name prompt opened");
    if (input) {
      input->setTextValue(name);
      input->accept();
    } else if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
      dialog->reject();
    }
  });
}

void openDialog(QMainWindow* window, const char* actionName, const char* className,
                const std::function<void(QDialog*)>& exercise)
{
  auto* action = control<QAction>(window, actionName);
  if (!action) return;
  QTimer::singleShot(100, window, [className, exercise] {
    auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
    const bool expected = dialog && QByteArray(dialog->metaObject()->className()) == className;
    check(expected, QString("Opened real %1").arg(className));
    if (expected) exercise(dialog);
    if (dialog && dialog->isVisible()) dialog->reject();
  });
  action->trigger();
  QTest::qWait(150);
  check(QTest::qWaitFor([window] {
    return window->isEnabled() && !QApplication::activeModalWidget();
  }, 5000), "Workspace remains interactive after closing dialog");
}

void profileChecks(QMainWindow* window)
{
  const QString profiles = output + "/Desert Workshop/profiles/";
  openDialog(window, "actionAdd_Profile", "ProfilesDialog", [&](QDialog* dialog) {
    dialog->resize(920, 640);
    auto* list = control<QListWidget>(dialog, "profilesList");
    auto* active = control<QLabel>(dialog, "activeProfileNameLabel");
    auto* selected = control<QLabel>(dialog, "selectedProfileNameLabel");
    auto* activate = control<QPushButton>(dialog, "select");
    auto* duplicate = control<QPushButton>(dialog, "copyProfileButton");
    auto* rename = control<QPushButton>(dialog, "renameButton");
    auto* remove = control<QPushButton>(dialog, "removeProfileButton");
    if (!list || !active || !selected || !activate || !duplicate || !rename || !remove) return;
    check(active->text().contains("Everyday") && selected->text().contains("Everyday"),
          "Profiles identifies the active and selected profile");
    check(!remove->isEnabled() && !rename->isEnabled(),
          "Active profile cannot be removed or renamed");
    capture(dialog, "profiles-active");

    auto* testing = namedItem(list, "Testing");
    check(testing, "Inactive profile is present");
    if (!testing) return;
    list->setCurrentItem(testing);
    check(selected->text().contains("Testing") && active->text().contains("Everyday") &&
              activate->isEnabled() && rename->isEnabled() && remove->isEnabled(),
          "Selecting a profile exposes its actions without changing the active profile");
    capture(dialog, "profiles-selected");

    const QByteArray originalMods = modSelection(profiles + "Testing/modlist.txt");
    answerName("UI Copy");
    duplicate->click();
    auto* copy = namedItem(list, "UI Copy");
    check(copy && !originalMods.isEmpty() && modSelection(profiles + "UI Copy/modlist.txt") == originalMods,
          "Duplicating a profile preserves its mod selection and order");
    if (copy) {
      list->setCurrentItem(copy);
      answerName("Testing");
      QTimer::singleShot(150, dialog, [] {
        auto* error = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        check(error && QByteArray(error->metaObject()->className()) != "ProfilesDialog",
              "A profile-name collision reports an error");
        if (error && QByteArray(error->metaObject()->className()) != "ProfilesDialog")
          error->reject();
      });
      rename->click();
      check(namedItem(list, "UI Copy") && namedItem(list, "Testing") &&
                modSelection(profiles + "UI Copy/modlist.txt") == originalMods &&
                modSelection(profiles + "Testing/modlist.txt") == originalMods,
            "Failed rename preserves both profile identities and their files");
      answerName("UI Renamed");
      rename->click();
      check(namedItem(list, "UI Renamed") && QDir(profiles + "UI Renamed").exists() &&
                !QDir(profiles + "UI Copy").exists() &&
                modSelection(profiles + "UI Renamed/modlist.txt") == originalMods &&
                selected->text().contains("UI Renamed"),
            "Renaming an inactive profile preserves its files");
      if (auto* saves = control<QCheckBox>(dialog, "localSavesBox"))
        saves->setChecked(true);
    }

    list->setCurrentRow(-1);
    list->clearSelection();
    check(!activate->isEnabled() && !duplicate->isEnabled() &&
              !rename->isEnabled() && !remove->isEnabled(),
          "Profile actions are disabled without a selection");
    QMetaObject::invokeMethod(dialog, "on_select_clicked", Qt::DirectConnection);
    check(dialog->isVisible(), "An empty selection cannot activate or close the profile manager");
    list->setCurrentItem(testing);
    dialog->resize(760, 600);
    QApplication::processEvents();
    check(dialog->width() <= 920 && dialog->height() <= 700 && activate->isVisible(),
          "Profiles remains usable at compact size with the configured font");
    capture(dialog, "profiles-compact");
    activate->click();
  });
  auto* profileButton = control<QToolButton>(window, "profileActionsButton");
  check(profileButton && profileButton->text().contains("Testing"),
        "Activating a profile updates the real workspace header");

  openDialog(window, "actionAdd_Profile", "ProfilesDialog", [&](QDialog* dialog) {
    auto* active = control<QLabel>(dialog, "activeProfileNameLabel");
    auto* list = control<QListWidget>(dialog, "profilesList");
    check(active && active->text().contains("Testing") && list && namedItem(list, "UI Renamed"),
          "Reopening Profiles retains the active profile and created profile");
    const QSettings renamedSettings(profiles + "UI Renamed/settings.ini", QSettings::IniFormat);
    check(renamedSettings.value("LocalSaves").toBool() && !QDir(profiles + "UI Copy").exists(),
          "Settings changed after renaming are saved under the new profile path");
    if (list && namedItem(list, "UI Renamed")) {
      list->setCurrentItem(namedItem(list, "UI Renamed"));
      if (auto* remove = control<QPushButton>(dialog, "removeProfileButton")) {
        QTimer::singleShot(60, dialog, [] {
          auto* confirmation = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
          check(confirmation, "Removing a profile asks for confirmation");
          if (confirmation && confirmation->button(QMessageBox::Yes))
            confirmation->button(QMessageBox::Yes)->click();
          else if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
            modal->reject();
        });
        remove->click();
        check(!namedItem(list, "UI Renamed") && !QDir(profiles + "UI Renamed").exists() &&
                  QDir(profiles + "Testing").exists(),
              "Removing an inactive fixture profile leaves the active profile intact");
      }
    }
  });
}

struct ProgramState {
  QString name;
  QString arguments;
  QString wrapper;
  bool proton = false;
  bool steam = false;
};

void executableChecks(QMainWindow* window)
{
  ProgramState original;
  openDialog(window, "actionModify_Executables", "EditExecutablesDialog", [&](QDialog* dialog) {
    dialog->resize(1000, 740);
    auto* list = control<QListWidget>(dialog, "list");
    auto* title = control<QLineEdit>(dialog, "title");
    auto* arguments = control<QLineEdit>(dialog, "arguments");
    auto* wrapper = control<QPlainTextEdit>(dialog, "wrapperOptions");
    auto* proton = control<QCheckBox>(dialog, "useProton");
    auto* steam = control<QCheckBox>(dialog, "useSteam");
    auto* advanced = control<QToolButton>(dialog, "executableAdvancedToggle");
    auto* buttons = control<QDialogButtonBox>(dialog, "buttons");
    if (!list || !title || !arguments || !wrapper || !proton || !steam || !advanced || !buttons) return;
    check(list->currentItem() && title->isEnabled(), "Current launch program is selected and editable");
    original = {title->text(), arguments->text(), wrapper->toPlainText(), proton->isChecked(), steam->isChecked()};
    check(!advanced->isChecked() && !wrapper->isVisible(), "Advanced launch settings start collapsed");
    capture(dialog, "executables-basic");
    advanced->click();
    check(advanced->isChecked() && wrapper->isVisible(), "Advanced options can be opened without changing runtime flags");
    check(proton->isChecked() == original.proton && steam->isChecked() == original.steam,
          "Disclosure does not change the selected program's launch mode");
    capture(dialog, "executables-advanced");
    list->clearSelection();
    check(!title->isEnabled() && !proton->isEnabled() && !steam->isEnabled(),
          "Executable fields disable when selection is cleared");
    if (auto* item = namedItem(list, original.name)) list->setCurrentItem(item);
    check(title->isEnabled() && proton->isEnabled() && steam->isEnabled(),
          "Reselecting a program restores its editable fields");
    arguments->setText("--discard-this-change");
    proton->setChecked(!original.proton);
    steam->setChecked(!original.steam);
    check(buttons->button(QDialogButtonBox::Apply)->isEnabled(),
          "Program edits are pending before Cancel");
    buttons->button(QDialogButtonBox::Cancel)->click();
  });

  if (original.name.isEmpty()) return;
  const QString savedArgs = "--fixture-preserve-arguments \"two words\"";
  const QString savedWrapper = "env FLUORINE_UI_FIXTURE=1 %command%";
  openDialog(window, "actionModify_Executables", "EditExecutablesDialog", [&](QDialog* dialog) {
    auto* title = control<QLineEdit>(dialog, "title");
    auto* arguments = control<QLineEdit>(dialog, "arguments");
    auto* wrapper = control<QPlainTextEdit>(dialog, "wrapperOptions");
    auto* proton = control<QCheckBox>(dialog, "useProton");
    auto* steam = control<QCheckBox>(dialog, "useSteam");
    auto* advanced = control<QToolButton>(dialog, "executableAdvancedToggle");
    auto* buttons = control<QDialogButtonBox>(dialog, "buttons");
    if (!title || !arguments || !wrapper || !proton || !steam || !advanced || !buttons) return;
    check(title->text() == original.name && arguments->text() == original.arguments &&
              proton->isChecked() == original.proton && steam->isChecked() == original.steam,
          "Cancel discards arguments and runtime edits");
    arguments->setText(savedArgs);
    wrapper->setPlainText(savedWrapper);
    proton->setChecked(!original.proton);
    steam->setChecked(!original.steam);
    buttons->button(QDialogButtonBox::Apply)->click();
    check(dialog->isVisible() && !buttons->button(QDialogButtonBox::Apply)->isEnabled(),
          "Apply commits valid changes and keeps the dialog open");
    advanced->setChecked(true);
    dialog->resize(900, 680);
    for (auto* area : dialog->findChildren<QScrollArea*>())
      area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());
    QApplication::processEvents();
    check(dialog->width() <= 1000 && buttons->isVisible(),
          "Advanced settings remain reachable at compact size with the configured font");
    capture(dialog, "executables-compact-advanced");
    arguments->setText("--discard-after-apply");
    buttons->button(QDialogButtonBox::Cancel)->click();
  });

  openDialog(window, "actionModify_Executables", "EditExecutablesDialog", [&](QDialog* dialog) {
    auto* arguments = control<QLineEdit>(dialog, "arguments");
    auto* wrapper = control<QPlainTextEdit>(dialog, "wrapperOptions");
    auto* proton = control<QCheckBox>(dialog, "useProton");
    auto* steam = control<QCheckBox>(dialog, "useSteam");
    check(arguments && wrapper && proton && steam && arguments->text() == savedArgs &&
              wrapper->toPlainText() == savedWrapper && proton->isChecked() != original.proton &&
              steam->isChecked() != original.steam,
          "Reopening retains applied arguments, wrapper and runtime flags while discarding later edits");
  });
  QFile expected(output + "/management-expected.json");
  check(expected.open(QIODevice::WriteOnly), "Save expected program state for an application restart");
  expected.write(QJsonDocument(QJsonObject{{"name", original.name}, {"arguments", savedArgs},
      {"wrapper", savedWrapper}, {"proton", !original.proton}, {"steam", !original.steam}}).toJson());
}

void restartChecks(QMainWindow* window)
{
  auto* profileButton = control<QToolButton>(window, "profileActionsButton");
  check(profileButton && profileButton->text().contains("Testing"),
        "Active profile survives an actual application restart");
  QFile expected(output + "/management-expected.json");
  check(expected.open(QIODevice::ReadOnly), "Read program state saved before restart");
  const auto data = QJsonDocument::fromJson(expected.readAll()).object();
  openDialog(window, "actionModify_Executables", "EditExecutablesDialog", [&](QDialog* dialog) {
    auto* list = control<QListWidget>(dialog, "list");
    if (auto* item = namedItem(list, data["name"].toString())) list->setCurrentItem(item);
    auto* title = control<QLineEdit>(dialog, "title");
    auto* arguments = control<QLineEdit>(dialog, "arguments");
    auto* wrapper = control<QPlainTextEdit>(dialog, "wrapperOptions");
    auto* proton = control<QCheckBox>(dialog, "useProton");
    auto* steam = control<QCheckBox>(dialog, "useSteam");
    check(!data.isEmpty() && title && arguments && wrapper && proton && steam &&
              title->text() == data["name"].toString() &&
              arguments->text() == data["arguments"].toString() &&
              wrapper->toPlainText() == data["wrapper"].toString() &&
              proton->isChecked() == data["proton"].toBool() &&
              steam->isChecked() == data["steam"].toBool(),
          "Applied program settings survive an actual application restart");
  });
}

void exercise(QMainWindow* window)
{
  window->resize(1300, 800);
  if (qEnvironmentVariableIsSet("FLUORINE_UI_SMOKE_RESTORE_MANAGEMENT")) {
    restartChecks(window);
  } else {
    profileChecks(window);
    executableChecks(window);
  }
  finish();
}

void installProbe()
{
  output = qEnvironmentVariable("FLUORINE_UI_SMOKE_DIR");
  if (output.isEmpty() || !QFile::exists(output + "/isolated-test-fixture")) return;
  QTimer::singleShot(0, qApp, [] {
    auto* timer = new QTimer(qApp);
    timer->setInterval(250);
    QObject::connect(timer, &QTimer::timeout, qApp, [timer, attempts = 0]() mutable {
      for (auto* widget : QApplication::topLevelWidgets()) {
        if (QByteArray(widget->metaObject()->className()) == "MainWindow" && widget->isVisible()) {
          timer->stop();
          auto* window = qobject_cast<QMainWindow*>(widget);
          QTimer::singleShot(1500, window, [window] { exercise(window); });
          return;
        }
      }
      if (++attempts == 160) {
        timer->stop();
        check(false, "MainWindow appeared within 40 seconds");
        finish();
      }
    });
    timer->start();
  });
}
}
Q_COREAPP_STARTUP_FUNCTION(installProbe)
