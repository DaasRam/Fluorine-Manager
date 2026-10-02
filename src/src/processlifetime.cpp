#include "processlifetime.h"
#include "launchenvironment.h"

#include <log.h>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <deque>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace env
{
namespace
{
using namespace MOBase;

QString readProcComm(pid_t pid)
{
  QFile f(QString("/proc/%1/comm").arg(pid));
  if (!f.open(QIODevice::ReadOnly)) {
    return {};
  }

  return QString::fromUtf8(f.readAll()).trimmed();
}

// Read /proc/<pid>/cmdline (NUL-separated) and return all argv entries.
QStringList readProcCmdline(pid_t pid)
{
  QFile f(QString("/proc/%1/cmdline").arg(pid));
  if (!f.open(QIODevice::ReadOnly)) {
    return {};
  }

  const QByteArray data = f.readAll();
  QStringList parts;
  for (const QByteArray& part : data.split('\0')) {
    if (!part.isEmpty()) {
      parts.push_back(QString::fromUtf8(part));
    }
  }
  return parts;
}

// Read a specific environment variable from /proc/<pid>/environ.
// The environ file is NUL-separated KEY=VALUE pairs.
QString readProcEnvVar(pid_t pid, const char* varName)
{
  QFile f(QString("/proc/%1/environ").arg(pid));
  if (!f.open(QIODevice::ReadOnly)) {
    return {};
  }

  const QByteArray data   = f.readAll();
  const QByteArray prefix = QByteArray(varName) + '=';
  for (const QByteArray& entry : data.split('\0')) {
    if (entry.startsWith(prefix)) {
      return QString::fromUtf8(entry.mid(prefix.size()));
    }
  }
  return {};
}

QByteArray readProcEnvironment(pid_t pid)
{
  QFile f(QString("/proc/%1/environ").arg(pid));
  return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{};
}

// Find a wineserver process owned by the current user that belongs to the
// given WINEPREFIX. When expectedPrefix is empty, returns the first
// wineserver owned by us (legacy behaviour).
//
// Wineserver stays alive as long as any Wine process in the prefix is
// running, making it the most reliable way to detect when a game has truly
// exited — even when launcher .exe's (nvse_loader, skse_loader, etc.) exit
// before the actual game.
env::NativeProcess findWineserver(const QString& expectedPrefix = {})
{
  const uid_t myUid = ::getuid();
  DIR* proc         = opendir("/proc");
  if (!proc)
    return {};

  env::NativeProcess result;
  struct dirent* entry = nullptr;
  while ((entry = readdir(proc)) != nullptr) {
    if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN)
      continue;
    const char* name = entry->d_name;
    if (*name == '\0' || !std::isdigit(static_cast<unsigned char>(*name)))
      continue;

    const pid_t pid    = static_cast<pid_t>(std::strtol(name, nullptr, 10));
    auto process = env::NativeProcess::observe(pid);
    const QString comm = readProcComm(pid);
    if (comm != "wineserver")
      continue;

    struct stat st;
    if (::stat(QString("/proc/%1").arg(pid).toStdString().c_str(), &st) != 0 ||
        st.st_uid != myUid) {
      continue;
    }

    // If a prefix filter was given, verify this wineserver belongs to it.
    // A wineserver without WINEPREFIX in its environ is using the default
    // ~/.wine prefix, which is never the game prefix — skip it too.
    if (!expectedPrefix.isEmpty()) {
      const QString wsPrefix = readProcEnvVar(pid, "WINEPREFIX");
      if (wsPrefix.isEmpty() ||
          QDir(wsPrefix).canonicalPath() != QDir(expectedPrefix).canonicalPath()) {
        log::debug("skipping wineserver {} (prefix '{}' != expected '{}')", pid,
                   wsPrefix.toStdString(), expectedPrefix.toStdString());
        continue;
      }
    }

    if (process.status().state != env::ProcessState::Running) continue;
    result = std::move(process);
    break;
  }
  closedir(proc);
  return result;
}

// Check whether any of the expected executable names appear in a process's
// comm or cmdline. Wine processes often show "wine64-preload" or "start.exe"
// in /proc/comm while the actual game executable only appears in cmdline.
// Also handles the 15-char TASK_COMM_LEN truncation in /proc/comm.
bool processMatchesExpected(pid_t pid, const QStringList& expected,
                            QString* matchedNameOut)
{
  // 1. Check /proc/comm (fast path).
  const QString comm = readProcComm(pid);
  if (!comm.isEmpty()) {
    const QString lower = comm.toLower();
    for (const QString& exp : expected) {
      if (lower == exp) {
        if (matchedNameOut)
          *matchedNameOut = comm;
        return true;
      }
      // Handle TASK_COMM_LEN truncation (15 chars): if the expected name is
      // longer than 15 chars, check if comm matches its first 15 chars.
      if (exp.size() > 15 && lower == exp.left(15)) {
        if (matchedNameOut)
          *matchedNameOut = exp;
        return true;
      }
    }
  }

  // 2. Check /proc/cmdline — Wine/Proton processes carry the .exe name here
  //    even when comm shows wine64-preloader or start.exe.
  const QStringList cmdline = readProcCmdline(pid);
  for (const QString& arg : cmdline) {
    const QString normalized = QString(arg).replace('\\', '/');
    const QString base       = QFileInfo(normalized).fileName().toLower();
    if (expected.contains(base)) {
      if (matchedNameOut)
        *matchedNameOut = QFileInfo(normalized).fileName();
      return true;
    }
  }

  return false;
}

std::unordered_map<pid_t, std::vector<pid_t>> buildProcChildrenMap()
{
  std::unordered_map<pid_t, std::vector<pid_t>> children;
  DIR* proc = opendir("/proc");
  if (!proc) {
    return children;
  }

  struct dirent* entry = nullptr;
  while ((entry = readdir(proc)) != nullptr) {
    if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) {
      continue;
    }

    const char* name = entry->d_name;
    if (*name == '\0' || !std::isdigit(static_cast<unsigned char>(*name))) {
      continue;
    }

    const pid_t pid = static_cast<pid_t>(std::strtol(name, nullptr, 10));
    std::ifstream status(QString("/proc/%1/status").arg(pid).toStdString());
    if (!status.is_open()) {
      continue;
    }

    std::string line;
    pid_t ppid = 0;
    while (std::getline(status, line)) {
      if (line.rfind("PPid:", 0) == 0) {
        ppid = static_cast<pid_t>(std::strtol(line.c_str() + 5, nullptr, 10));
        break;
      }
    }

    if (ppid > 0) {
      children[ppid].push_back(pid);
    }
  }

  closedir(proc);
  return children;
}

