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

#include <cerrno>
#include "profilesdialog.h"
#include "ui_profilesdialog.h"

#include "bsainvalidation.h"
#include "filesystemutilities.h"
#include "fluorinetheme.h"
#include "game_features.h"
#include "iplugingame.h"
#include "localsavegames.h"
#include "organizercore.h"
#include "profile.h"
#include "profileinputdialog.h"
#include "report.h"
#include "settings.h"
#include "shared/appconfig.h"
#include "transfersavesdialog.h"

#include <QDir>
#include <QDirIterator>
#include <QInputDialog>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QIcon>
#include <QMessageBox>
#include <QStyle>
#include <QWhatsThis>

#include <exception>

using namespace MOBase;
using namespace MOShared;

Q_DECLARE_METATYPE(Profile::Ptr)

ProfilesDialog::ProfilesDialog(const QString& profileName, OrganizerCore& organizer,
                               QWidget* parent)
    : TutorableDialog("Profiles", parent), ui(new Ui::ProfilesDialog),
      m_GameFeatures(organizer.gameFeatures()), m_FailState(false),
      m_Game(organizer.managedGame()), m_ActiveProfileName(profileName)
{
  ui->setupUi(this);
  ui->profileStatusLayout->setColumnStretch(1, 1);
  ui->contentLayout->setStretch(0, 2);
  ui->contentLayout->setStretch(1, 3);
  ui->profilesDescriptionLabel->setProperty("secondary", true);
  ui->optionsHelpLabel->setProperty("secondary", true);
  FluorineTheme::apply(this);
  ui->activeProfileNameLabel->setText(profileName.isEmpty() ? tr("None") : profileName);

  QDir profilesDir(Settings::instance().paths().profiles());
  profilesDir.setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);

  QDirIterator profileIter(profilesDir);

  while (profileIter.hasNext()) {
    profileIter.next();
    QListWidgetItem* item = addItem(profileIter.filePath());
    if (profileName == profileIter.fileName()) {
      ui->profilesList->setCurrentItem(item);
    }
  }

  auto invalidation = m_GameFeatures.gameFeature<BSAInvalidation>();
  if (invalidation == nullptr) {
    ui->invalidationBox->setToolTip(
        tr("Archive invalidation isn't required for this game."));
    ui->invalidationBox->setEnabled(false);
  }

  if (!m_GameFeatures.gameFeature<LocalSavegames>()) {
    ui->localSavesBox->setToolTip(
        tr("This game does not support profile-specific game saves."));
    ui->localSavesBox->setEnabled(false);
  }

  connect(this, &ProfilesDialog::profileCreated, &organizer,
          &OrganizerCore::profileCreated);
  connect(this, &ProfilesDialog::profileRenamed, &organizer,
          &OrganizerCore::profileRenamed);
  connect(this, &ProfilesDialog::profileRemoved, &organizer,
          &OrganizerCore::profileRemoved);

  on_profilesList_currentItemChanged(ui->profilesList->currentItem(), nullptr);
}

ProfilesDialog::~ProfilesDialog()
{
  delete ui;
}

int ProfilesDialog::exec()
{
  GeometrySaver gs(Settings::instance(), this);
  return QDialog::exec();
}

void ProfilesDialog::showEvent(QShowEvent* event)
{
  TutorableDialog::showEvent(event);

  if (ui->profilesList->count() == 0) {
    QPoint pos = ui->profilesList->mapToGlobal(QPoint(0, 0));
    pos.rx() += ui->profilesList->width() / 2;
    pos.ry() += (ui->profilesList->height() / 2) - 20;
    QWhatsThis::showText(
        pos,
        QObject::tr(
            "Before you can use Fluorine, create at least one profile. "
            "Start the game at least once before creating a profile."),
        ui->profilesList);
  }
}

void ProfilesDialog::on_close_clicked()
{
  close();
}

void ProfilesDialog::on_select_clicked()
{
  QListWidgetItem* item = ui->profilesList->currentItem();
  if (item == nullptr) {
    return;
  }

  const Profile::Ptr currentProfile = item->data(Qt::UserRole).value<Profile::Ptr>();
  if (!currentProfile || currentProfile->name() == m_ActiveProfileName) {
    return;
  }

  m_Selected = currentProfile->name();
  close();
}

std::optional<QString> ProfilesDialog::selectedProfile() const
{
  return m_Selected;
}

QString ProfilesDialog::profileNameForItem(const QListWidgetItem* item) const
{
  if (item == nullptr) {
    return {};
  }

  const QVariant name = item->data(Qt::UserRole + 1);
  if (name.isValid()) {
    return name.toString();
  }

  const Profile::Ptr profile = item->data(Qt::UserRole).value<Profile::Ptr>();
  return profile ? profile->name() : item->text();
}

