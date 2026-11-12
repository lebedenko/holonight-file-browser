// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>

#include <array>
#include <functional>
#include <holonight_images/isolated.h>
#include <memory>

// Consumer policy only. Each slot owns its thread and Qt/process objects. The
// control thread calls these methods and remains available during slot work.
class PreviewSessionPool {
 public:
  using Job = std::function<HolonightImages::RuntimeStatus(HolonightImages::ReusableSession*,
                                                           const HolonightImages::GenerationLease&)>;
  using Failure = std::function<void(HolonightImages::RuntimeStatus, const QString&)>;
  // Injectable external boundaries for deterministic lifecycle tests. Production
  // uses the native owner/session path when no operations are supplied.
  struct Operations {
    std::function<qint64()> now;
    std::function<bool()> reusable;
    std::function<HolonightImages::SessionLifecycleResult(size_t, const std::atomic_bool&)> prepare;
    std::function<HolonightImages::SessionLifecycleResult(size_t)> retire;
    std::function<bool(size_t)> ready;
    std::function<qint64(size_t)> memory;
  };
  struct Snapshot {
    int preparing = 0;
    int ready = 0;
    int active = 0;
    int retiring = 0;
    qint64 idle_memory = 0;
    bool disabled = false;
  };
  PreviewSessionPool(QString runtime, QObject* control, Operations operations);
  [[nodiscard]] Snapshot snapshot() const;
  void poll();
  void setReadinessObserver(std::function<void(bool)> observer);
  explicit PreviewSessionPool(QString runtime, QObject* control);
  ~PreviewSessionPool();
  PreviewSessionPool(const PreviewSessionPool&) = delete;
  PreviewSessionPool& operator=(const PreviewSessionPool&) = delete;
  PreviewSessionPool(PreviewSessionPool&&) = delete;
  PreviewSessionPool& operator=(PreviewSessionPool&&) = delete;
  void submit(Job job, std::shared_ptr<std::atomic_bool> cancelled, Failure failure);
  void refresh();
  void shutdown();

 private:
  class State;
  std::unique_ptr<State> state_;
};
