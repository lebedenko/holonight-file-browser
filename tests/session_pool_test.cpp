// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/preview/session_pool.h"

#include <QScopeGuard>
#include <QSemaphore>
#include <QTest>

#include <gtest/gtest.h>

TEST(PreviewScheduling, LatestPendingSelectionWaitsForActiveWorkToFinish) {
  QObject control;
  PreviewSessionPool pool("/absent-mutable-runtime", &control);
  QSemaphore started;
  QSemaphore release;
  auto first = std::make_shared<std::atomic_bool>(false);
  std::atomic_int ran{0};
  std::atomic_int failures{0};
  auto failure = [&](HolonightImages::RuntimeStatus, const QString&) { ++failures; };
  pool.submit(
      [&](HolonightImages::ReusableSession*, const HolonightImages::GenerationLease&) {
        started.release();
        release.acquire();
        return HolonightImages::RuntimeStatus::Cancelled;
      },
      first, failure);
  const auto unblock = qScopeGuard([&] {
    release.release();
    pool.shutdown();
  });
  ASSERT_TRUE(started.tryAcquire(1, 1000));
  first->store(true);
  pool.submit(
      [&](HolonightImages::ReusableSession*, const HolonightImages::GenerationLease&) {
        ran.store(1);
        return HolonightImages::RuntimeStatus::Success;
      },
      std::make_shared<std::atomic_bool>(false), failure);
  pool.submit(
      [&](HolonightImages::ReusableSession*, const HolonightImages::GenerationLease&) {
        ran.store(2);
        return HolonightImages::RuntimeStatus::Success;
      },
      std::make_shared<std::atomic_bool>(false), failure);
  QCoreApplication::processEvents();
  EXPECT_EQ(ran.load(), 0);
  release.release();
  ASSERT_TRUE(QTest::qWaitFor([&] { return ran.load() != 0; }));
  EXPECT_EQ(ran.load(), 2);
  EXPECT_EQ(failures.load(), 0);
}

