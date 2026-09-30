#include "nativeprocess.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
constexpr int kWaitTimeoutMs = 5000;

std::unique_ptr<QProcess> shellProcess(const QString& command)
{
  auto process = std::make_unique<QProcess>();
  process->setProgram(QStringLiteral("/bin/sh"));
  process->setArguments({QStringLiteral("-c"), command});
  process->setProcessChannelMode(QProcess::ForwardedChannels);
  return process;
}

bool waitForTerminal(const env::NativeProcess& process, int timeoutMs)
{
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeoutMs) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (process.status().state != env::ProcessState::Running) {
      return true;
    }
    QThread::msleep(5);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  return process.status().state != env::ProcessState::Running;
}

class KillManagedChildOnFailure
{
public:
  explicit KillManagedChildOnFailure(const env::NativeProcess& process)
      : m_process(process)
  {}

  ~KillManagedChildOnFailure()
  {
    if (!m_process ||
        m_process.status().state != env::ProcessState::Running) {
      return;
    }
    (void)m_process.sendSignal(SIGKILL);
    (void)waitForTerminal(m_process, 1000);
  }

private:
  env::NativeProcess m_process;
};

bool writeByte(int fd, char value)
{
  ssize_t written;
  do {
    written = ::write(fd, &value, 1);
  } while (written < 0 && errno == EINTR);
  return written == 1;
}

bool readExactWithDeadline(int fd, void* output, std::size_t size,
                           int timeoutMs)
{
  auto* bytes = static_cast<char*>(output);
  QElapsedTimer timer;
  timer.start();
  std::size_t offset = 0;
  while (offset < size && timer.elapsed() < timeoutMs) {
    pollfd watch{fd, POLLIN, 0};
    const int remaining = timeoutMs - static_cast<int>(timer.elapsed());
    int ready;
    do {
      ready = ::poll(&watch, 1, std::max(remaining, 1));
    } while (ready < 0 && errno == EINTR && timer.elapsed() < timeoutMs);
    if (ready <= 0 || (watch.revents & (POLLERR | POLLNVAL))) {
      return false;
    }

    ssize_t count;
    do {
      count = ::read(fd, bytes + offset, size - offset);
    } while (count < 0 && errno == EINTR);
    if (count <= 0) {
      return false;
    }
    offset += static_cast<std::size_t>(count);
  }
  return offset == size;
}

bool waitForChildWithDeadline(pid_t child, int& status, int timeoutMs)
{
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeoutMs) {
    const pid_t result = ::waitpid(child, &status, WNOHANG);
    if (result == child) {
      return true;
    }
    if (result < 0 && errno != EINTR) {
      return false;
    }
    QThread::msleep(5);
  }
  return false;
}

// A direct child owns a grandchild and deliberately does not reap it until
// the test writes to guardianRelease. The grandchild blocks on targetRelease,
// then exits with 37. This lets observe() see both a live non-child and an
// unreaped zombie without involving any installed application or game.
class NonChildFixture
{
public:
  NonChildFixture() = default;
  NonChildFixture(const NonChildFixture&) = delete;
  NonChildFixture& operator=(const NonChildFixture&) = delete;

  ~NonChildFixture()
  {
    releaseTarget();
    requestReap();
    if (m_guardian > 0 && !m_guardianReaped) {
      int status = 0;
      if (!waitForChildWithDeadline(m_guardian, status, 2000)) {
        (void)::kill(m_guardian, SIGKILL);
        (void)waitForChildWithDeadline(m_guardian, status, 2000);
      }
      m_guardianReaped = true;
    }
    closeFd(m_reportRead);
    closeFd(m_targetReleaseRead);
    closeFd(m_targetReleaseWrite);
    closeFd(m_guardianReleaseRead);
    closeFd(m_guardianReleaseWrite);
  }

