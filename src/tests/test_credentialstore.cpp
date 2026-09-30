#include "credentialstore.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>

TEST(CredentialStore, PersistsRemovesAndRestrictsPermissions)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const auto path = temp.filePath("config/credentials.ini");
  ASSERT_TRUE(CredentialStore::write(path, "test-token", "synthetic-token"));
  EXPECT_EQ(CredentialStore::read(path, "test-token"), "synthetic-token");
  struct stat info{};
  ASSERT_EQ(::stat(QFile::encodeName(path).constData(), &info), 0);
  EXPECT_EQ(info.st_mode & 0777, 0600);
  ASSERT_TRUE(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                        QFileDevice::ReadGroup | QFileDevice::ReadOther));
  EXPECT_EQ(CredentialStore::read(path, "test-token"), "synthetic-token");
  ASSERT_EQ(::stat(QFile::encodeName(path).constData(), &info), 0);
  EXPECT_EQ(info.st_mode & 0777, 0600);
  ASSERT_TRUE(CredentialStore::write(path, "test-token", {}));
  EXPECT_TRUE(CredentialStore::read(path, "test-token").isEmpty());
}

TEST(CredentialStore, RejectsBlockedDestinationsAndSymlinks)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const auto file = temp.filePath("blocker");
  QFile blocker(file);
  ASSERT_TRUE(blocker.open(QIODevice::WriteOnly));
  blocker.write("unchanged");
  blocker.close();
  EXPECT_FALSE(CredentialStore::write(file + "/credentials.ini", "token", "synthetic"));
  const auto link = temp.filePath("link.ini");
  ASSERT_TRUE(QFile::link(file, link));
  EXPECT_FALSE(CredentialStore::write(link, "token", "synthetic"));
  ASSERT_TRUE(blocker.open(QIODevice::ReadOnly));
  EXPECT_EQ(blocker.readAll(), "unchanged");
}

TEST(CredentialStore, ReportsCommitFailureAndPreservesPreviousContents)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const auto path = temp.filePath("credentials.ini");
  ASSERT_TRUE(CredentialStore::write(path, "token", "synthetic-old"));
  QFile original(path);
  ASSERT_TRUE(original.open(QIODevice::ReadOnly));
  const auto contents = original.readAll();
  original.close();
  const pid_t child = fork();
  ASSERT_GE(child, 0);
  if (child == 0) {
    signal(SIGXFSZ, SIG_IGN);
    const rlimit limit{64, 64};
    if (setrlimit(RLIMIT_FSIZE, &limit) != 0) _exit(2);
    _exit(CredentialStore::write(path, "token", QString(8192, 'x')) ? 1 : 0);
  }
  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  ASSERT_TRUE(WIFEXITED(status));
  EXPECT_EQ(WEXITSTATUS(status), 0);
  ASSERT_TRUE(original.open(QIODevice::ReadOnly));
  EXPECT_EQ(original.readAll(), contents);
}

TEST(CredentialStore, ReadsOwnerReadOnlyCredentials)
{
  QTemporaryDir temp;
  ASSERT_TRUE(temp.isValid());
  const auto path = temp.filePath("credentials.ini");
  ASSERT_TRUE(CredentialStore::write(path, "token", "synthetic"));
  ASSERT_TRUE(QFile::setPermissions(path, QFileDevice::ReadOwner));
  EXPECT_EQ(CredentialStore::read(path, "token"), "synthetic");
  struct stat info {};
  ASSERT_EQ(::stat(QFile::encodeName(path).constData(), &info), 0);
  EXPECT_EQ(info.st_mode & 0777, 0600);
}
