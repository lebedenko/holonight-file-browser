#include <QFile>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <HolonightFileBrowser/breadcrumb_adapter.h>
#include <HolonightFileBrowser/directory_reader.h>
#include <HolonightFileBrowser/directory_scan.h>
#include <HolonightFileBrowser/grid_navigation.h>
#include <HolonightFileBrowser/native_path.h>
#include <HolonightFileBrowser/selection.h>
#include <gtest/gtest.h>
using namespace HolonightFileBrowser;
TEST(NativePath, RoundTripsUndecodableNamesAndReservedUriBytes) {
  const QByteArray path = QByteArray("/tmp/") + static_cast<char>(0xff) + " #?%";
  EXPECT_EQ(nativePath(localUrl(path)), path);
  EXPECT_TRUE(localUrl("relative").isEmpty());
  EXPECT_TRUE(nativePath(QUrl("https://example.com/a")).isEmpty());
  EXPECT_TRUE(nativePath(QUrl("file:///tmp/%00a")).isEmpty());
  EXPECT_NE(entryIdentity(path), entryIdentity(path, true));
}
TEST(Selection, ReconcilesIdentityAcrossReorderingFilteringAndRemoval) {
  Selection state;
  state.single("a");
  state.range({"a", "b", "c"}, "c");
  EXPECT_EQ(state.selected, (QList<QByteArray>{"a", "b", "c"}));
  state.reconcile({"c", "a"});
  EXPECT_EQ(state.current, "c");
  EXPECT_EQ(state.selected, (QList<QByteArray>{"a", "c"}));
  state.toggle("a");
  EXPECT_EQ(state.selected, (QList<QByteArray>{"c"}));
  state.reconcile({});
  EXPECT_TRUE(state.current.isEmpty());
  EXPECT_TRUE(state.selected.isEmpty());
}
TEST(DirectoryRefresh, RetainsPersistentIndexesWithoutReset) {
  QTemporaryDir dir;
  QFile file(dir.filePath("keep"));
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write("one");
  file.close();
  DirectoryReader reader;
  reader.load(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !reader.scanning(); }));
  QPersistentModelIndex kept;
  for (int row = 0; row < reader.rowCount(); ++row) {
    if (reader.index(row).data(DirectoryRoles::NameRole) == "keep") {
      kept = reader.index(row);
    }
  }
  ASSERT_TRUE(kept.isValid());
  QSignalSpy resets(&reader, &QAbstractItemModel::modelReset);
  ASSERT_TRUE(file.open(QIODevice::Append));
  file.write("more");
  file.close();
  reader.refresh();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !reader.scanning(); }));
  EXPECT_EQ(resets.count(), 0);
  ASSERT_TRUE(kept.isValid());
  EXPECT_EQ(kept.data(DirectoryRoles::SizeRole).toLongLong(), 7);
  ASSERT_TRUE(file.remove());
  reader.refresh();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !reader.scanning(); }));
  EXPECT_FALSE(kept.isValid());
}
TEST(DirectoryScan, CancellationIsSilent) {
  auto cancel = std::make_shared<std::atomic_bool>(true);
  bool published = false;
  walkDirectory("/", cancel, {}, -1, {}, [&](const auto&, auto, const auto&) { published = true; });
  EXPECT_FALSE(published);
}

TEST(DirectoryRefresh, FailedRefreshPreservesTheLastKnownRows) {
  QTemporaryDir dir;
  ASSERT_TRUE(QDir(dir.path()).mkdir("folder"));
  QFile file(dir.filePath("folder/keep"));
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.close();
  DirectoryReader reader;
  reader.load(dir.filePath("folder"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !reader.scanning(); }));
  const int count = reader.rowCount();
  ASSERT_TRUE(QDir(dir.path()).rename("folder", "moved"));
  reader.refresh();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !reader.scanning(); }));
  EXPECT_FALSE(reader.directoryError().isEmpty());
  EXPECT_EQ(reader.rowCount(), count);
}

TEST(BreadcrumbAdapter, KeepsUndecodableNativeAncestorsIndependentOfDisplayLabels) {
  HolonightFileBrowser::BreadcrumbAdapter adapter;
  const QByteArray first = QByteArray("/same/") + static_cast<char>(0xff);
  const QByteArray second = QByteArray("/same/") + static_cast<char>(0xfe);
  adapter.setPath(first);
  const auto labels = adapter.labels();
  EXPECT_EQ(adapter.nativePathAt(0), QByteArray("/"));
  EXPECT_EQ(adapter.nativePathAt(1), QByteArray("/same"));
  EXPECT_EQ(adapter.nativePathAt(2), first);
  adapter.setPath(second);
  EXPECT_EQ(adapter.labels(), labels);
  EXPECT_EQ(adapter.nativePathAt(2), second);
  EXPECT_TRUE(adapter.nativePathAt(-1).isEmpty());
  EXPECT_TRUE(adapter.nativePathAt(3).isEmpty());
  adapter.setPath("relative");
  EXPECT_TRUE(adapter.labels().isEmpty());
}
