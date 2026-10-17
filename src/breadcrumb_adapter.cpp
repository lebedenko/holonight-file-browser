#include <QFile>

#include <HolonightFileBrowser/breadcrumb_adapter.h>
#include <HolonightFileBrowser/native_path.h>

namespace HolonightFileBrowser {
void BreadcrumbAdapter::setPath(const QByteArray& path) {
  labels_.clear();
  paths_.clear();
  if (absoluteNativePath(path)) {
    labels_.append(QStringLiteral("/"));
    paths_.append(QByteArray("/"));
    for (qsizetype end = 1; end < path.size();) {
      const auto start = end;
      while (end < path.size() && path[end] != '/') {
        ++end;
      }
      if (end > start) {
        labels_.append(QFile::decodeName(path.mid(start, end - start)));
        paths_.append(path.left(end));
      }
      ++end;
    }
  }
  emit changed();
}
QByteArray BreadcrumbAdapter::nativePathAt(int index) const {
  return index >= 0 && index < paths_.size() ? paths_.at(index) : QByteArray{};
}
}  // namespace HolonightFileBrowser
