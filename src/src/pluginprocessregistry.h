#pragma once

#include "nativeprocess.h"

#include <QStringList>

#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

// Adapts native process references to the opaque integer-shaped HANDLE used by
// the legacy plugin API. Tokens are scoped to a registry (normally one
// OrganizerProxy) and can be consumed by waitForApplication exactly once.
// Native process identity and ownership remain inside the application core;
// tokens are never interpreted as PIDs.
class PluginProcessRegistry
{
public:
  using Token = std::uintptr_t;

  struct Process
  {
    env::NativeProcess native;
    QStringList expectedExecutables;
  };

  // Returns 0 when the process is invalid or the token space is exhausted.
  Token insert(env::NativeProcess process,
               QStringList expectedExecutables = {})
  {
    if (!process) {
      return 0;
    }

    const Token token = allocateToken();
    if (token == 0) {
      return 0;
    }

    std::lock_guard lock(m_mutex);
    m_processes.emplace(
        token, Process{std::move(process), std::move(expectedExecutables)});
    return token;
  }

  // Removes and returns a registered process reference plus its launch
  // tracking context. Unknown, consumed, cross-registry, and reserved tokens
  // all fail without PID fallback.
  std::optional<Process> take(Token token)
  {
    if (token == 0 || token == invalidToken()) {
      return std::nullopt;
    }

    std::lock_guard lock(m_mutex);
    const auto it = m_processes.find(token);
    if (it == m_processes.end()) {
      return std::nullopt;
    }

    std::optional<Process> process(std::move(it->second));
    m_processes.erase(it);
    return process;
  }

private:
  static constexpr Token invalidToken()
  {
    return std::numeric_limits<Token>::max();
  }

  static Token allocateToken()
  {
    Token current = s_nextToken.load(std::memory_order_relaxed);
    while (current != 0 && current != invalidToken()) {
      const Token next = current + 1;
      if (s_nextToken.compare_exchange_weak(current, next,
                                            std::memory_order_relaxed,
                                            std::memory_order_relaxed)) {
        return current;
      }
    }
    return 0;
  }

  inline static std::atomic<Token> s_nextToken{1};
  std::mutex m_mutex;
  std::unordered_map<Token, Process> m_processes;
};
