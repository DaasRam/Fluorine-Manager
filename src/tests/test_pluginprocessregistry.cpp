#include "pluginprocessregistry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

#include <unistd.h>

TEST(PluginProcessRegistry, TransfersAValidReferenceOnce)
{
  PluginProcessRegistry registry;
  const auto process = env::NativeProcess::observe(getpid());
  ASSERT_TRUE(process);

  const auto token = registry.insert(process, {QStringLiteral("skyrimse.exe")});
  ASSERT_NE(token, 0u);
  EXPECT_NE(token, std::numeric_limits<PluginProcessRegistry::Token>::max());

  auto transferred = registry.take(token);
  ASSERT_TRUE(transferred);
  ASSERT_TRUE(transferred->native);
  EXPECT_EQ(transferred->native.pid(), getpid());
  EXPECT_EQ(transferred->expectedExecutables,
            QStringList{QStringLiteral("skyrimse.exe")});
  EXPECT_FALSE(registry.take(token));
}

TEST(PluginProcessRegistry, RejectsInvalidAndReservedTokens)
{
  PluginProcessRegistry registry;
  EXPECT_EQ(registry.insert(env::NativeProcess{}), 0u);
  EXPECT_FALSE(registry.take(0));
  EXPECT_FALSE(registry.take(
      std::numeric_limits<PluginProcessRegistry::Token>::max()));
  EXPECT_FALSE(registry.take(1));
}

TEST(PluginProcessRegistry, TokensAreUniqueAndScopedToTheirRegistry)
{
  PluginProcessRegistry first;
  PluginProcessRegistry second;
  const auto process = env::NativeProcess::observe(getpid());
  ASSERT_TRUE(process);

  const auto firstToken = first.insert(process);
  const auto secondToken = second.insert(process);
  ASSERT_NE(firstToken, 0u);
  ASSERT_NE(secondToken, 0u);
  EXPECT_NE(firstToken, secondToken);
  EXPECT_FALSE(second.take(firstToken));

  auto fromFirst = first.take(firstToken);
  auto fromSecond = second.take(secondToken);
  ASSERT_TRUE(fromFirst);
  ASSERT_TRUE(fromSecond);
  EXPECT_EQ(fromFirst->native.pid(), getpid());
  EXPECT_EQ(fromSecond->native.pid(), getpid());
}

TEST(PluginProcessRegistry, ConcurrentInsertionsIssueDistinctTokens)
{
  constexpr int threadCount = 4;
  constexpr int insertionsPerThread = 16;
  PluginProcessRegistry registry;
  std::mutex tokensMutex;
  std::vector<PluginProcessRegistry::Token> tokens;
  tokens.reserve(threadCount * insertionsPerThread);

  std::vector<std::thread> threads;
  threads.reserve(threadCount);
  for (int thread = 0; thread < threadCount; ++thread) {
    threads.emplace_back([&] {
      for (int insertion = 0; insertion < insertionsPerThread; ++insertion) {
        auto process = env::NativeProcess::observe(getpid());
        const auto token = registry.insert(std::move(process));
        ASSERT_NE(token, 0u);
        std::lock_guard lock(tokensMutex);
        tokens.push_back(token);
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  std::sort(tokens.begin(), tokens.end());
  ASSERT_EQ(tokens.size(), threadCount * insertionsPerThread);
  EXPECT_TRUE(std::adjacent_find(tokens.begin(), tokens.end()) == tokens.end());
}
