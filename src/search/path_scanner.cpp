#include <QFile>

#include <HolonightFileBrowser/native_path.h>
#include <HolonightFileBrowser/search/path_scanner.h>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

namespace {
struct DirectoryCloser {
  void operator()(DIR* directory) const { ::closedir(directory); }
};
bool classifyEntry(int descriptor, const dirent& entry, bool& isDirectory) {
  isDirectory = entry.d_type == DT_DIR;
  if (entry.d_type != DT_UNKNOWN && entry.d_type != DT_LNK) {
    return true;
  }
  const auto* name = static_cast<const char*>(entry.d_name);
  struct stat metadata{};
  if (::fstatat(descriptor, name, &metadata, AT_SYMLINK_NOFOLLOW) != 0) {
    return false;
  }
  if (S_ISLNK(metadata.st_mode)) {
    if (::fstatat(descriptor, name, &metadata, 0) != 0 || S_ISDIR(metadata.st_mode)) {
      return false;
    }
  }
  isDirectory = S_ISDIR(metadata.st_mode);
  return true;
}
void scanFolder(const QByteArray& folder, const QString& root, const QByteArray& prefix, QList<QByteArray>& pending,
                QVector<PathCandidate>& batch, bool includeHidden, const SearchExclusionPolicy& policy,
                const std::atomic_bool& cancelled, const PathBatchReady& batchReady) {
  const std::unique_ptr<DIR, DirectoryCloser> directory(::opendir(folder.constData()));
  if (!directory) {
    return;
  }
  const int descriptor = ::dirfd(directory.get());
  if (descriptor < 0) {
    return;
  }
  const auto folderPrefix = folder.endsWith('/') ? folder : folder + '/';
  while (!cancelled.load()) {
    const auto* entry = ::readdir(directory.get());
    if (entry == nullptr) {
      return;
    }
    const QByteArray name(static_cast<const char*>(entry->d_name));
    if (name == "." || name == ".." || (!includeHidden && name.startsWith('.'))) {
      continue;
    }
    bool isDirectory = false;
    if (!classifyEntry(descriptor, *entry, isDirectory)) {
      continue;
    }
    const auto nativePath = folderPrefix + name;
    if (::access(nativePath.constData(), R_OK) != 0) {
      continue;
    }
    const auto path = QFile::decodeName(nativePath);
    if (policy.excludes(path, isDirectory, root)) {
      continue;
    }
    const auto relative = nativePath.mid(prefix.size());
    batch.append({.path = path,
                  .relative_path = QFile::decodeName(relative),
                  .directory = isDirectory,
                  .native_path = nativePath,
                  .native_relative_path = relative});
    if (isDirectory) {
      pending.append(nativePath);
    }
    if (batch.size() == 256) {
      batchReady(std::move(batch));
      batch.clear();
      if (cancelled.load()) {
        return;
      }
    }
  }
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
  const auto displayRoot = QFile::decodeName(cleaned);
  QList<QByteArray> pending{cleaned};
  QVector<PathCandidate> batch;
  while (!pending.isEmpty() && !cancelled.load()) {
    const auto folder = pending.takeLast();
    scanFolder(folder, displayRoot, prefix, pending, batch, includeHidden, policy, cancelled, batchReady);
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