std::unordered_set<pid_t> collectDescendants(
    pid_t root, const std::unordered_map<pid_t, std::vector<pid_t>>& children)
{
  std::unordered_set<pid_t> out;
  std::deque<pid_t> q;
  q.push_back(root);

  while (!q.empty()) {
    const pid_t cur = q.front();
    q.pop_front();

    const auto it = children.find(cur);
    if (it == children.end()) {
      continue;
    }

    for (pid_t child : it->second) {
      if (out.insert(child).second) {
        q.push_back(child);
      }
    }
  }

  return out;
}

env::NativeProcess findTrackedProcess(pid_t rootPid, const QStringList& expected,
                                      QString* trackedNameOut)
{
  if (expected.isEmpty()) return {};
  const auto descendants = collectDescendants(rootPid, buildProcChildrenMap());
  for (pid_t pid : descendants) {
    auto process = env::NativeProcess::observe(pid);
    QString matched;
    if (processMatchesExpected(pid, expected, &matched) &&
        process.status().state == env::ProcessState::Running) {
      if (trackedNameOut) *trackedNameOut = matched;
      return process;
    }
  }
  return {};
}

// Scan every process owned by the current user for one whose comm or cmdline
// matches an expected game executable (e.g. FalloutNV.exe, SkyrimSE.exe).
// Used as a fallback after the immediate descendant tree loses the game —
// Proton's session manager can reparent game processes outside of our root
// PID's subtree, so a plain-descendant walk misses them. Wine candidates must
// belong to the launch's prefix (or carry its token); native candidates must
// carry the per-launch token so same-named games from another session are not
// tracked accidentally.
env::NativeProcess findGameProcessInPrefix(const QStringList& expected,
                              const QString& winePrefix,
                              const QString& launchToken,
                              QString* matchedNameOut, pid_t excludedPid)
{
  if (expected.isEmpty() ||
      (winePrefix.isEmpty() && launchToken.isEmpty())) {
    return {};
  }

  const uid_t myUid = ::getuid();
  DIR* proc         = opendir("/proc");
  if (!proc) {
    return {};
  }

  env::NativeProcess best;
  struct dirent* entry = nullptr;
  while ((entry = readdir(proc)) != nullptr) {
    if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN)
      continue;
    const char* name = entry->d_name;
    if (*name == '\0' || !std::isdigit(static_cast<unsigned char>(*name)))
      continue;

    const pid_t pid = static_cast<pid_t>(std::strtol(name, nullptr, 10));
    if (pid == excludedPid) continue;

    struct stat st;
    if (::stat(QString("/proc/%1").arg(pid).toStdString().c_str(), &st) != 0 ||
        st.st_uid != myUid) {
      continue;
    }

    auto process = env::NativeProcess::observe(pid);
    QString matched;
    if (!processMatchesExpected(pid, expected, &matched)) {
      continue;
    }

    // Match the process to this launch by prefix and/or inherited token.
    if (!processEnvironmentMatchesLaunch(readProcEnvironment(pid), winePrefix,
                                         launchToken)) {
      continue;
    }

    if (process.status().state != env::ProcessState::Running) {
      continue;
    }
    best = std::move(process);
    if (matchedNameOut)
      *matchedNameOut = matched;
    break;
  }
  closedir(proc);
  return best;
}

