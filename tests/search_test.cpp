#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>

#include <HolonightFileBrowser/search/folder_search.h>
#include <HolonightFileBrowser/search/path_index_store.h>
#include <HolonightFileBrowser/search/path_scanner.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <unistd.h>
TEST(BrowserSearch, NativeNamesRemainDistinctThroughTraversalAndCache) {
  QTemporaryDir dir;
  const auto root = QFile::encodeName(dir.path());
  for (char byte : {static_cast<char>(0xfe), static_cast<char>(0xff)}) {
    const auto path = root + "/file" + byte;
    // POSIX open is variadic, including when a creation mode is required.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const int descriptor = ::open(path.constData(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    ASSERT_GE(descriptor, 0);
    ::close(descriptor);
  }
  std::atomic_bool cancelled{false};
  QStringList diagnostics;
  const auto policy = SearchExclusionPolicy::compile({}, diagnostics);
  QVector<PathCandidate> entries;
  ASSERT_TRUE(scanNativePaths(root, false, policy, cancelled, [&](const auto& batch) { entries += batch; }));
  ASSERT_EQ(entries.size(), 2);
  EXPECT_EQ(entries[0].path, entries[1].path);
  EXPECT_NE(entries[0].native_path, entries[1].native_path);
  const auto cache = dir.filePath("index");
  BrowserPathIndexStore store(cache, dir.path(), policy.fingerprint());
  store.append(entries);
  ASSERT_TRUE(store.commit(QDateTime::currentMSecsSinceEpoch(), cancelled));
  QVector<PathCandidate> restored;
  qint64 completed = 0;
  ASSERT_TRUE(BrowserPathIndexStore::load(
      cache, dir.path(), policy.fingerprint(), cancelled, [&](const auto& batch) { restored += batch; }, completed));
  ASSERT_EQ(restored.size(), 2);
  EXPECT_EQ(restored[0].native_path, entries[0].native_path);
  QList<HolonightFileBrowser::FolderSearchEntry> folder;
  for (const auto& entry : restored) {
    folder.append({.path = entry.native_path, .name = entry.relative_path, .directory = false});
  }
  EXPECT_EQ(HolonightFileBrowser::matchFolder(folder, "file", cancelled).size(), 2);
  cancelled.store(true);
  EXPECT_TRUE(HolonightFileBrowser::matchFolder(folder, "file", cancelled).isEmpty());
}

TEST(BrowserSearch, RootDirectoryCachePreservesNativeIdentity) {
  QTemporaryDir directory;
  const auto cache = directory.filePath("index");
  const QByteArray relative = QByteArray("file") + static_cast<char>(0xff);
  const QByteArray path = "/" + relative;
  const std::atomic_bool cancelled{false};
  BrowserPathIndexStore store(cache, "/", {});
  store.append({{.path = QFile::decodeName(path),
                 .relative_path = QFile::decodeName(relative),
                 .directory = false,
                 .native_path = path,
                 .native_relative_path = relative}});
  ASSERT_TRUE(store.commit(QDateTime::currentMSecsSinceEpoch(), cancelled));
  QVector<PathCandidate> restored;
  qint64 completed = 0;
  ASSERT_TRUE(
      BrowserPathIndexStore::load(cache, "/", {}, cancelled, [&](const auto& batch) { restored += batch; }, completed));
  ASSERT_EQ(restored.size(), 1);
  EXPECT_EQ(restored[0].native_path, path);
}
