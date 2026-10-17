#pragma once
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QStringList>

namespace HolonightFileBrowser {
// Native identities never pass through the lossy display labels.
class BreadcrumbAdapter : public QObject {
  Q_OBJECT
  Q_PROPERTY(QStringList labels READ labels NOTIFY changed)
 public:
  explicit BreadcrumbAdapter(QObject* parent = nullptr) : QObject(parent) {}
  void setPath(const QByteArray& path);
  [[nodiscard]] QStringList labels() const { return labels_; }
  [[nodiscard]] QByteArray nativePathAt(int index) const;
 signals:
  void changed();

 private:
  QStringList labels_;
  QList<QByteArray> paths_;
};
}  // namespace HolonightFileBrowser
