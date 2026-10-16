#pragma once
#include <QByteArray>
#include <QUrl>
namespace HolonightFileBrowser {
inline bool absoluteNativePath(const QByteArray& path) { return path.startsWith('/') && !path.contains('\0'); }
inline QUrl localUrl(const QByteArray& path) {
  return absoluteNativePath(path) ? QUrl::fromEncoded("file://" + path.toPercentEncoding("/")) : QUrl{};
}
inline QByteArray nativePath(const QUrl& url) {
  if (!url.isLocalFile() || (!url.host().isEmpty() && url.host() != "localhost") || url.hasQuery() ||
      url.hasFragment()) {
    return {};
  }
  const auto path = QByteArray::fromPercentEncoding(url.path(QUrl::FullyEncoded).toLatin1());
  return absoluteNativePath(path) ? path : QByteArray{};
}
// Type prefixes distinguish synthetic entries from real entries with identical native paths.
inline QByteArray entryIdentity(const QByteArray& path, bool parent = false) {
  return (parent ? "parent:" : "entry:") + path;
}
}  // namespace HolonightFileBrowser