  bool start(pid_t& targetPid)
  {
    int report[2]            = {-1, -1};
    int targetRelease[2]     = {-1, -1};
    int guardianRelease[2]   = {-1, -1};
    if (::pipe(report) != 0 || ::pipe(targetRelease) != 0 ||
        ::pipe(guardianRelease) != 0) {
      closeFd(report[0]);
      closeFd(report[1]);
      closeFd(targetRelease[0]);
      closeFd(targetRelease[1]);
      closeFd(guardianRelease[0]);
      closeFd(guardianRelease[1]);
      return false;
    }

    m_guardian = ::fork();
    if (m_guardian < 0) {
      closeFd(report[0]);
      closeFd(report[1]);
      closeFd(targetRelease[0]);
      closeFd(targetRelease[1]);
      closeFd(guardianRelease[0]);
      closeFd(guardianRelease[1]);
      return false;
    }

    if (m_guardian == 0) {
      closeFd(report[0]);
      closeFd(targetRelease[1]);
      closeFd(guardianRelease[1]);

      const pid_t grandchild = ::fork();
      if (grandchild < 0) {
        _exit(40);
      }
      if (grandchild == 0) {
        closeFd(report[1]);
        closeFd(guardianRelease[0]);
        char release = 0;
        ssize_t count;
        do {
          count = ::read(targetRelease[0], &release, 1);
        } while (count < 0 && errno == EINTR);
        _exit(count == 1 ? 37 : 41);
      }

      closeFd(targetRelease[0]);
      if (::write(report[1], &grandchild, sizeof(grandchild)) !=
          static_cast<ssize_t>(sizeof(grandchild))) {
        _exit(42);
      }
      closeFd(report[1]);

      char reap = 0;
      ssize_t count;
      do {
        count = ::read(guardianRelease[0], &reap, 1);
      } while (count < 0 && errno == EINTR);
      if (count != 1) {
        _exit(43);
      }

      int childStatus = 0;
      pid_t waited;
      do {
        waited = ::waitpid(grandchild, &childStatus, 0);
      } while (waited < 0 && errno == EINTR);
      _exit(waited == grandchild && WIFEXITED(childStatus) &&
                    WEXITSTATUS(childStatus) == 37
                ? 0
                : 44);
    }

    closeFd(report[1]);
    // Keep the read endpoints open in this parent until each write so a
    // fixture failure cannot raise SIGPIPE in the test runner.
    m_reportRead          = report[0];
    m_targetReleaseRead   = targetRelease[0];
    m_targetReleaseWrite  = targetRelease[1];
    m_guardianReleaseRead = guardianRelease[0];
    m_guardianReleaseWrite = guardianRelease[1];

    return readExactWithDeadline(m_reportRead, &targetPid, sizeof(targetPid),
                                 kWaitTimeoutMs) &&
           targetPid > 0;
  }

  bool releaseTarget()
  {
    if (m_targetReleaseWrite < 0) {
      return true;
    }
    const bool written = writeByte(m_targetReleaseWrite, 'x');
    closeFd(m_targetReleaseWrite);
    closeFd(m_targetReleaseRead);
    return written;
  }

  bool requestReap()
  {
    if (m_guardianReleaseWrite < 0) {
      return true;
    }
    const bool written = writeByte(m_guardianReleaseWrite, 'r');
    closeFd(m_guardianReleaseWrite);
    closeFd(m_guardianReleaseRead);
    return written;
  }

  bool waitForGuardian(int& status, int timeoutMs = kWaitTimeoutMs)
  {
    if (m_guardian <= 0 || m_guardianReaped) {
      return false;
    }
    if (!waitForChildWithDeadline(m_guardian, status, timeoutMs)) {
      return false;
    }
    m_guardianReaped = true;
    return true;
  }

private:
  static void closeFd(int& fd)
  {
    if (fd >= 0) {
      ::close(fd);
      fd = -1;
    }
  }

  pid_t m_guardian = -1;
  int m_reportRead = -1;
  int m_targetReleaseRead = -1;
  int m_targetReleaseWrite = -1;
  int m_guardianReleaseRead = -1;
  int m_guardianReleaseWrite = -1;
  bool m_guardianReaped = false;
};

