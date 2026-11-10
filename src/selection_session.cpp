#include <HolonightFileBrowser/selection_session.h>
#include <cmath>

namespace HolonightFileBrowser {
QByteArray SelectionSession::identity(int row) const { return order_.value(row); }
int SelectionSession::hiddenSelectedCount() const {
  return static_cast<int>(std::count_if(state_.selected.begin(), state_.selected.end(),
                                        [this](const auto& nativePath) { return !order_.contains(nativePath); }));
}
void SelectionSession::removeIneligible() {
  state_.selected.removeIf(
      [this](const auto& nativePath) { return nativePath.isEmpty() || ineligible_.contains(nativePath); });
}
void SelectionSession::setVisibleEntries(const QList<QByteArray>& order, const QList<QByteArray>& ineligible) {
  if (order_ == order && ineligible_ == ineligible) {
    return;
  }
  order_ = order;
  ineligible_ = ineligible;
  removeIneligible();
  emit changed();
}
void SelectionSession::reconcile(const QList<QByteArray>& available, bool settled) {
  if (!settled) {
    return;
  }
  const auto anchor = state_.anchor;
  const auto before = state_;
  state_.reconcile(available);
  if (!anchor.isEmpty() && state_.anchor.isEmpty() && mode_ == Mode::Visual) {
    mode_ = Mode::Mark;
  }
  if (before.selected != state_.selected || before.current != state_.current || before.anchor != state_.anchor) {
    emit changed();
  }
}
bool SelectionSession::isRowSelected(int row) const {
  const auto nativePath = identity(row);
  return !nativePath.isEmpty() && state_.selected.contains(nativePath);
}
void SelectionSession::restoreCursor(const QByteArray& nativePath) {
  if (state_.current == nativePath) {
    return;
  }
  state_.current = nativePath;
  emit changed();
}
void SelectionSession::move(int row) {
  const auto nativePath = identity(row);
  state_.current = nativePath;
  if (mode_ == Mode::Visual && !nativePath.isEmpty() && order_.contains(state_.anchor)) {
    state_.range(order_, nativePath);
  }
  removeIneligible();
  emit changed();
}
void SelectionSession::pointer(int row, bool control, bool shift) {
  const auto nativePath = identity(row);
  if (nativePath.isEmpty()) {
    return;
  }
  if (ineligible_.contains(nativePath)) {
    state_.current = nativePath;
    if (!control && !shift) {
      state_.selected.clear();
      mode_ = Mode::Normal;
    }
    emit changed();
    return;
  }
  if (shift) {
    state_.range(order_, nativePath, control);
  } else if (control) {
    state_.toggle(nativePath);
  } else {
    state_.single(nativePath);
  }
  removeIneligible();
  mode_ = control || shift ? Mode::Mark : Mode::Normal;
  emit changed();
}
void SelectionSession::beginRange(int row) {
  state_.single(identity(row));
  removeIneligible();
  mode_ = state_.current.isEmpty() ? Mode::Normal : Mode::Visual;
  emit changed();
}
void SelectionSession::beginMark(int row) {
  mode_ = Mode::Mark;
  state_.current = identity(row);
  if (state_.selected.isEmpty()) {
    state_.single(state_.current);
  }
  removeIneligible();
  emit changed();
}
void SelectionSession::toggle(int row) {
  if (identity(row).isEmpty() || ineligible_.contains(identity(row))) {
    return;
  }
  state_.toggle(identity(row));
  mode_ = Mode::Mark;
  emit changed();
}
void SelectionSession::swapEnds() {
  if (mode_ != Mode::Visual) {
    return;
  }
  std::swap(state_.anchor, state_.current);
  emit changed();
}
void SelectionSession::clear() {
  state_.anchor.clear();
  state_.selected.clear();
  mode_ = Mode::Normal;
  emit changed();
}
void SelectionSession::beginRectangle(bool additive) {
  rectangle_before_ = state_;
  rectangle_mode_ = mode_;
  rectangle_initial_ = additive ? state_.selected : QList<QByteArray>{};
  mode_ = Mode::Mark;
}
void SelectionSession::updateRectangle(const QList<int>& rows) {
  state_.selected = rectangle_initial_;
  for (const auto row : rows) {
    const auto nativePath = identity(row);
    if (!nativePath.isEmpty() && !state_.selected.contains(nativePath)) {
      state_.selected.append(nativePath);
    }
  }
  removeIneligible();
  mode_ = Mode::Mark;
  emit changed();
}
void SelectionSession::selectGridRectangle(QRectF area, QSizeF cell, int columns, qreal inset) {
  QList<int> rows;
  area = area.normalized();
  if (!area.isEmpty() && cell.width() > 2 * inset && cell.height() > 2 * inset && columns > 0) {
    const auto firstColumn = qBound(0, static_cast<int>(std::floor(area.left() / cell.width())), columns - 1);
    const auto lastColumn = qBound(0, static_cast<int>(std::floor(area.right() / cell.width())), columns - 1);
    const auto firstLine = qMax(0, static_cast<int>(std::floor(area.top() / cell.height())));
    const auto lastLine =
        qMin(static_cast<int>(order_.size()) / columns, static_cast<int>(std::floor(area.bottom() / cell.height())));
    for (int line = firstLine; line <= lastLine; ++line) {
      for (int column = firstColumn; column <= lastColumn; ++column) {
        const int row = (line * columns) + column;
        const QRectF tile((column * cell.width()) + inset, (line * cell.height()) + inset, cell.width() - (2 * inset),
                          cell.height() - (2 * inset));
        if (row < order_.size() && tile.intersects(area)) {
          rows.append(row);
        }
      }
    }
  }
  updateRectangle(rows);
}
void SelectionSession::cancelRectangle() {
  state_ = rectangle_before_;
  mode_ = rectangle_mode_;
  emit changed();
}
}  // namespace HolonightFileBrowser