// A short-lived launcher can leave a nonmatching intermediary behind, which
// may exec the game only after the launcher exits. Keep a stable reference to
// one process from this launch while waiting for that handoff. The persistent
// wineserver is deliberately excluded: its lifetime is not a game lifetime.
env::NativeProcess findPendingLaunchProcess(const QString& launchToken,
                                           pid_t excludedPid)
{
  if (launchToken.isEmpty()) return {};

  const uid_t myUid = ::getuid();
  DIR* proc = opendir("/proc");
  if (!proc) return {};

  env::NativeProcess pending;
  struct dirent* entry = nullptr;
  while ((entry = readdir(proc)) != nullptr) {
    if (entry->d_type != DT_DIR && entry->d_type != DT_UNKNOWN) continue;
    const char* name = entry->d_name;
    if (*name == '\0' || !std::isdigit(static_cast<unsigned char>(*name))) continue;

    const pid_t pid = static_cast<pid_t>(std::strtol(name, nullptr, 10));
    if (pid == excludedPid) continue;

    struct stat st;
    if (::stat(QString("/proc/%1").arg(pid).toStdString().c_str(), &st) != 0 ||
        st.st_uid != myUid) {
      continue;
    }

    auto process = env::NativeProcess::observe(pid);
    if (!processEnvironmentMatchesLaunch(readProcEnvironment(pid), {},
                                         launchToken)) {
      continue;
    }
    if (readProcComm(pid) == "wineserver" ||
        process.status().state != env::ProcessState::Running) {
      continue;
    }
    pending = std::move(process);
    break;
  }
  closedir(proc);
  return pending;
}

// Signals target stable native references, never a PID that may have been
// reused between the discovery scan and force-unlock.
void signalProcess(const env::NativeProcess& process, int signal)
{
  const auto error = process.sendSignal(signal);
  if (error && error.value() != ESRCH) {
    log::warn("signal {} on process {} failed: {}", signal, process.pid(),
              error.message());
  }
}

void killWineserverForPrefix(const QString& winePrefix)
{
  if (winePrefix.isEmpty()) return;
  auto server = findWineserver(winePrefix);
  if (!server) return;
  signalProcess(server, SIGTERM);
  for (int i = 0; i < 10; ++i) {
    if (server.status().state != env::ProcessState::Running) return;
    QThread::msleep(100);
  }
  signalProcess(server, SIGKILL);
}

// Probes whether the root may still be signalled. ESRCH proves it has exited;
// any other failure (a sandbox denying pidfd_send_signal, a missing syscall)
// means it cannot be terminated and must be treated as still running.
enum class RootProbe { Alive, Exited, Unsignallable };

