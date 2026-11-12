// SPDX-License-Identifier: GPL-3.0-or-later
#include "session_pool.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <array>
#include <optional>

using namespace HolonightImages;
class PreviewSessionPool::State {
 public:
  enum class Phase : unsigned char { Empty, Preparing, Ready, Active, Retiring };
  struct Slot {
    QThread thread;
    QObject* worker = nullptr;
    // Session is accessed and destroyed only by the slot thread.
    std::unique_ptr<ReusableSession> session;
    Phase phase = Phase::Empty;
    std::shared_ptr<std::atomic_bool> cancelled;
    quint64 epoch = 0;
    quint64 failed_demand = 0;
    bool failed = false;
    int started = 0;
    bool used = false;
    qint64 ready_at = 0;
    qint64 last_used = 0;
    qint64 memory = -1;
    bool fake_helper = false;
    bool cleanup_failed = false;
  };
  struct Pending {
    Job job;
    std::shared_ptr<std::atomic_bool> cancelled;
    Failure failure;
  };
  Operations operations;
  std::function<void(bool)> readiness_observer;
  QString directory;
  QObject* control;
  std::unique_ptr<RuntimeOwner> owner;
  std::array<Slot, 2> workers;
  std::optional<Pending> pending;
  QElapsedTimer clock;
  QTimer timer;
  quint64 epoch = 1;
  quint64 demand = 0;
  bool stopping = false;
  std::shared_ptr<std::atomic_bool> alive = std::make_shared<std::atomic_bool>(true);
  bool disabled = false;
  State(QString runtime, QObject* receiver, Operations injected = {})
      : operations(std::move(injected)), directory(std::move(runtime)), control(receiver) {
    clock.start();
    for (auto& slot : workers) {
      slot.worker = new QObject;
      slot.worker->moveToThread(&slot.thread);
      QObject::connect(&slot.thread, &QThread::finished, slot.worker, &QObject::deleteLater);
      slot.thread.start();
    }
    // Mutable development prefixes retain per-call verification and one-shot
    // execution. They never enter the prepared-session path.
    if (QFileInfo::exists(directory + "/current")) {
      owner = std::make_unique<RuntimeOwner>(directory);
    }
    QObject::connect(&timer, &QTimer::timeout, control, [this] {
      maintain();
      notifyReadiness();
    });
    timer.start(1000);
    if (reusable()) {
      prepare(0);
    }
  }
  [[nodiscard]] bool reusable() const {
    if (operations.reusable) {
      return operations.reusable();
    }
    if (operations.prepare) {
      return true;
    }
    const auto lease = owner ? owner->acquire() : GenerationLease{};
    return lease && lease.supportsSessions();
  }
  [[nodiscard]] qint64 now() const { return operations.now ? operations.now() : clock.elapsed(); }
  SessionLifecycleResult close(size_t index) {
    auto& slot = workers.at(index);
    if (operations.retire) {
      if (!slot.fake_helper) {
        return {.runtime = RuntimeStatus::Success, .diagnostic = {}};
      }
      slot.fake_helper = false;
      const auto result = operations.retire(index);
      slot.cleanup_failed = slot.cleanup_failed || result.runtime == RuntimeStatus::CleanupFailure;
      return result;
    }
    const auto result = slot.session ? slot.session->retire()
                                     : SessionLifecycleResult{.runtime = RuntimeStatus::Success, .diagnostic = {}};
    slot.session.reset();
    if (result.runtime == RuntimeStatus::CleanupFailure) {
      slot.cleanup_failed = true;
      qWarning() << "Preview runtime failure" << static_cast<int>(result.runtime) << result.diagnostic;
    }
    return result;
  }
  void notifyReadiness() {
    if (owner && owner->launchesDisabled()) {
      disabled = true;
    }
    if (!readiness_observer) {
      return;
    }
    bool ready = false;
    for (const auto& slot : workers) {
      ready = ready || slot.phase == Phase::Ready;
    }
    readiness_observer(ready && !occupied() && !disabled && !stopping);
  }
  void post(std::function<void()> callback) {
    const auto lifetime = alive;
    QMetaObject::invokeMethod(
        control,
        [this, lifetime, callback = std::move(callback)] {
          if (lifetime->load()) {
            callback();
          }
          if (lifetime->load()) {
            notifyReadiness();
          }
        },
        Qt::QueuedConnection);
  }
  [[nodiscard]] bool occupied() const {
    return std::ranges::any_of(
        workers, [](const Slot& slot) { return slot.phase == Phase::Active || slot.phase == Phase::Retiring; });
  }
  qint64 memory(size_t index) {
    if (operations.memory) {
      return operations.memory(index);
    }
    return workers.at(index).session ? workers.at(index).session->idleMemoryBytes() : -1;
  }
  void completePreparation(size_t index, const SessionLifecycleResult& prepared, quint64 preparing_epoch,
                           quint64 preparing_demand, qint64 idle_memory) {
    if (stopping) {
      return;
    }
    auto& target = workers.at(index);
    if (prepared.runtime == RuntimeStatus::CleanupFailure) {
      disabled = true;
    }
    if (prepared.runtime != RuntimeStatus::Success) {
      target.phase = Phase::Empty;
      target.failed = preparing_epoch == epoch;
      target.failed_demand = preparing_demand;
    } else {
      target.phase = Phase::Ready;
      target.ready_at = target.last_used = now();
      target.started = 0;
      target.memory = idle_memory;
      if (idle_memory < 0 || idle_memory > 64LL * 1024 * 1024) {
        target.failed = true;
        target.failed_demand = preparing_demand;
        retire(index);
      } else if (preparing_epoch != epoch) {
        retire(index);
      } else {
        enforceMemory();
      }
    }
    schedule();
  }
  void prepare(size_t index) {
    auto& slot = workers.at(index);
    if (stopping || disabled || slot.phase != Phase::Empty || !reusable()) {
      return;
    }
    const auto lease = owner ? owner->acquire() : GenerationLease{};
    if (!lease && !operations.prepare) {
      slot.failed = true;
      slot.failed_demand = demand;
      return;
    }
    slot.phase = Phase::Preparing;
    slot.epoch = epoch;
    slot.cancelled = std::make_shared<std::atomic_bool>(false);
    const auto cancel = slot.cancelled;
    const auto preparing_epoch = epoch;
    const auto preparing_demand = demand;
    QMetaObject::invokeMethod(
        slot.worker,
        [this, index, lease, cancel, preparing_epoch, preparing_demand] {
          auto& target = workers.at(index);
          SessionLifecycleResult prepared;
          if (operations.prepare) {
            prepared = operations.prepare(index, *cancel);
            target.fake_helper = prepared.runtime == RuntimeStatus::Success;
          } else {
            target.session = std::make_unique<ReusableSession>(lease);
            target.session->setTransactionStartedCallback([this, index] { transactionStarted(index); });
            prepared = target.session->prepare(*cancel);
            if (prepared.runtime != RuntimeStatus::Success) {
              target.session.reset();
            }
          }
          if (prepared.runtime == RuntimeStatus::CleanupFailure) {
            target.cleanup_failed = true;
            qWarning() << "Preview runtime failure" << static_cast<int>(prepared.runtime) << prepared.diagnostic;
          }
          const auto idle_memory = prepared.runtime == RuntimeStatus::Success ? memory(index) : -1;
          post([this, index, prepared, preparing_epoch, preparing_demand, idle_memory] {
            completePreparation(index, prepared, preparing_epoch, preparing_demand, idle_memory);
          });
        },
        Qt::QueuedConnection);
  }
  void retire(size_t index) {
    auto& slot = workers.at(index);
    if (slot.phase != Phase::Ready) {
      return;
    }
    slot.phase = Phase::Retiring;
    QMetaObject::invokeMethod(
        slot.worker,
        [this, index] {
          auto& target = workers.at(index);
          const auto retired = close(index);
          post([this, index, retired] {
            if (stopping) {
              return;
            }
            workers.at(index).phase = Phase::Empty;
            if (retired.runtime == RuntimeStatus::CleanupFailure) {
              disabled = true;
            }
            schedule();
          });
        },
        Qt::QueuedConnection);
  }
  void observe(size_t index) {
    const auto observing_epoch = workers.at(index).epoch;
    QMetaObject::invokeMethod(
        workers.at(index).worker,
        [this, index, observing_epoch] {
          const auto bytes = memory(index);
          post([this, index, observing_epoch, bytes] {
            if (stopping || workers.at(index).phase != Phase::Ready || workers.at(index).epoch != observing_epoch) {
              return;
            }
            if (bytes < 0) {
              retire(index);
              schedule();
              return;
            }
            workers.at(index).memory = bytes;
            enforceMemory();
          });
        },
        Qt::QueuedConnection);
  }
  void enforceMemory() {
    qint64 total = 0;
    std::optional<size_t> largest;
    for (size_t index = 0; index < workers.size(); ++index) {
      const auto& slot = workers.at(index);
      if (slot.phase != Phase::Ready) {
        continue;
      }
      if (slot.memory < 0) {
        continue;
      }  // Await this slot's queued observation.
      total += slot.memory;
      if (!largest || slot.memory > workers.at(*largest).memory) {
        largest = index;
      }
    }
    if (total > 64LL * 1024 * 1024 && largest) {
      retire(*largest);
      enforceMemory();
    }
  }
  void maintain() {
    // Revoked acquisition does not suspend retirement of existing workers.
    if (stopping) {
      return;
    }
    for (size_t index = 0; index < workers.size(); ++index) {
      const auto& slot = workers.at(index);
      if (slot.phase != Phase::Ready) {
        continue;
      }
      if (now() - slot.last_used >= 30000 || now() - slot.ready_at >= 60000) {
        retire(index);
      } else {
        observe(index);
      }
    }
  }
  bool launchReady() {
    for (size_t index = 0; index < workers.size(); ++index) {
      auto& slot = workers.at(index);
      if (slot.phase == Phase::Ready) {
        if (slot.epoch != epoch || now() - slot.ready_at >= 60000 || slot.started >= 32) {
          retire(index);
          return true;
        }
        launch(index);
        return true;
      }
    }
    return false;
  }
  void schedule() {
    if (stopping || !pending) {
      return;
    }
    if (pending->cancelled->load()) {
      pending.reset();
      return;
    }
    if (owner && owner->launchesDisabled()) {
      disabled = true;
    }
    if (disabled) {
      auto failure = std::move(pending->failure);
      pending.reset();
      failure(RuntimeStatus::CleanupFailure, "runtime owner disabled after cleanup failure");
      return;
    }
    // A cancelled dispatched request must finish verified retirement before any
    // standby may start decoding. Preparing helpers still count toward the cap.
    if (occupied()) {
      return;
    }
    if (owner && !owner->acquire()) {
      auto failure = std::move(pending->failure);
      pending.reset();
      failure(RuntimeStatus::Unavailable, owner->diagnostic());
      return;
    }
    if (!reusable()) {
      workers.at(0).phase = Phase::Ready;
      launch(0);
      return;
    }
    if (launchReady()) {
      return;
    }
    for (const auto& slot : workers) {
      if (slot.phase == Phase::Preparing) {
        return;
      }
    }
    // Only one demand worker is prepared here. Standby preparation belongs to
    // launch(), when a real session transaction starts.
    for (size_t index = 0; index < workers.size(); ++index) {
      auto& slot = workers.at(index);
      if (slot.phase == Phase::Empty && (!slot.failed || slot.failed_demand != demand)) {
        prepare(index);
        if (slot.phase == Phase::Preparing) {
          return;
        }
      }
    }
    auto failure = std::move(pending->failure);
    pending.reset();
    failure(RuntimeStatus::Unavailable,
            owner && !owner->diagnostic().isEmpty() ? owner->diagnostic() : "session preparation failed");
  }
  void transactionStarted(size_t index) {
    post([this, index] {
      ++workers.at(index).started;
      workers.at(index).used = true;
      for (size_t standby = 0; standby < workers.size(); ++standby) {
        if (standby != index && workers.at(standby).phase == Phase::Empty &&
            (!workers.at(standby).failed || workers.at(standby).failed_demand != demand)) {
          prepare(standby);
        }
      }
    });
  }
  void completeJob(size_t index, bool has_session, bool poisoned, RuntimeStatus cleanup_status, bool ran,
                   RuntimeStatus result, qint64 idle_memory, const std::shared_ptr<std::atomic_bool>& cancel) {
    if (stopping) {
      return;
    }
    auto& target = workers.at(index);
    if (cleanup_status == RuntimeStatus::CleanupFailure) {
      disabled = true;
    }
    target.phase = has_session && !poisoned ? Phase::Ready : Phase::Empty;
    target.memory = idle_memory;
    if (target.used) {
      target.last_used = now();
    }
    if (target.phase == Phase::Ready) {
      if ((ran && cancel->load() && result == RuntimeStatus::Success) || target.epoch != epoch ||
          target.started >= 32 || now() - target.ready_at >= 60000 || idle_memory < 0) {
        retire(index);
      } else {
        enforceMemory();
      }
    }
    schedule();
  }
  void executeJob(size_t index, const GenerationLease& lease, const Pending& job) {
    auto& target = workers.at(index);
    RuntimeStatus result = RuntimeStatus::Cancelled;
    bool ran = false;
    try {
      if (!job.cancelled->load()) {
        ran = true;
        if (operations.prepare) {
          transactionStarted(index);
        }
        result = job.job(target.session.get(), lease);
      }
    } catch (const std::exception& error) {
      result = RuntimeStatus::ProtocolFailure;
      job.failure(result, QString::fromUtf8(error.what()));
    } catch (...) {
      result = RuntimeStatus::ProtocolFailure;
      job.failure(result, "non-standard preview callback exception");
    }
    // A cancellation after acknowledgement still invalidates a used worker.
    // Cancellation before dispatch (Cancelled with a ready session) may keep it.
    const bool has_session = target.session || target.fake_helper;
    const bool session_ready = operations.ready ? operations.ready(index) : (target.session && target.session->ready());
    const bool poisoned =
        has_session && (!session_ready || (result != RuntimeStatus::Success && result != RuntimeStatus::Cancelled) ||
                        (ran && job.cancelled->load() && result == RuntimeStatus::Success));
    RuntimeStatus cleanup_status = RuntimeStatus::Success;
    if (poisoned) {
      cleanup_status = close(index).runtime;
    }
    const auto idle_memory = has_session && !poisoned ? memory(index) : 0;
    post([this, index, has_session, poisoned, cleanup_status, ran, result, idle_memory, cancel = job.cancelled] {
      completeJob(index, has_session, poisoned, cleanup_status, ran, result, idle_memory, cancel);
    });
  }
  void launch(size_t index) {
    const auto lease = owner ? owner->acquire() : GenerationLease{};
    if (owner && !lease) {
      auto failure = std::move(pending->failure);
      pending.reset();
      failure(owner->launchesDisabled() ? RuntimeStatus::CleanupFailure : RuntimeStatus::Unavailable,
              owner->diagnostic());
      return;
    }
    auto job = std::move(*pending);
    pending.reset();
    auto& slot = workers.at(index);
    slot.phase = Phase::Active;
    slot.used = false;
    slot.cancelled = job.cancelled;
    QMetaObject::invokeMethod(
        slot.worker, [this, index, lease, job = std::move(job)] { executeJob(index, lease, job); },
        Qt::QueuedConnection);
  }
  void shutdown() {
    if (stopping) {
      return;
    }
    stopping = true;
    alive->store(false);
    notifyReadiness();
    timer.stop();
    pending.reset();
    for (auto& slot : workers) {
      if (slot.cancelled) {
        slot.cancelled->store(true);
      }
      const auto index = static_cast<size_t>(&slot - workers.data());
      QMetaObject::invokeMethod(
          slot.worker,
          [this, index] {
            close(index);
            QThread::currentThread()->quit();
          },
          Qt::QueuedConnection);
    }
    for (auto& slot : workers) {
      slot.thread.wait();
      disabled = disabled || slot.cleanup_failed;
      slot.phase = Phase::Empty;
      slot.memory = 0;
    }
    owner.reset();
  }
};
PreviewSessionPool::PreviewSessionPool(QString runtime, QObject* control)
    : state_(std::make_unique<State>(std::move(runtime), control)) {}
