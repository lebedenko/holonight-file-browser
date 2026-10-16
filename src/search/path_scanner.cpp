#include <QFile>

#include <HolonightFileBrowser/directory_scan.h>
#include <HolonightFileBrowser/native_path.h>
#include <HolonightFileBrowser/search/path_scanner.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
void scanFolder(const QByteArray& folder, const QByteArray& cleaned, const QByteArray& prefix,
                QList<QByteArray>& pending, QVector<PathCandidate>& batch, bool includeHidden,
                const SearchExclusionPolicy& policy, const std::atomic_bool& cancelled,
                const PathBatchReady& batchReady) {
  auto cancel = std::shared_ptr<const std::atomic_bool>(&cancelled, [](auto*) {});
  HolonightFileBrowser::walkDirectory(
      QFile::decodeName(folder), cancel, {}, -1, {},
      [&](const QList<DirectoryEntry>& entries, bool, const QString&) {
        for (const auto& entry : entries) {
          if (cancelled.load()) {
            return;
          }
          if (entry.is_parent || (!includeHidden && entry.name.startsWith(u'.')) ||
              (entry.is_symlink && entry.is_dir) || ::access(entry.native_path.constData(), R_OK) != 0 ||
              policy.excludes(entry.absolute_path, entry.is_dir, QFile::decodeName(cleaned))) {
            continue;
          }
          const auto relative = entry.native_path.mid(prefix.size());
          batch.append({.path = entry.absolute_path,
                        .relative_path = QFile::decodeName(relative),
                        .directory = entry.is_dir,
                        .native_path = entry.native_path,
                        .native_relative_path = relative});
          if (entry.is_dir) {
            pending.append(entry.native_path);
          }
          if (batch.size() == 256) {
            batchReady(std::move(batch));
            batch.clear();
          }
        }
      },
      folder);
}
}  // namespace
bool scanNativePaths(const QByteArray& root, bool includeHidden, const SearchExclusionPolicy& policy,
                     const std::atomic_bool& cancelled, const PathBatchReady& batchReady) {
  struct stat metadata{};
  if (!HolonightFileBrowser::absoluteNativePath(root) || ::stat(root.constData(), &metadata) != 0 ||
      !S_ISDIR(metadata.st_mode) || ::access(root.constData(), R_OK) != 0) {
    return false;
  }
  auto cleaned = root;
  while (cleaned.size() > 1 && cleaned.endsWith('/')) {
    cleaned.chop(1);
  }
  const auto prefix = cleaned.endsWith('/') ? cleaned : cleaned + '/';
  QList<QByteArray> pending{cleaned};
  QVector<PathCandidate> batch;
  while (!pending.isEmpty() && !cancelled.load()) {
    const auto folder = pending.takeLast();
    scanFolder(folder, cleaned, prefix, pending, batch, includeHidden, policy, cancelled, batchReady);
  }
  if (!cancelled.load() && !batch.isEmpty()) {
    batchReady(std::move(batch));
  }
  return true;
}
bool scanPaths(const QString& root, bool includeHidden, const SearchExclusionPolicy& policy,
               const std::atomic_bool& cancelled, const PathBatchReady& batchReady) {
  return scanNativePaths(QFile::encodeName(root), includeHidden, policy, cancelled, batchReady);
}
