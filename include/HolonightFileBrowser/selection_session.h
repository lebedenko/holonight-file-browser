#pragma once

#include <QObject>
#include <QRectF>
#include <QSizeF>

#include <HolonightFileBrowser/selection.h>

namespace HolonightFileBrowser {
// Identity-based state for one browsing surface. Empty identities are placeholders.
// Consumers supply visible order separately from settled underlying directory entries.
class SelectionSession : public QObject {
  Q_OBJECT
  Q_PROPERTY(Mode mode READ mode NOTIFY changed)
  Q_PROPERTY(int selectedCount READ selectedCount NOTIFY changed)
  Q_PROPERTY(int hiddenSelectedCount READ hiddenSelectedCount NOTIFY changed)
  Q_PROPERTY(int cursorRow READ cursorRow NOTIFY changed)
  Q_PROPERTY(int anchorRow READ anchorRow NOTIFY changed)
 public:
  enum class Mode : quint8 { Normal, Visual, Mark };
  Q_ENUM(Mode)
  explicit SelectionSession(QObject* parent = nullptr) : QObject(parent) {}
  [[nodiscard]] Mode mode() const { return mode_; }
  [[nodiscard]] int selectedCount() const { return static_cast<int>(state_.selected.size()); }
  [[nodiscard]] int hiddenSelectedCount() const;
  [[nodiscard]] int cursorRow() const {
    return state_.current.isEmpty() ? -1 : static_cast<int>(order_.indexOf(state_.current));
  }
  [[nodiscard]] int anchorRow() const {
    return state_.anchor.isEmpty() ? -1 : static_cast<int>(order_.indexOf(state_.anchor));
  }
  [[nodiscard]] const QList<QByteArray>& selected() const { return state_.selected; }
  void setVisibleEntries(const QList<QByteArray>& order, const QList<QByteArray>& ineligible = {});
  void reconcile(const QList<QByteArray>& available, bool settled = true);
  Q_INVOKABLE [[nodiscard]] bool isRowSelected(int row) const;
  Q_INVOKABLE void move(int row);
  // Cursor restoration after a model change never extends a range.
  void restoreCursor(const QByteArray& nativePath);
  Q_INVOKABLE void pointer(int row, bool control = false, bool shift = false);
  Q_INVOKABLE void beginRange(int row);
  Q_INVOKABLE void beginMark(int row);
  Q_INVOKABLE void toggle(int row);
  Q_INVOKABLE void swapEnds();
  Q_INVOKABLE void clear();
  // Rectangle rows are computed from view geometry, independent of instantiated delegates.
  Q_INVOKABLE void beginRectangle(bool additive);
  Q_INVOKABLE void updateRectangle(const QList<int>& rows);
  Q_INVOKABLE void cancelRectangle();
  Q_INVOKABLE void selectGridRectangle(QRectF area, QSizeF cell, int columns, qreal inset = 4);
 signals:
  void changed();

 private:
  [[nodiscard]] QByteArray identity(int row) const;
  void removeIneligible();
  QList<QByteArray> ineligible_;
  Selection state_;
  QList<QByteArray> order_;
  QList<QByteArray> rectangle_initial_;
  Selection rectangle_before_;
  Mode rectangle_mode_ = Mode::Normal;
  Mode mode_ = Mode::Normal;
};
}  // namespace HolonightFileBrowser