void ProfilesDialog::setProfileItemName(QListWidgetItem* item, const QString& name)
{
  item->setText(name);
  item->setData(Qt::UserRole + 1, name);
  if (item == ui->profilesList->currentItem()) {
    ui->selectedProfileNameLabel->setText(name);
  }

  if (!name.isEmpty() && name == m_ActiveProfileName) {
    item->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    item->setToolTip(tr("Currently active profile"));
    item->setData(Qt::AccessibleDescriptionRole, tr("Current profile"));
  } else {
    item->setIcon(QIcon());
    item->setToolTip(tr("Profile: %1").arg(name));
    item->setData(Qt::AccessibleDescriptionRole, QString());
  }
}

QListWidgetItem* ProfilesDialog::addItem(const QString& name)
{
  QDir profileDir(name);
  QListWidgetItem* newItem = new QListWidgetItem;
  setProfileItemName(newItem, profileDir.dirName());
  try {
    newItem->setData(Qt::UserRole, QVariant::fromValue(Profile::Ptr(new Profile(
                                       profileDir, m_Game, m_GameFeatures))));
    m_FailState = false;
  } catch (const std::exception& e) {
    reportError(tr("failed to create profile: %1").arg(e.what()));
  }
  ui->profilesList->addItem(newItem);
  return newItem;
}

void ProfilesDialog::createProfile(const QString& name, bool useDefaultSettings)
{
  try {
    auto profile =
        Profile::Ptr(new Profile(name, m_Game, m_GameFeatures, useDefaultSettings));
    QListWidgetItem* newItem = new QListWidgetItem;
    newItem->setData(Qt::UserRole, QVariant::fromValue(profile));
    setProfileItemName(newItem, name);
    ui->profilesList->addItem(newItem);
    m_FailState = false;
    ui->profilesList->setCurrentItem(newItem);
    emit profileCreated(profile.get());
  } catch (const std::exception&) {
    m_FailState = true;
    throw;
  }
}

void ProfilesDialog::createProfile(const QString& name, const Profile& reference)
{
  try {
    auto profile = Profile::Ptr(Profile::createPtrFrom(name, reference, m_Game));
    QListWidgetItem* newItem = new QListWidgetItem;
    newItem->setData(Qt::UserRole, QVariant::fromValue(profile));
    setProfileItemName(newItem, name);
    ui->profilesList->addItem(newItem);
    m_FailState = false;
    ui->profilesList->setCurrentItem(newItem);
    emit profileCreated(profile.get());
  } catch (const std::exception&) {
    m_FailState = true;
    throw;
  }
}

void ProfilesDialog::on_addProfileButton_clicked()
{
  ProfileInputDialog dialog(this);
  const bool okClicked = dialog.exec();
  const QString name   = dialog.getName();

  if (okClicked && !name.isEmpty()) {
    try {
      createProfile(name, dialog.getPreferDefaultSettings());
    } catch (const std::exception& e) {
      reportError(tr("failed to create profile: %1").arg(e.what()));
    }
  }
}

void ProfilesDialog::on_copyProfileButton_clicked()
{
  QListWidgetItem* item = ui->profilesList->currentItem();
  const Profile::Ptr currentProfile =
      item ? item->data(Qt::UserRole).value<Profile::Ptr>() : Profile::Ptr();
  if (!currentProfile) {
    return;
  }

  bool okClicked = false;
  const QString suggestedName = tr("%1 Copy").arg(currentProfile->name());
  QString name = QInputDialog::getText(this, tr("Duplicate profile"),
                                       tr("Name for the duplicate:"),
                                       QLineEdit::Normal, suggestedName, &okClicked);
  if (!okClicked) {
    return;
  }

  if (!fixDirectoryName(name)) {
    QMessageBox::warning(this, tr("Invalid name"), tr("Invalid profile name"));
    return;
  }

  try {
    createProfile(name, *currentProfile);
  } catch (const std::exception& e) {
    reportError(tr("failed to duplicate profile: %1").arg(e.what()));
  }
}