namespace {
class PoolLifecycle : public testing::Test {
 protected:
  // GoogleTest generates derived tests that share this fixture state.
  // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
  QObject control;
  std::atomic<qint64> time{0};
  std::atomic_int launches{0};
  std::atomic_int cleanups{0};
  std::atomic_int live{0};
  std::atomic<qint64> memory{8LL * 1024 * 1024};
  std::atomic_bool fail_prepare{false};
  std::atomic_bool fail_cleanup{false};
  std::unique_ptr<PreviewSessionPool> pool;
  // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
  PreviewSessionPool::Operations operations() {
    return {.now = [&] { return time.load(); },
            .prepare =
                [&](size_t, const std::atomic_bool&) {
                  ++launches;
                  if (fail_prepare.load()) {
                    return HolonightImages::SessionLifecycleResult{
                        .runtime = HolonightImages::RuntimeStatus::Unavailable,
                        .diagnostic = "injected startup failure"};
                  }
                  EXPECT_LE(++live, 2);
                  return HolonightImages::SessionLifecycleResult{.runtime = HolonightImages::RuntimeStatus::Success,
                                                                 .diagnostic = {}};
                },
            .retire =
                [&](size_t) {
                  ++cleanups;
                  --live;
                  return HolonightImages::SessionLifecycleResult{
                      .runtime = fail_cleanup.load() ? HolonightImages::RuntimeStatus::CleanupFailure
                                                     : HolonightImages::RuntimeStatus::Success,
                      .diagnostic = {}};
                },
            .ready = [](size_t) { return true; },
            .memory = [&](size_t) { return memory.load(); }};
  }
  void start() { pool = std::make_unique<PreviewSessionPool>("/fake-managed-runtime", &control, operations()); }
  void TearDown() override {
    if (pool) {
      pool->shutdown();
      pool.reset();
    }
  }
  void transaction() {
    std::atomic_bool finished{false};
    pool->submit(
        [&](HolonightImages::ReusableSession*, const HolonightImages::GenerationLease&) {
          finished.store(true);
          return HolonightImages::RuntimeStatus::Success;
        },
        std::make_shared<std::atomic_bool>(false),
        [](auto, const auto& diagnostic) { ADD_FAILURE() << diagnostic.toStdString(); });
    ASSERT_TRUE(QTest::qWaitFor([&] { return finished.load() && pool->snapshot().active == 0; }));
  }
};
}  // namespace
TEST_F(PoolLifecycle, RecyclesAfter32StartedTransactionsWithinTwoHelperCap) {
  start();
  for (int number = 0; number < 33; ++number) {
    transaction();
  }
  ASSERT_TRUE(QTest::qWaitFor([&] { return cleanups.load() >= 1; }));
  EXPECT_LE(live.load(), 2);
}
TEST_F(PoolLifecycle, IdleExpiryDoesNotPrepareReplacementUntilAnotherDemand) {
  start();
  ASSERT_TRUE(QTest::qWaitFor([&] { return pool->snapshot().ready == 1; }));
  time.store(30000);
  pool->poll();
  ASSERT_TRUE(QTest::qWaitFor([&] { return live.load() == 0; }));
  QCoreApplication::processEvents();
  EXPECT_EQ(launches.load(), 1);
  transaction();
  EXPECT_GT(launches.load(), 1);
}
TEST_F(PoolLifecycle, IdleExpiryStillRetiresWhenOwnerStopsAuthorizingAcquisitions) {
  const auto authorized = std::make_shared<std::atomic_bool>(true);
  auto injected = operations();
  injected.reusable = [authorized] { return authorized->load(); };
  pool = std::make_unique<PreviewSessionPool>("/fake-managed-runtime", &control, std::move(injected));
  ASSERT_TRUE(QTest::qWaitFor([&] { return pool->snapshot().ready == 1; }));
  authorized->store(false);
  time.store(30000);
  pool->poll();
  ASSERT_TRUE(QTest::qWaitFor([&] { return live.load() == 0; }, 1000));
  EXPECT_EQ(launches.load(), 1);
}
TEST_F(PoolLifecycle, EnforcesAggregateIdleMemoryByRetiringWorkers) {
  memory.store(48LL * 1024 * 1024);
  start();
  transaction();
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return cleanups.load() != 0 && pool->snapshot().retiring == 0 && pool->snapshot().preparing == 0; }));
  EXPECT_LE(pool->snapshot().idle_memory, 64LL * 1024 * 1024);
  EXPECT_LE(live.load(), 1);
}
TEST_F(PoolLifecycle, PreparationFailureHasNoSelfScheduledRetry) {
  fail_prepare.store(true);
  start();
  ASSERT_TRUE(QTest::qWaitFor([&] { return pool->snapshot().preparing == 0; }));
  EXPECT_EQ(launches.load(), 1);
  std::atomic_int failed{0};
  pool->submit(
      [](auto*, const auto&) {
        ADD_FAILURE() << "failed preparation executed a transaction";
        return HolonightImages::RuntimeStatus::Success;
      },
      std::make_shared<std::atomic_bool>(false), [&](auto, const auto&) { ++failed; });
  ASSERT_TRUE(QTest::qWaitFor([&] { return failed.load() == 1; }));
  const auto attempts = launches.load();
  for (int iteration = 0; iteration < 5; ++iteration) {
    QCoreApplication::processEvents();
    pool->poll();
  }
  EXPECT_EQ(launches.load(), attempts);
  fail_prepare.store(false);
  transaction();
}
TEST_F(PoolLifecycle, CleanupFailurePermanentlyDisablesNewLaunches) {
  start();
  ASSERT_TRUE(QTest::qWaitFor([&] { return pool->snapshot().ready == 1; }));
  fail_cleanup.store(true);
  time.store(30000);
  pool->poll();
  ASSERT_TRUE(QTest::qWaitFor([&] { return pool->snapshot().disabled; }));
  const auto attempts = launches.load();
  std::atomic_int failed{0};
  pool->submit(
      [](auto*, const auto&) {
        ADD_FAILURE() << "disabled owner executed work";
        return HolonightImages::RuntimeStatus::Success;
      },
      std::make_shared<std::atomic_bool>(false),
      [&](auto status, const auto&) {
        EXPECT_EQ(status, HolonightImages::RuntimeStatus::CleanupFailure);
        ++failed;
      });
  ASSERT_TRUE(QTest::qWaitFor([&] { return failed.load() == 1; }));
  pool->refresh();
  EXPECT_EQ(launches.load(), attempts);
}

