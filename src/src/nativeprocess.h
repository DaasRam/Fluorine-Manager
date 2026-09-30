#ifndef FLUORINE_NATIVEPROCESS_H
#define FLUORINE_NATIVEPROCESS_H

#include <QProcessEnvironment>
#include <QString>

#include <memory>
#include <optional>
#include <system_error>
#include <sys/types.h>

class QProcess;

namespace env
{

enum class ProcessState { Running, Exited, Error };

struct ProcessStatus
{
  ProcessState state = ProcessState::Running;
  std::optional<int> exitCode;
  std::error_code error;
  bool crashed = false;
};

// A shared reference to one Linux process. Qt is the sole reaper of children
// launched by start(); observed processes are never reaped here. Dropping a
// reference closes its monitoring descriptor, never terminates the process.
class NativeProcess
{
public:
  NativeProcess() = default;

  // Takes a configured, not-yet-started QProcess. Its original thread owns it
  // until finished, while this reference retains the completion result.
  static NativeProcess start(std::unique_ptr<QProcess> process,
                             int timeoutMs = 5000);
  static NativeProcess observe(pid_t pid);

  explicit operator bool() const;
  pid_t pid() const;
  ProcessStatus status() const;
  std::error_code sendSignal(int signal) const;
  QString errorString() const;
  QProcessEnvironment processEnvironment() const;

private:
  struct State;
  explicit NativeProcess(std::shared_ptr<State> state);
  std::shared_ptr<State> m_state;
};

}  // namespace env

#endif