PreviewSessionPool::PreviewSessionPool(QString runtime, QObject* control, Operations operations)
    : state_(std::make_unique<State>(std::move(runtime), control, std::move(operations))) {}
void PreviewSessionPool::poll() {
  state_->maintain();
  state_->notifyReadiness();
}
void PreviewSessionPool::setReadinessObserver(std::function<void(bool)> observer) {
  state_->readiness_observer = std::move(observer);
  state_->notifyReadiness();
}
PreviewSessionPool::Snapshot PreviewSessionPool::snapshot() const {
  Snapshot result;
  result.disabled = state_->disabled;
  for (const auto& slot : state_->workers) {
    switch (slot.phase) {
      case State::Phase::Empty:
        break;
      case State::Phase::Preparing:
        ++result.preparing;
        break;
      case State::Phase::Ready:
        ++result.ready;
        if (slot.memory > 0) {
          result.idle_memory += slot.memory;
        }
        break;
      case State::Phase::Active:
        ++result.active;
        break;
      case State::Phase::Retiring:
        ++result.retiring;
        break;
    }
  }
  return result;
}
PreviewSessionPool::~PreviewSessionPool() { state_->shutdown(); }
void PreviewSessionPool::submit(Job job, std::shared_ptr<std::atomic_bool> cancelled, Failure failure) {
  if (state_->stopping) {
    return;
  }
  ++state_->demand;
  state_->pending =
      State::Pending{.job = std::move(job), .cancelled = std::move(cancelled), .failure = std::move(failure)};
  state_->schedule();
  state_->notifyReadiness();
}
void PreviewSessionPool::refresh() {
  if (state_->stopping) {
    return;
  }
  ++state_->epoch;
  if (state_->owner) {
    state_->owner->refresh();
  }
  for (size_t index = 0; index < state_->workers.size(); ++index) {
    auto& slot = state_->workers.at(index);
    slot.failed = false;
    if (slot.phase == State::Phase::Preparing && slot.cancelled) {
      slot.cancelled->store(true);
    }
    if (slot.phase == State::Phase::Ready) {
      state_->retire(index);
    }
  }
  state_->schedule();
  state_->notifyReadiness();
}
void PreviewSessionPool::shutdown() { state_->shutdown(); }
