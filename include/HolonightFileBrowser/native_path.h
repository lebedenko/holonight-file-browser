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
// Preserve the real-entry identity prefix and authoritative native path bytes.
inline QByteArray entryIdentity(const QByteArray& path) { return "entry:" + path; }
}  // namespace HolonightFileBrowser