RootProbe probeRoot(const env::NativeProcess& root)
{
  const auto error = root.sendSignal(0);
  if (!error) return RootProbe::Alive;
  if (error.value() == ESRCH) return RootProbe::Exited;
  log::warn("cannot signal process {} for force unlock: {}", root.pid(),
            error.message());
  return RootProbe::Unsignallable;
}

// Returns true only when the root is known to have exited, so callers never
// tear down the VFS beneath a process that survived.
[[nodiscard]] bool killProcessTree(const env::NativeProcess& root)
{
  if (!root) return true;
  if (const auto probe = probeRoot(root); probe != RootProbe::Alive) {
    return probe == RootProbe::Exited;
  }

  const auto descendants = collectDescendants(root.pid(), buildProcChildrenMap());
  std::vector<env::NativeProcess> processes;
  for (pid_t pid : descendants) {
    processes.push_back(env::NativeProcess::observe(pid));
  }
  // Exclude IDs whose ancestry changed before we captured their identity.
  const auto currentDescendants =
      collectDescendants(root.pid(), buildProcChildrenMap());
  std::erase_if(processes, [&](const auto& process) {
    return !currentDescendants.contains(process.pid());
  });
  if (const auto probe = probeRoot(root); probe != RootProbe::Alive) {
    return probe == RootProbe::Exited;
  }
  processes.push_back(root);

  for (const auto& process : processes) signalProcess(process, SIGTERM);
  for (int i = 0; i < 5; ++i) {
    bool anyAlive = false;
    for (const auto& process : processes) {
      if (process.status().state == env::ProcessState::Running) {
        anyAlive = true;
        break;
      }
    }
    if (!anyAlive) return true;
    QThread::msleep(100);
  }
  for (const auto& process : processes) {
    if (process.status().state == env::ProcessState::Running) {
      signalProcess(process, SIGKILL);
    }
  }
  for (int i = 0; i < 20; ++i) {
    if (root.status().state == env::ProcessState::Exited) return true;
    // A QProcess-backed root only reports Exited once Qt delivers finished().
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(50);
  }
  log::error("process {} survived SIGKILL during force unlock", root.pid());
  return false;
}

}  // namespace

using namespace MOBase;