int findPidfdFor(pid_t pid)
{
  DIR* directory = ::opendir("/proc/self/fd");
  if (!directory) {
    return -1;
  }

  int result = -1;
  while (const dirent* entry = ::readdir(directory)) {
    char* end = nullptr;
    const long descriptor = std::strtol(entry->d_name, &end, 10);
    if (!end || *end != '\0' || descriptor < 0) {
      continue;
    }

    const QString infoPath = QStringLiteral("/proc/self/fdinfo/%1")
                                 .arg(descriptor);
    QFile info(infoPath);
    if (!info.open(QIODevice::ReadOnly)) {
      continue;
    }
    const QByteArray contents = info.readAll();
    const QByteArray pidLine = QByteArrayLiteral("Pid:\t") +
                               QByteArray::number(pid) + '\n';
    if (!contents.contains(pidLine)) {
      continue;
    }

    char linkTarget[128] = {};
    const QString fdPath = QStringLiteral("/proc/self/fd/%1").arg(descriptor);
    const QByteArray encoded = QFile::encodeName(fdPath);
    const ssize_t length = ::readlink(encoded.constData(), linkTarget,
                                      sizeof(linkTarget) - 1);
    if (length > 0 &&
        std::string(linkTarget, static_cast<std::size_t>(length)) ==
            "anon_inode:[pidfd]") {
      result = static_cast<int>(descriptor);
      break;
    }
  }
  ::closedir(directory);
  return result;
}

}  // namespace

TEST(NativeProcess, CapturesFastExitAndRetainsStatusAfterQtDeletesProcess)
{
  auto process = shellProcess(QStringLiteral("exit 37"));
  auto environment = QProcessEnvironment::systemEnvironment();
  environment.insert(QStringLiteral("NATIVEPROCESS_TEST_VALUE"),
                     QStringLiteral("kept"));
  process->setProcessEnvironment(environment);

  auto native = env::NativeProcess::start(std::move(process));
  ASSERT_TRUE(native);
  ASSERT_GT(native.pid(), 0);
  EXPECT_EQ(native.processEnvironment().value(
                QStringLiteral("NATIVEPROCESS_TEST_VALUE")),
            QStringLiteral("kept"));

  const auto immediate = native.status();
  EXPECT_TRUE(immediate.state == env::ProcessState::Running ||
              immediate.state == env::ProcessState::Exited);
  ASSERT_TRUE(waitForTerminal(native, kWaitTimeoutMs));
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);

  const auto delayed = native.status();
  EXPECT_EQ(delayed.state, env::ProcessState::Exited);
  ASSERT_TRUE(delayed.exitCode.has_value());
  EXPECT_EQ(*delayed.exitCode, 37);
  EXPECT_FALSE(delayed.crashed);
  EXPECT_FALSE(delayed.error);
}

TEST(NativeProcess, ManagedSignalExitIsReportedAsCrash)
{
  auto native = env::NativeProcess::start(
      shellProcess(QStringLiteral("exec /bin/sleep 5")));
  ASSERT_TRUE(native);
  KillManagedChildOnFailure cleanup(native);
  ASSERT_EQ(native.status().state, env::ProcessState::Running);

  const auto signalError = native.sendSignal(SIGTERM);
  ASSERT_FALSE(signalError) << signalError.message();
  ASSERT_TRUE(waitForTerminal(native, kWaitTimeoutMs));

  const auto status = native.status();
  EXPECT_EQ(status.state, env::ProcessState::Exited);
  EXPECT_TRUE(status.crashed);
  ASSERT_TRUE(status.exitCode.has_value());
  EXPECT_NE(*status.exitCode, 0);
  EXPECT_FALSE(status.error);
}

TEST(NativeProcess, FailedStartHasErrorAndNoProcessId)
{
  auto process = std::make_unique<QProcess>();
  process->setProgram(QStringLiteral("/definitely/missing/fluorine-test-program"));
  auto native = env::NativeProcess::start(std::move(process));

  EXPECT_FALSE(native);
  EXPECT_EQ(native.pid(), 0);
  const auto status = native.status();
  EXPECT_EQ(status.state, env::ProcessState::Error);
  EXPECT_FALSE(status.error.message().empty());
  EXPECT_FALSE(native.errorString().isEmpty());
}

TEST(NativeProcess, DroppingOneReferenceLeavesManagedChildAlive)
{
  auto first = env::NativeProcess::start(
      shellProcess(QStringLiteral("exec /bin/sleep 5")));
  ASSERT_TRUE(first);
  KillManagedChildOnFailure cleanup(first);
  const pid_t pid = first.pid();
  auto observer = first;

  first = env::NativeProcess{};
  ASSERT_TRUE(observer);
  EXPECT_EQ(observer.pid(), pid);
  EXPECT_EQ(observer.status().state, env::ProcessState::Running);
  EXPECT_EQ(::kill(pid, 0), 0);

  const auto signalError = observer.sendSignal(SIGTERM);
  ASSERT_FALSE(signalError) << signalError.message();
  ASSERT_TRUE(waitForTerminal(observer, kWaitTimeoutMs));
  const auto status = observer.status();
  EXPECT_EQ(status.state, env::ProcessState::Exited);
  EXPECT_TRUE(status.crashed);
}

