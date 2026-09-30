#include "nativeprocess.h"

#include <QProcess>

#include <array>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <string>
#include <sys/syscall.h>
#include <unistd.h>

namespace env
{
namespace
{
std::error_code nativeError(int code)
{
  return {code, std::generic_category()};
}

bool processGone(int error)
{
  return error == ENOENT || error == ESRCH;
}
}  // namespace

struct NativeProcess::State
{
  mutable std::mutex mutex;
  pid_t pid = 0;
  bool managedByQt = false;
  int descriptor = -1;
  bool pollable = false;
  std::error_code monitorError;
  ProcessStatus result;
  QString message;
  QProcessEnvironment environment;

  ~State() { closeMonitor(); }

  void closeMonitor()
  {
    if (descriptor >= 0) {
      // close() must not be retried on Linux: the fd may already be reused.
      ::close(descriptor);
      descriptor = -1;
    }
  }

  void openMonitor()
  {
#ifdef SYS_pidfd_open
    do {
      descriptor = static_cast<int>(::syscall(SYS_pidfd_open, pid, 0));
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor >= 0) {
      pollable = true;
      return;
    }
    if (errno == ESRCH) {
      monitorError = nativeError(ESRCH);
      return;
    }
#endif
    // Older kernels or sandbox policies may not allow pidfd_open. An open
    // /proc directory still refers to this process after its PID is recycled.
    const std::string path = "/proc/" + std::to_string(pid);
    do {
      descriptor = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) {
      monitorError = nativeError(errno);
    }
  }

