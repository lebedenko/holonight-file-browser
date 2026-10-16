#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

#include <HolonightFileBrowser/search/path_index_store.h>

namespace {
constexpr quint32 magic = 0x484e4631;
constexpr quint32 version = 2;
constexpr quint32 maximumPathBytes = 1024 * 1024;
QByteArray header(const QString& root, const QByteArray& policy) {
  QByteArray bytes;
  QDataStream stream(&bytes, QIODevice::WriteOnly);
  stream.setVersion(QDataStream::Qt_6_0);
  stream << magic << version << root << policy;
  return bytes;
}
}  // namespace

BrowserPathIndexStore::BrowserPathIndexStore(const QString& path, const QString& root, const QByteArray& policy)
    : file_(path) {
  file_.setDirectWriteFallback(false);
  valid_ = QDir().mkpath(QFileInfo(path).absolutePath()) && file_.open(QIODevice::WriteOnly);
  if (!valid_) {
    qWarning() << "Cannot write Files search cache:" << path << file_.errorString();
    return;
  }
  write(header(root, policy));
}

void BrowserPathIndexStore::write(const QByteArray& bytes) {
  if (!valid_) {
    return;
  }
  hash_.addData(bytes);
  valid_ = file_.write(bytes) == bytes.size();
  if (!valid_) {
    qWarning() << "Cannot write Files search cache:" << file_.errorString();
  }
}

void BrowserPathIndexStore::append(const QVector<PathCandidate>& batch) {
  if (!valid_) {
    return;
  }
  for (const auto& candidate : batch) {
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    const auto path = candidate.native_relative_path.isEmpty() ? QFile::encodeName(candidate.relative_path)
                                                               : candidate.native_relative_path;
    if (path.isEmpty() || path.size() > maximumPathBytes) {
      valid_ = false;
      return;
    }
    stream << static_cast<quint32>(path.size());
    stream.writeRawData(path.data(), static_cast<int>(path.size()));
    stream << static_cast<quint8>(candidate.directory ? 1 : 0);
    write(bytes);
  }
}

bool BrowserPathIndexStore::commit(qint64 completed, const std::atomic_bool& cancelled) {
  QByteArray footer;
  QDataStream stream(&footer, QIODevice::WriteOnly);
  stream << quint32{0} << completed;
  write(footer);
  const auto digest = hash_.result();
  if (!valid_ || cancelled.load()) {
    file_.cancelWriting();
    return false;
  }
  if (file_.write(digest) != digest.size()) {
    qWarning() << "Cannot write Files search cache:" << file_.errorString();
    file_.cancelWriting();
    return false;
  }
  if (!file_.commit()) {
    qWarning() << "Cannot publish Files search cache:" << file_.errorString();
    return false;
  }
  return true;
}

bool BrowserPathIndexStore::load(const QString& path, const QString& root, const QByteArray& policy,
                                 const std::atomic_bool& cancelled, const PathBatchReady& batchReady,
                                 qint64& completed) {
  QFile file(path);
  if (!file.exists()) {
    return false;
  }
  const auto expected = header(root, policy);
  if (!file.open(QIODevice::ReadOnly) || file.read(expected.size()) != expected) {
    qWarning() << "Incompatible or unreadable Files search cache:" << path;
    return false;
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  hash.addData(expected);
  auto nativeRoot = QFile::encodeName(QDir(root).absolutePath());
  if (!nativeRoot.endsWith('/')) {
    nativeRoot.append('/');
  }
  QVector<PathCandidate> batch;
  while (!cancelled.load()) {
    const auto lengthBytes = file.read(4);
    QDataStream lengthStream(lengthBytes);
    quint32 length = 0;
    lengthStream >> length;
    if (lengthBytes.size() != 4 || length > maximumPathBytes) {
      break;
    }
    hash.addData(lengthBytes);
    if (length == 0) {
      const auto timeBytes = file.read(8);
      QDataStream timeStream(timeBytes);
      timeStream >> completed;
      hash.addData(timeBytes);
      const auto digest = file.read(32);
      if (timeBytes.size() != 8 || completed <= 0 || completed > QDateTime::currentMSecsSinceEpoch() ||
          digest != hash.result() || !file.atEnd()) {
        break;
      }
      if (!batch.isEmpty()) {
        batchReady(std::move(batch));
      }
      return !cancelled.load();
    }
    const auto bytes = file.read(length);
    const auto type = file.read(1);
    const auto relative = QFile::decodeName(bytes);
    if (bytes.size() != length || type.size() != 1 || (type[0] != 0 && type[0] != 1) ||
        QDir::isAbsolutePath(relative) || QDir::cleanPath(relative) != relative || relative == QStringLiteral(".") ||
        relative == QStringLiteral("..") || relative.startsWith(QStringLiteral("../")) || relative.contains(QChar(0))) {
      break;
    }
    hash.addData(bytes);
    hash.addData(type);
    batch.append({.path = QDir(root).filePath(relative),
                  .relative_path = relative,
                  .directory = type[0] == 1,
                  .native_path = nativeRoot + bytes,
                  .native_relative_path = bytes});
    if (batch.size() == 256) {
      batchReady(std::move(batch));
      batch.clear();
    }
  }
  if (!cancelled.load()) {
    qWarning() << "Corrupt Files search cache:" << path;
  }
  return false;
}
