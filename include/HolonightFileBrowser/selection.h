#pragma once
#include <QByteArray>
#include <QList>

#include <algorithm>
namespace HolonightFileBrowser {
// Pure session state. Consumers decide when to reconcile or clear it.
class Selection {
 public:
  QByteArray current;
  QByteArray anchor;
  QList<QByteArray> selected;
  void clear() {
    current.clear();
    anchor.clear();
    selected.clear();
  }
  void single(const QByteArray& identity) {
    current = anchor = identity;
    selected = identity.isEmpty() ? QList<QByteArray>{} : QList<QByteArray>{identity};
  }
  void toggle(const QByteArray& identity) {
    if (identity.isEmpty()) {
      return;
    }
    current = anchor = identity;
    if (selected.removeAll(identity) == 0) {
      selected.append(identity);
    }
  }
  void range(const QList<QByteArray>& order, const QByteArray& identity, bool extend = false) {
    auto first = anchor.isEmpty() ? -1 : order.indexOf(anchor);
    auto last = order.indexOf(identity);
    if (last < 0) {
      return;
    }
    if (first < 0) {
      if (extend) {
        current = anchor = identity;
        if (!identity.isEmpty() && !selected.contains(identity)) {
          selected.append(identity);
        }
      } else {
        single(identity);
      }
      return;
    }
    current = identity;
    if (!extend) {
      selected.clear();
    }
    if (first > last) {
      std::swap(first, last);
    }
    for (auto row = first; row <= last; ++row) {
      if (!order[row].isEmpty() && !selected.contains(order[row])) {
        selected.append(order[row]);
      }
    }
  }
  void reconcile(const QList<QByteArray>& available) {
    selected.removeIf([&](const auto& identity) { return !available.contains(identity); });
    if (!available.contains(current)) {
      current.clear();
    }
    if (!available.contains(anchor)) {
      anchor.clear();
    }
  }
};
}  // namespace HolonightFileBrowser
