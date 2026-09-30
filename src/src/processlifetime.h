#ifndef FLUORINE_PROCESSLIFETIME_H
#define FLUORINE_PROCESSLIFETIME_H

#include "nativeprocess.h"

#include <QStringList>
#include <functional>

namespace env
{
enum class ProcessWaitAction { Continue, Cancel, Unlock, Abort };
enum class ProcessWaitResult { Completed, Cancelled, Unlocked, Error };

struct ProcessCompletion
{
  ProcessWaitResult result = ProcessWaitResult::Error;
  int exitCode = -1;
};

using ProcessProgress =
    std::function<ProcessWaitAction(pid_t, const QString&)>;

// Waits for the launched workload, including Wine/Proton descendants. Cancel
// preserves the workload; Unlock terminates it only when terminateOnUnlock is
// true. Qt events continue to run on the calling thread.
ProcessCompletion waitForProcessTree(const NativeProcess& process,
                                     const QStringList& expectedExecutables,
                                     bool terminateOnUnlock = false,
                                     const ProcessProgress& progress = {});
}  // namespace env

#endif
