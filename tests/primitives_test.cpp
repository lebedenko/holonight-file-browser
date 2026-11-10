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
#include <HolonightFileBrowser/selection_session.h>
#include <gtest/gtest.h>
using namespace HolonightFileBrowser;
TEST(NativePath, RoundTripsUndecodableNamesAndReservedUriBytes) {
  const QByteArray path = QByteArray("/tmp/") + static_cast<char>(0xff) + " #?%";
  EXPECT_EQ(nativePath(localUrl(path)), path);
  EXPECT_TRUE(localUrl("relative").isEmpty());
  EXPECT_TRUE(nativePath(QUrl("https://example.com/a")).isEmpty());
  EXPECT_TRUE(nativePath(QUrl("file:///tmp/%00a")).isEmpty());
  EXPECT_EQ(entryIdentity(path), QByteArray("entry:") + path);
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

TEST(SelectionSession, IndependentMarksAndModifierRanges) {
  SelectionSession session;
  session.setVisibleEntries({"1", "2", "3", "4", "5", "6", "7", "8"});
  session.pointer(2);
  session.pointer(5, true);
  session.pointer(7, true);
  EXPECT_EQ(session.selected(), (QList<QByteArray>{"3", "6", "8"}));
  session.pointer(5, true);
  EXPECT_EQ(session.selected(), (QList<QByteArray>{"3", "8"}));
  session.pointer(7, true, true);
  EXPECT_EQ(session.selected(), (QList<QByteArray>{"3", "8", "6", "7"}));
  session.pointer(6, false, true);
  EXPECT_EQ(session.selected(), (QList<QByteArray>{"6", "7"}));
  EXPECT_EQ(session.mode(), SelectionSession::Mode::Mark);
  session.clear();
  EXPECT_EQ(session.mode(), SelectionSession::Mode::Normal);
  EXPECT_EQ(session.selectedCount(), 0);
}
TEST(SelectionSession, RangeMotionAndMarkMotionUseTheSameIdentities) {
  SelectionSession session;
  session.setVisibleEntries({"a", {}, "c", "d", "e"});
  session.beginRange(0);
  session.move(4);
  EXPECT_EQ(session.selectedCount(), 4);
  session.move(2);
  EXPECT_EQ(session.selected(), (QList<QByteArray>{"a", "c"}));
  session.swapEnds();
  EXPECT_EQ(session.cursorRow(), 0);
  EXPECT_EQ(session.anchorRow(), 2);
  session.beginMark(0);
  session.move(4);
  EXPECT_EQ(session.selectedCount(), 2);
  session.toggle(4);
  EXPECT_EQ(session.selectedCount(), 3);
  session.pointer(1);
  EXPECT_EQ(session.selectedCount(), 3);
  session.beginRange(4);
  EXPECT_EQ(session.selected(), (QList<QByteArray>{"e"}));
}
TEST(SelectionSession, FilteringTransientResetsAndRemovalHaveDistinctSemantics) {
  SelectionSession session;
  const QByteArray first = QByteArray("/same/") + static_cast<char>(0xff);
  const QByteArray second = QByteArray("/same/") + static_cast<char>(0xfe);
  session.setVisibleEntries({first, second, "third"});
  session.beginRange(0);
  session.move(1);
  session.setVisibleEntries({second});
  EXPECT_EQ(session.selectedCount(), 2);
  EXPECT_EQ(session.hiddenSelectedCount(), 1);
  session.reconcile({}, false);
  EXPECT_EQ(session.selectedCount(), 2);
  session.setVisibleEntries({"third", second, first});
  EXPECT_TRUE(session.isRowSelected(2));
  session.reconcile({second, "third"});
  EXPECT_EQ(session.selected(), (QList<QByteArray>{second}));
  EXPECT_EQ(session.mode(), SelectionSession::Mode::Mark);
  session.move(0);
  EXPECT_EQ(session.selected(), (QList<QByteArray>{second}));
}
TEST(SelectionSession, RectangleRecomputesAgainstInitialSetAndCanCancel) {
  SelectionSession session;
  session.setVisibleEntries({"a", {}, "c", "d"});
  session.pointer(0);
  session.beginRectangle(true);
  session.updateRectangle({1, 2, 3});
  EXPECT_EQ(session.selected(), (QList<QByteArray>{"a", "c", "d"}));
  session.updateRectangle({2});
  EXPECT_EQ(session.selected(), (QList<QByteArray>{"a", "c"}));
  session.cancelRectangle();
  EXPECT_EQ(session.selected(), (QList<QByteArray>{"a"}));
  EXPECT_EQ(session.mode(), SelectionSession::Mode::Normal);
  session.beginRectangle(false);
  session.updateRectangle({3});
  EXPECT_EQ(session.selected(), (QList<QByteArray>{"d"}));
}

TEST(SelectionSession, GridRectangleUsesContentCoordinatesAndFractionalCellPitch) {
  SelectionSession session;
  session.setVisibleEntries({"a", "b", "c", "d", "e", "f", "g"});
  session.beginRectangle(false);
  session.selectGridRectangle({130, 160, 155, 180}, {136.5, 160.5}, 3);
  EXPECT_EQ(session.selected(), (QList<QByteArray>{"d", "e", "f", "g"}));
  // Scrolling changes the content rectangle, not the model or instantiated rows.
  session.selectGridRectangle({0, 321, 137, 160}, {136.5, 160.5}, 3);
  EXPECT_EQ(session.selected(), (QList<QByteArray>{"g"}));
  session.selectGridRectangle({0, 0, 3, 3}, {136.5, 160.5}, 3);
  EXPECT_TRUE(session.selected().isEmpty());
  session.selectGridRectangle({0, 0, 0, 0}, {136.5, 160.5}, 3);
  EXPECT_TRUE(session.selected().isEmpty());
  session.selectGridRectangle({0, 0, 100, 100}, {0, 0}, 0);
  EXPECT_TRUE(session.selected().isEmpty());
}

TEST(SelectionSession, RestoringCursorAfterFilteringDoesNotExtendTheRange) {
  SelectionSession session;
  session.setVisibleEntries({"a", "b", "c"});
  session.beginRange(0);
  session.move(1);
  session.setVisibleEntries({"c", "a"});
  session.restoreCursor("c");
  EXPECT_EQ(session.cursorRow(), 0);
  EXPECT_EQ(session.selected(), (QList<QByteArray>{"a", "b"}));
  EXPECT_EQ(session.hiddenSelectedCount(), 1);
}

TEST(SelectionSession, MissingAnchorDoesNotDiscardMarksOnAdditiveRangeAndEscapeKeepsCursor) {
  SelectionSession session;
  session.setVisibleEntries({"a", "b", "c"});
  session.pointer(0);
  session.pointer(1, true);
  session.reconcile({"a", "c"});
  session.setVisibleEntries({"a", "c"});
  session.pointer(1, true, true);
  EXPECT_EQ(session.selected(), (QList<QByteArray>{"a", "c"}));
  session.clear();
  EXPECT_EQ(session.cursorRow(), 1);
  EXPECT_EQ(session.selectedCount(), 0);
  EXPECT_EQ(session.mode(), SelectionSession::Mode::Normal);
}