  ProcessStatus observeStatus()
  {
    if (result.state != ProcessState::Running || managedByQt) {
      return result;
    }

    if (descriptor < 0) {
      if (processGone(monitorError.value())) {
        result.state = ProcessState::Exited;
      } else {
        result.state = ProcessState::Error;
        result.error = monitorError;
      }
      return result;
    }

    if (pollable) {
      pollfd watched{descriptor, POLLIN, 0};
      int ready;
      do {
        ready = ::poll(&watched, 1, 0);
      } while (ready < 0 && errno == EINTR);
      if (ready < 0) {
        result = {ProcessState::Error, {}, nativeError(errno)};
      } else if (watched.revents & (POLLIN | POLLHUP)) {
        // A zombie has exited even when its parent has not reaped it yet.
        result.state = ProcessState::Exited;
      } else if (watched.revents & (POLLNVAL | POLLERR)) {
        result = {ProcessState::Error, {}, nativeError(EBADF)};
      }
    } else {
      int statFd;
      do {
        statFd = ::openat(descriptor, "stat", O_RDONLY | O_CLOEXEC);
      } while (statFd < 0 && errno == EINTR);
      if (statFd < 0) {
        if (processGone(errno)) {
          result.state = ProcessState::Exited;
        } else {
          result = {ProcessState::Error, {}, nativeError(errno)};
        }
      } else {
        std::array<char, 4096> buffer{};
        ssize_t length;
        do {
          length = ::read(statFd, buffer.data(), buffer.size());
        } while (length < 0 && errno == EINTR);
        const int readError = errno;
        ::close(statFd);
        if (length < 0) {
          if (processGone(readError)) {
            result.state = ProcessState::Exited;
          } else {
            result = {ProcessState::Error, {}, nativeError(readError)};
          }
        } else {
          const std::string stat(buffer.data(), static_cast<std::size_t>(length));
          const auto nameEnd = stat.rfind(')');
          if (nameEnd == std::string::npos || nameEnd + 2 >= stat.size()) {
            result = {ProcessState::Error, {}, nativeError(EIO)};
          } else if (const char state = stat[nameEnd + 2];
                     state == 'Z' || state == 'X' || state == 'x') {
            result.state = ProcessState::Exited;
          }
        }
      }
    }

    if (result.state != ProcessState::Running) {
      closeMonitor();
    }
    return result;
  }
};

NativeProcess::NativeProcess(std::shared_ptr<State> state) : m_state(std::move(state)) {}

NativeProcess NativeProcess::start(std::unique_ptr<QProcess> process, int timeoutMs)
{
  if (!process || process->state() != QProcess::NotRunning) {
    return {};
  }

  auto state = std::make_shared<State>();
  state->managedByQt = true;
  state->environment = process->processEnvironment();
  auto* child = process.get();

  QObject::connect(child, &QProcess::started, child, [state, child] {
    std::scoped_lock lock(state->mutex);
    state->pid = static_cast<pid_t>(child->processId());
    state->openMonitor();
  });
  QObject::connect(child, &QProcess::finished, child,
                   [state](int code, QProcess::ExitStatus status) {
    std::scoped_lock lock(state->mutex);
    // Qt only documents code for NormalExit. Preserve a crash as failure
    // without interpreting an undocumented platform-dependent signal value.
    state->result = {ProcessState::Exited,
                     status == QProcess::NormalExit ? code : EXIT_FAILURE,
                     {}, status == QProcess::CrashExit};
    state->closeMonitor();
  });
  QObject::connect(child, &QProcess::finished, child, &QObject::deleteLater);
  QObject::connect(child, &QObject::destroyed, [state] {
    std::scoped_lock lock(state->mutex);
    if (state->result.state == ProcessState::Running) {
      state->result = {ProcessState::Error, {}, nativeError(ECANCELED)};
      state->closeMonitor();
    }
  });

  child->start();
  if (!child->waitForStarted(timeoutMs)) {
    std::scoped_lock lock(state->mutex);
    state->message = child->errorString();
    state->result = {ProcessState::Error, {}, nativeError(EIO)};
    state->closeMonitor();
    return NativeProcess(std::move(state));
  }

  // The finished connection owns Qt cleanup; retaining or dropping a native
  // reference never destroys a running QProcess or closes its output pipes.
  process.release();
  return NativeProcess(std::move(state));
}

NativeProcess NativeProcess::observe(pid_t pid)
{
  if (pid <= 0) {
    return {};
  }
  auto state = std::make_shared<State>();
  state->pid = pid;
  state->openMonitor();
  return NativeProcess(std::move(state));
}

NativeProcess::operator bool() const { return pid() > 0; }

pid_t NativeProcess::pid() const
{
  if (!m_state) return 0;
  std::scoped_lock lock(m_state->mutex);
  return m_state->pid;
}

ProcessStatus NativeProcess::status() const
{
  if (!m_state) return {ProcessState::Error, {}, nativeError(EINVAL)};
  std::scoped_lock lock(m_state->mutex);
  return m_state->observeStatus();
}

std::error_code NativeProcess::sendSignal(int signal) const
{
  if (!m_state) return nativeError(EINVAL);
  std::scoped_lock lock(m_state->mutex);
  const auto current = m_state->observeStatus();
  if (current.state == ProcessState::Exited) return nativeError(ESRCH);
  if (current.state == ProcessState::Error) return current.error;
  if (m_state->descriptor < 0) return m_state->monitorError;
#ifdef SYS_pidfd_send_signal
  int result;
  do {
    // Linux also accepts an open /proc/<pid> directory here. Never fall back
    // to kill(rawPid), which could signal a different process after PID reuse.
    result = static_cast<int>(::syscall(SYS_pidfd_send_signal,
                                       m_state->descriptor, signal, nullptr, 0));
  } while (result < 0 && errno == EINTR);
  return result == 0 ? std::error_code{} : nativeError(errno);
#else
  return nativeError(ENOSYS);
#endif
}

QString NativeProcess::errorString() const
{
  if (!m_state) return QString::fromStdString(nativeError(EINVAL).message());
  std::scoped_lock lock(m_state->mutex);
  return !m_state->message.isEmpty()
             ? m_state->message
             : QString::fromStdString(m_state->result.error.message());
}

QProcessEnvironment NativeProcess::processEnvironment() const
{
  if (!m_state) return {};
  std::scoped_lock lock(m_state->mutex);
  return m_state->environment;
}

}  // namespace env