TEST_F(PoolLifecycle, ObsoletePreparationCannotBecomeReadyAfterRefresh) {
  QSemaphore entered;
  QSemaphore release;
  auto injected = operations();
  const auto prepare = injected.prepare;
  injected.prepare = [&](size_t index, const std::atomic_bool& cancelled) {
    entered.release();
    release.acquire();
    return prepare(index, cancelled);
  };
  pool = std::make_unique<PreviewSessionPool>("/fake-managed-runtime", &control, injected);
  const auto unblock = qScopeGuard([&] { release.release(); });
  ASSERT_TRUE(entered.tryAcquire(1, 1000));
  pool->refresh();
  release.release();
  ASSERT_TRUE(QTest::qWaitFor([&] { return cleanups.load() == 1 && pool->snapshot().retiring == 0; }));
  EXPECT_EQ(pool->snapshot().ready, 0);
  EXPECT_EQ(launches.load(), 1);
}
TEST_F(PoolLifecycle, AgeRetirementWaitsForInFlightTransaction) {
  start();
  QSemaphore entered;
  QSemaphore release;
  const auto unblock = qScopeGuard([&] { release.release(); });
  pool->submit(
      [&](auto*, const auto&) {
        entered.release();
        release.acquire();
        return HolonightImages::RuntimeStatus::Success;
      },
      std::make_shared<std::atomic_bool>(false), [](auto, const auto&) { ADD_FAILURE(); });
  ASSERT_TRUE(QTest::qWaitFor([&] { return entered.available() != 0 && pool->snapshot().ready == 1; }));
  time.store(65000);
  pool->poll();
  EXPECT_EQ(pool->snapshot().active, 1);
  release.release();
  ASSERT_TRUE(QTest::qWaitFor([&] { return pool->snapshot().active == 0; }));
  pool->poll();
  ASSERT_TRUE(QTest::qWaitFor([&] { return live.load() == 0; }));
}
TEST_F(PoolLifecycle, CancelledUsedWorkerCleanupBlocksDecodeOnReadyStandby) {
  QSemaphore started;
  QSemaphore release_job;
  QSemaphore cleaning;
  QSemaphore release_cleanup;
  std::atomic_bool first_cleanup{true};
  auto injected = operations();
  const auto retire = injected.retire;
  injected.retire = [&](size_t index) {
    if (first_cleanup.exchange(false)) {
      cleaning.release();
      release_cleanup.acquire();
    }
    return retire(index);
  };
  pool = std::make_unique<PreviewSessionPool>("/fake-managed-runtime", &control, injected);
  const auto unblock = qScopeGuard([&] {
    release_job.release();
    release_cleanup.release();
    pool->shutdown();
    pool.reset();
  });
  auto cancelled = std::make_shared<std::atomic_bool>(false);
  pool->submit(
      [&](auto*, const auto&) {
        started.release();
        release_job.acquire();
        return HolonightImages::RuntimeStatus::Success;
      },
      cancelled, [](auto, const auto&) { ADD_FAILURE(); });
  ASSERT_TRUE(QTest::qWaitFor([&] { return started.available() != 0 && pool->snapshot().ready == 1; }));
  cancelled->store(true);
  release_job.release();
  ASSERT_TRUE(cleaning.tryAcquire(1, 1000));
  std::atomic_bool replacement{false};
  pool->submit(
      [&](auto*, const auto&) {
        replacement.store(true);
        return HolonightImages::RuntimeStatus::Success;
      },
      std::make_shared<std::atomic_bool>(false), [](auto, const auto&) { ADD_FAILURE(); });
  QCoreApplication::processEvents();
  EXPECT_FALSE(replacement.load());
  release_cleanup.release();
  ASSERT_TRUE(QTest::qWaitFor([&] { return replacement.load(); }));
}