ProcessCompletion waitForProcessTree(const NativeProcess& process,
                                     const QStringList& expected,
                                     bool killTreeOnUnlock,
                                     const ProcessProgress& progress)
{
  if (!process) return {ProcessWaitResult::Error};
  const pid_t pid = process.pid();
  const auto environment = process.processEnvironment();
  const QString winePrefix = environment.contains("WINEPREFIX")
                                 ? environment.value("WINEPREFIX")
                                 : readProcEnvVar(pid, "WINEPREFIX");
  const QString launchToken = environment.contains(kFluorineLaunchTokenEnvironment)
      ? environment.value(kFluorineLaunchTokenEnvironment)
      : readProcEnvVar(pid, kFluorineLaunchTokenEnvironment);

  env::NativeProcess tracked;
  QString trackedName;
  env::NativeProcess pendingLaunch;
  bool seenTrackedProcess = false;
  QElapsedTimer noCandidateTimer;
  constexpr qint64 kNoCandidateGraceMs = 250;

  const auto complete = [&] {
    const auto status = process.status();
    // Unowned Wine/Proton descendants have no waitable exit status. Keep the
    // existing successful-completion policy for that case, while retaining
    // an authoritative root failure whenever Qt has reported one.
    return ProcessCompletion{ProcessWaitResult::Completed,
                             status.exitCode.value_or(0)};
  };

  while (true) {
    const auto rootStatus = process.status();
    if (rootStatus.state == env::ProcessState::Error) {
      log::error("failed monitoring process {}: {}", pid, rootStatus.error.message());
      return {ProcessWaitResult::Error};
    }
    const auto pendingStatus = pendingLaunch ? pendingLaunch.status()
                                             : env::ProcessStatus{};
    if (pendingLaunch && pendingStatus.state == env::ProcessState::Error) {
      log::error("failed monitoring pending launch process {}: {}",
                 pendingLaunch.pid(), pendingStatus.error.message());
      return {ProcessWaitResult::Error};
    }
    if (pendingLaunch && pendingStatus.state == env::ProcessState::Exited) {
      pendingLaunch = {};
    }

    const auto trackedStatus = tracked ? tracked.status() : env::ProcessStatus{};
    if (tracked && trackedStatus.state == env::ProcessState::Error) {
      log::error("failed monitoring game process {}: {}", tracked.pid(),
                 trackedStatus.error.message());
      return {ProcessWaitResult::Error};
    }
    if (!tracked || trackedStatus.state == env::ProcessState::Exited) {
      tracked = {};
      if (rootStatus.state == env::ProcessState::Running) {
        tracked = findTrackedProcess(pid, expected, &trackedName);
      }
      // Always rescan on root exit, including a launcher that exits before
      // our first poll and has already reparented the actual game.
      if (!tracked && (seenTrackedProcess ||
                       rootStatus.state == env::ProcessState::Exited)) {
        tracked = findGameProcessInPrefix(expected, winePrefix, launchToken,
                                           &trackedName, pid);
      }
      if (tracked) {
        if (pendingLaunch && pendingLaunch.pid() == tracked.pid()) {
          pendingLaunch = {};
        }
        noCandidateTimer.invalidate();
        seenTrackedProcess = true;
        log::info("tracking game process {}: {}", tracked.pid(), trackedName);
      } else if (rootStatus.state == env::ProcessState::Exited) {
        // An already-running process from this launch may exec the target much
        // later (for example while Proton initializes a prefix). Retain its
        // stable reference and keep polling it until it exits or becomes the
        // expected executable. Only bound the short interval with no known
        // launch process, so unrelated persistent wineservers do not delay a
        // completed application.
        if (!pendingLaunch && !expected.isEmpty()) {
          pendingLaunch = findPendingLaunchProcess(launchToken, pid);
        }
        if (pendingLaunch) {
          noCandidateTimer.invalidate();
        } else {
          if (!noCandidateTimer.isValid()) noCandidateTimer.start();
          if (noCandidateTimer.elapsed() >= kNoCandidateGraceMs) {
            return complete();
          }
        }
      }
    }

    const env::NativeProcess displayProcess = tracked ? tracked
        : pendingLaunch ? pendingLaunch : process;
    const pid_t displayPid = displayProcess.pid();
    const QString displayName = tracked ? trackedName : readProcComm(displayPid);
    if (progress) {
      const auto action = progress(displayPid, displayName);
      if (action == ProcessWaitAction::Abort) return {ProcessWaitResult::Error};
      if (action == ProcessWaitAction::Cancel) {
        // PreventExit's Cancel button cancels Fluorine's exit. Preserve the
        // game and its mounted VFS so the user can continue working.
        return {ProcessWaitResult::Cancelled, 0};
      }
      if (action == ProcessWaitAction::Unlock) {
        if (killTreeOnUnlock) {
          QString effectivePrefix = winePrefix;
          if (effectivePrefix.isEmpty() && tracked) {
            effectivePrefix = readProcEnvVar(tracked.pid(), "WINEPREFIX");
          }
          if (effectivePrefix.isEmpty() && pendingLaunch) {
            effectivePrefix = readProcEnvVar(pendingLaunch.pid(), "WINEPREFIX");
          }
          bool terminated = killProcessTree(process);
          if (tracked && tracked.pid() != pid) {
            terminated = killProcessTree(tracked) && terminated;
          }
          if (pendingLaunch && pendingLaunch.pid() != pid &&
              (!tracked || pendingLaunch.pid() != tracked.pid())) {
            terminated = killProcessTree(pendingLaunch) && terminated;
          }
          if (!terminated) {
            // Reporting Unlocked would make ProcessRunner unmount the VFS
            // beneath a game that is still running. Error keeps it mounted.
            log::error("force unlock could not terminate the game process "
                       "tree; keeping the VFS mounted");
            return {ProcessWaitResult::Error};
          }
          killWineserverForPrefix(effectivePrefix);
        }
        return {ProcessWaitResult::Unlocked, killTreeOnUnlock ? 1 : 0};
      }
    }

    // Qt owns child reaping and completion callbacks on its original thread.
    // This also keeps synchronous UI waits responsive; worker threads observe
    // the shared result while the application's main event loop runs.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    QThread::msleep(50);
  }
}

}  // namespace env