TEST(NativeProcess, ObservesUnreapedNonChildZombieWithoutReapingIt)
{
  NonChildFixture fixture;
  pid_t targetPid = -1;
  ASSERT_TRUE(fixture.start(targetPid));
  auto observer = env::NativeProcess::observe(targetPid);
  ASSERT_TRUE(observer);
  ASSERT_EQ(observer.pid(), targetPid);
  ASSERT_EQ(observer.status().state, env::ProcessState::Running);

  ASSERT_TRUE(fixture.releaseTarget());
  ASSERT_TRUE(waitForTerminal(observer, kWaitTimeoutMs));
  const auto status = observer.status();
  ASSERT_EQ(status.state, env::ProcessState::Exited);
  EXPECT_FALSE(status.exitCode.has_value());
  EXPECT_FALSE(status.error);
  // The guardian has not been given permission to waitpid() yet, so kill(0)
  // still sees this exited grandchild as a zombie. observe() must not reap it.
  EXPECT_EQ(::kill(targetPid, 0), 0);
  EXPECT_EQ(observer.sendSignal(SIGKILL),
            std::make_error_code(std::errc::no_such_process));

  ASSERT_TRUE(fixture.requestReap());
  int guardianStatus = 0;
  ASSERT_TRUE(fixture.waitForGuardian(guardianStatus));
  ASSERT_TRUE(WIFEXITED(guardianStatus));
  EXPECT_EQ(WEXITSTATUS(guardianStatus), 0);
  const int result = ::kill(targetPid, 0);
  const int error = errno;
  EXPECT_EQ(result, -1);
  EXPECT_EQ(error, ESRCH);
}

TEST(NativeProcess, ObserverDescriptorIsCloseOnExecAndReleasedWithLastReference)
{
  NonChildFixture fixture;
  pid_t targetPid = -1;
  ASSERT_TRUE(fixture.start(targetPid));

  auto first = env::NativeProcess::observe(targetPid);
  ASSERT_TRUE(first);
  ASSERT_EQ(first.status().state, env::ProcessState::Running);
  const int descriptor = findPidfdFor(targetPid);
  if (descriptor < 0) {
    GTEST_SKIP() << "pidfd_open is unavailable; /proc fallback remains covered";
  }
  const int flags = ::fcntl(descriptor, F_GETFD);
  ASSERT_GE(flags, 0);
  EXPECT_NE(flags & FD_CLOEXEC, 0);

  auto last = first;
  first = env::NativeProcess{};
  EXPECT_GE(findPidfdFor(targetPid), 0);
  EXPECT_EQ(::kill(targetPid, 0), 0);

  last = env::NativeProcess{};
  EXPECT_LT(findPidfdFor(targetPid), 0);
  EXPECT_EQ(::kill(targetPid, 0), 0);

  ASSERT_TRUE(fixture.releaseTarget());
  ASSERT_TRUE(fixture.requestReap());
  int guardianStatus = 0;
  ASSERT_TRUE(fixture.waitForGuardian(guardianStatus));
  ASSERT_TRUE(WIFEXITED(guardianStatus));
  EXPECT_EQ(WEXITSTATUS(guardianStatus), 0);
}

TEST(NativeProcess, RejectsNonPositivePids)
{
  for (const pid_t pid : {pid_t{0}, pid_t{-1}}) {
    const auto invalid = env::NativeProcess::observe(pid);
    EXPECT_FALSE(invalid);
    EXPECT_EQ(invalid.pid(), 0);
    const auto status = invalid.status();
    EXPECT_EQ(status.state, env::ProcessState::Error);
    EXPECT_EQ(status.error,
              std::make_error_code(std::errc::invalid_argument));
    EXPECT_EQ(invalid.sendSignal(0),
              std::make_error_code(std::errc::invalid_argument));
  }
}

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
