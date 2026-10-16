#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSet>
#include <QTemporaryDir>

#include <HolonightFileBrowser/search/folder_search.h>
#include <HolonightFileBrowser/search/path_index_store.h>
#include <HolonightFileBrowser/search/path_scanner.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
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

TEST(BrowserSearch, NestedNativePathsPreserveAllCandidateFields) {
  QTemporaryDir directory;
  const auto root = QFile::encodeName(directory.path());
  QSet<QByteArray> expected;
  for (char byte : {static_cast<char>(0xfe), static_cast<char>(0xff)}) {
    const auto folder = QByteArray("folder") + byte;
    ASSERT_EQ(::mkdir((root + '/' + folder).constData(), 0700), 0);
    const auto relative = folder + "/file" + byte;
    // Use POSIX creation to preserve undecodable bytes.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const int descriptor = ::open((root + '/' + relative).constData(), O_CREAT | O_WRONLY, 0600);
    ASSERT_GE(descriptor, 0);
    ::close(descriptor);
    expected.insert(folder);
    expected.insert(relative);
  }
  QStringList diagnostics;
  const auto policy = SearchExclusionPolicy::compile({}, diagnostics);
  const std::atomic_bool cancelled{false};
  QSet<QByteArray> actual;
  ASSERT_TRUE(scanNativePaths(root + "///", false, policy, cancelled, [&](const auto& batch) {
    for (const auto& entry : batch) {
      actual.insert(entry.native_relative_path);
      EXPECT_EQ(entry.native_path, root + '/' + entry.native_relative_path);
      EXPECT_EQ(entry.path, QFile::decodeName(entry.native_path));
      EXPECT_EQ(entry.relative_path, QFile::decodeName(entry.native_relative_path));
      EXPECT_EQ(entry.directory, !entry.native_relative_path.contains('/'));
    }
  }));
  EXPECT_EQ(actual, expected);
}

TEST(BrowserSearch, HiddenExclusionsAndSelectedRootOverride) {
  QTemporaryDir directory;
  QDir root(directory.path());
  for (const auto& folder : {"visible", ".hidden", "blocked", "build-output"}) {
    ASSERT_TRUE(root.mkdir(folder));
    QFile file(root.filePath(QString::fromLatin1(folder) + "/child"));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  }
  QFile patternFile(root.filePath("build-file"));
  ASSERT_TRUE(patternFile.open(QIODevice::WriteOnly));
  QStringList diagnostics;
  const auto policy = SearchExclusionPolicy::compile({.paths = {root.filePath("blocked")}}, diagnostics);
  const std::atomic_bool cancelled{false};
  auto scan = [&](const QString& path, bool hidden) {
    QSet<QByteArray> result;
    EXPECT_TRUE(scanPaths(path, hidden, policy, cancelled, [&](const auto& batch) {
      for (const auto& entry : batch) {
        result.insert(entry.native_relative_path);
      }
    }));
    return result;
  };
  EXPECT_EQ(scan(directory.path(), false), (QSet<QByteArray>{"visible", "visible/child", "build-file"}));
  EXPECT_EQ(scan(directory.path(), true),
            (QSet<QByteArray>{"visible", "visible/child", "build-file", ".hidden", ".hidden/child"}));
  EXPECT_EQ(scan(root.filePath("blocked"), false), (QSet<QByteArray>{"child"}));
  EXPECT_EQ(scan(root.filePath("build-output"), false), (QSet<QByteArray>{"child"}));
}

TEST(BrowserSearch, SymlinksAndInvalidRoots) {
  QTemporaryDir directory;
  const auto root = QFile::encodeName(directory.path());
  QFile file(directory.filePath("file"));
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_TRUE(QDir(directory.path()).mkdir("folder"));
  ASSERT_EQ(::symlink("file", (root + "/file-link").constData()), 0);
  ASSERT_EQ(::symlink("folder", (root + "/folder-link").constData()), 0);
  ASSERT_EQ(::symlink("missing", (root + "/broken").constData()), 0);
  QStringList diagnostics;
  const auto policy = SearchExclusionPolicy::compile({}, diagnostics);
  const std::atomic_bool cancelled{false};
  QSet<QByteArray> actual;
  auto collect = [&](const auto& batch) {
    for (const auto& entry : batch) {
      actual.insert(entry.native_relative_path);
      EXPECT_EQ(entry.directory, entry.native_relative_path == "folder");
    }
  };
  ASSERT_TRUE(scanNativePaths(root, false, policy, cancelled, collect));
  EXPECT_EQ(actual, (QSet<QByteArray>{"file", "file-link", "folder"}));
  EXPECT_FALSE(scanNativePaths(root + "/missing", false, policy, cancelled, collect));
  EXPECT_FALSE(scanNativePaths(root + "/file", false, policy, cancelled, collect));
  EXPECT_FALSE(scanNativePaths("relative", false, policy, cancelled, collect));
  EXPECT_FALSE(scanNativePaths(root + QByteArray("\0suffix", 7), false, policy, cancelled, collect));
}

TEST(BrowserSearch, UnreadableEntriesAreOmitted) {
  QTemporaryDir directory;
  const auto root = QFile::encodeName(directory.path());
  QFile file(directory.filePath("unreadable"));
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.close();
  ASSERT_EQ(::chmod((root + "/unreadable").constData(), 0000), 0);
  if (::access((root + "/unreadable").constData(), R_OK) == 0) {
    GTEST_SKIP() << "Current credentials can read mode-000 entries";
  }
  ASSERT_EQ(::mkdir((root + "/closed").constData(), 0000), 0);
  QStringList diagnostics;
  const auto policy = SearchExclusionPolicy::compile({}, diagnostics);
  const std::atomic_bool cancelled{false};
  qsizetype count = 0;
  EXPECT_TRUE(scanNativePaths(root, false, policy, cancelled, [&](const auto& batch) { count += batch.size(); }));
  EXPECT_EQ(count, 0);
  EXPECT_FALSE(scanNativePaths(root + "/closed", false, policy, cancelled, [](const auto&) {}));
  EXPECT_EQ(::chmod((root + "/closed").constData(), 0700), 0);
}

TEST(BrowserSearch, BatchesAndCooperativeCancellation) {
  QTemporaryDir directory;
  for (int i = 0; i < 600; ++i) {
    QFile file(directory.filePath(QString::number(i)));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  }
  QStringList diagnostics;
  const auto policy = SearchExclusionPolicy::compile({}, diagnostics);
  std::atomic_bool cancelled{true};
  QList<qsizetype> sizes;
  auto collect = [&](const auto& batch) { sizes.append(batch.size()); };
  ASSERT_TRUE(scanPaths(directory.path(), false, policy, cancelled, collect));
  EXPECT_TRUE(sizes.isEmpty());
  cancelled.store(false);
  ASSERT_TRUE(scanPaths(directory.path(), false, policy, cancelled, collect));
  EXPECT_EQ(sizes, (QList<qsizetype>{256, 256, 88}));
  sizes.clear();
  ASSERT_TRUE(scanPaths(directory.path(), false, policy, cancelled, [&](const auto& batch) {
    collect(batch);
    cancelled.store(true);
  }));
  EXPECT_EQ(sizes, (QList<qsizetype>{256}));
}
