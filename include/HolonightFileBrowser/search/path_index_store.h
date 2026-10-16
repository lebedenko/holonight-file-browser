#pragma once

#include <QCryptographicHash>
#include <QSaveFile>

#include <HolonightFileBrowser/search/path_scanner.h>

// Private, bounded-memory snapshot codec. All methods run on the traversal worker.
class BrowserPathIndexStore {
 public:
  static bool load(const QString& path, const QString& root, const QByteArray& policy,
                   const std::atomic_bool& cancelled, const PathBatchReady& batchReady, qint64& completed);
  BrowserPathIndexStore(const QString& path, const QString& root, const QByteArray& policy);
  void append(const QVector<PathCandidate>& batch);
  bool commit(qint64 completed, const std::atomic_bool& cancelled);

 private:
  void write(const QByteArray& bytes);
  QSaveFile file_;
  QCryptographicHash hash_{QCryptographicHash::Sha256};
  bool valid_ = false;
};