void ProfilesDialog::on_removeProfileButton_clicked()
{
  QListWidgetItem* currentItem = ui->profilesList->currentItem();
  if (currentItem == nullptr) {
    return;
  }

  const QString profileName = profileNameForItem(currentItem);
  Profile::Ptr profileToDelete =
      currentItem->data(Qt::UserRole).value<Profile::Ptr>();
  if (profileName == m_ActiveProfileName) {
    QMessageBox::warning(this, tr("Deleting active profile"),
                         tr("Unable to delete active profile. Please change to a "
                            "different profile first."));
    return;
  }

  QMessageBox confirmBox(QMessageBox::Question, tr("Confirm"),
                         tr("Are you sure you want to remove this profile (including "
                            "profile-specific save games, if any)?"),
                         QMessageBox::Yes | QMessageBox::No, this);
  if (confirmBox.exec() != QMessageBox::Yes) {
    return;
  }

  QString profilePath;
  if (!profileToDelete) {
    profilePath = QDir(Settings::instance().paths().profiles())
                      .absoluteFilePath(profileName);
    if (QMessageBox::question(
            this, tr("Profile broken"),
            tr("This profile you're about to delete seems to be broken or the path "
               "is invalid. I'm about to delete the following folder: \"%1\". "
               "Proceed?")
                .arg(profilePath),
            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
      return;
    }
  } else {
    // on destruction, the profile object would write the profile.ini file again, so
    // we have to get rid of the it before deleting the directory
    profilePath = profileToDelete->absolutePath();
  }

  QListWidgetItem* item = ui->profilesList->takeItem(ui->profilesList->currentRow());
  delete item;
  profileToDelete.reset();
  if (!shellDelete(QStringList(profilePath))) {
    log::warn("Failed to shell-delete \"{}\" (errorcode {}), trying regular delete",
              profilePath, errno);
    if (!removeDir(profilePath)) {
      log::warn("regular delete failed too");
    }
  }

  emit profileRemoved(profileName);
}

void ProfilesDialog::on_renameButton_clicked()
{
  QListWidgetItem* item = ui->profilesList->currentItem();
  const Profile::Ptr currentProfile =
      item ? item->data(Qt::UserRole).value<Profile::Ptr>() : Profile::Ptr();
  if (!currentProfile || item == nullptr) {
    return;
  }

  if (currentProfile->name() == m_ActiveProfileName) {
    QMessageBox::warning(this, tr("Renaming active profile"),
                         tr("The active profile cannot be renamed. Please change to a "
                            "different profile first."));
    return;
  }

  bool ok = false;
  QString name = QInputDialog::getText(this, tr("Rename profile"),
                                       tr("New profile name:"), QLineEdit::Normal,
                                       currentProfile->name(), &ok);
  if (!ok) {
    return;
  }
  if (!fixDirectoryName(name)) {
    QMessageBox::warning(this, tr("Invalid name"), tr("Invalid profile name"));
    return;
  }

  const QString oldName = currentProfile->name();
  if (name == oldName) {
    return;
  }

  try {
    currentProfile->rename(name);
  } catch (const std::exception& e) {
    reportError(tr("failed to rename profile: %1").arg(e.what()));
    return;
  }

  setProfileItemName(item, name);
  emit profileRenamed(currentProfile.get(), oldName, name);
}

void ProfilesDialog::on_invalidationBox_stateChanged(int state)
{
  if (!ui->invalidationBox->isEnabled()) {
    return;
  }

  QListWidgetItem* currentItem = ui->profilesList->currentItem();
  const Profile::Ptr currentProfile =
      currentItem ? currentItem->data(Qt::UserRole).value<Profile::Ptr>()
                  : Profile::Ptr();
  if (!currentProfile) {
    return;
  }

  try {
    if (state == Qt::Unchecked) {
      currentProfile->deactivateInvalidation();
    } else {
      currentProfile->activateInvalidation();
    }
  } catch (const std::exception& e) {
    reportError(tr("failed to change archive invalidation state: %1").arg(e.what()));
    on_profilesList_currentItemChanged(currentItem, nullptr);
  }
}

void ProfilesDialog::on_profilesList_currentItemChanged(QListWidgetItem* current,
                                                        QListWidgetItem*)
{
  const bool hasSelection = current != nullptr;
  const QString selectedName = profileNameForItem(current);
  const Profile::Ptr currentProfile =
      current ? current->data(Qt::UserRole).value<Profile::Ptr>() : Profile::Ptr();
  const bool hasProfile = static_cast<bool>(currentProfile);
  const bool isActive = hasSelection && selectedName == m_ActiveProfileName;

  ui->selectedProfileNameLabel->setText(hasSelection ? selectedName : tr("None"));
  ui->copyProfileButton->setEnabled(hasProfile);
  ui->renameButton->setEnabled(hasProfile && !isActive);
  // A broken profile can still be removed after the user confirms its folder.
  ui->removeProfileButton->setEnabled(hasSelection && !isActive);
  ui->select->setText(isActive ? tr("Current profile") : tr("Use profile"));
  ui->select->setEnabled(hasProfile && !isActive);

  const bool localSavesSupported =
      m_GameFeatures.gameFeature<LocalSavegames>() != nullptr;
  ui->localSavesBox->setEnabled(hasProfile && localSavesSupported);
  ui->localIniFilesBox->setEnabled(hasProfile);
  ui->invalidationBox->setEnabled(false);
  ui->transferButton->setEnabled(false);

  ui->invalidationBox->blockSignals(true);
  ui->invalidationBox->setChecked(false);
  ui->invalidationBox->blockSignals(false);
  ui->localSavesBox->blockSignals(true);
  ui->localSavesBox->setChecked(false);
  ui->localSavesBox->blockSignals(false);
  ui->localIniFilesBox->blockSignals(true);
  ui->localIniFilesBox->setChecked(false);
  ui->localIniFilesBox->blockSignals(false);

  if (!currentProfile) {
    return;
  }

  try {
    bool invalidationSupported = false;
    ui->invalidationBox->blockSignals(true);
    ui->invalidationBox->setChecked(
        currentProfile->invalidationActive(&invalidationSupported));
    ui->invalidationBox->setEnabled(invalidationSupported);
    ui->invalidationBox->blockSignals(false);

    const bool localSaves = currentProfile->localSavesEnabled();
    ui->localSavesBox->blockSignals(true);
    ui->localSavesBox->setChecked(localSaves);
    ui->localSavesBox->blockSignals(false);
    ui->transferButton->setEnabled(localSaves && localSavesSupported);

    ui->localIniFilesBox->blockSignals(true);
    ui->localIniFilesBox->setChecked(currentProfile->localSettingsEnabled());
    ui->localIniFilesBox->blockSignals(false);
  } catch (const std::exception& e) {
    ui->invalidationBox->blockSignals(false);
    ui->localSavesBox->blockSignals(false);
    ui->localIniFilesBox->blockSignals(false);
    reportError(tr("failed to determine profile options: %1").arg(e.what()));
    ui->copyProfileButton->setEnabled(false);
    ui->renameButton->setEnabled(false);
    ui->select->setEnabled(false);
    ui->invalidationBox->setEnabled(false);
    ui->localSavesBox->setEnabled(false);
    ui->localIniFilesBox->setEnabled(false);
    ui->transferButton->setEnabled(false);
  }
}

void ProfilesDialog::on_profilesList_itemActivated(QListWidgetItem* item)
{
  if (item != nullptr && item == ui->profilesList->currentItem()) {
    on_select_clicked();
  }
}

void ProfilesDialog::on_localSavesBox_stateChanged(int state)
{
  QListWidgetItem* item = ui->profilesList->currentItem();
  const Profile::Ptr currentProfile =
      item ? item->data(Qt::UserRole).value<Profile::Ptr>() : Profile::Ptr();
  if (!currentProfile) {
    return;
  }

  try {
    if (currentProfile->enableLocalSaves(state == Qt::Checked)) {
      ui->transferButton->setEnabled(state == Qt::Checked);
    } else {
      ui->localSavesBox->blockSignals(true);
      ui->localSavesBox->setChecked(state != Qt::Checked);
      ui->localSavesBox->blockSignals(false);
    }
  } catch (const std::exception& e) {
    ui->localSavesBox->blockSignals(true);
    ui->localSavesBox->setChecked(state != Qt::Checked);
    ui->localSavesBox->blockSignals(false);
    reportError(tr("failed to change profile save settings: %1").arg(e.what()));
  }
}

void ProfilesDialog::on_transferButton_clicked()
{
  QListWidgetItem* item = ui->profilesList->currentItem();
  const Profile::Ptr currentProfile =
      item ? item->data(Qt::UserRole).value<Profile::Ptr>() : Profile::Ptr();
  if (!currentProfile) {
    return;
  }

  TransferSavesDialog transferDialog(*currentProfile, m_Game, this);
  transferDialog.exec();
}

void ProfilesDialog::on_localIniFilesBox_stateChanged(int state)
{
  QListWidgetItem* item = ui->profilesList->currentItem();
  const Profile::Ptr currentProfile =
      item ? item->data(Qt::UserRole).value<Profile::Ptr>() : Profile::Ptr();
  if (!currentProfile) {
    return;
  }

  try {
    if (!currentProfile->enableLocalSettings(state == Qt::Checked)) {
      ui->localIniFilesBox->blockSignals(true);
      ui->localIniFilesBox->setChecked(state != Qt::Checked);
      ui->localIniFilesBox->blockSignals(false);
    }
  } catch (const std::exception& e) {
    ui->localIniFilesBox->blockSignals(true);
    ui->localIniFilesBox->setChecked(state != Qt::Checked);
    ui->localIniFilesBox->blockSignals(false);
    reportError(tr("failed to change profile INI settings: %1").arg(e.what()));
  }
}
