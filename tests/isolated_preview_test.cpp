#include <QColorSpace>
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>

#include <HolonightFileBrowser/preview/image_policy.h>
#include <HolonightFileBrowser/preview/preview_service.h>
#include <HolonightFileBrowser/preview/thumbnail_decoder.h>
#include <HolonightFileBrowser/preview/thumbnail_service.h>
#include <array>
#include <fcntl.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <holonight_thumbnails/cache.h>
#include <sys/stat.h>
#include <unistd.h>

TEST(IsolatedPreview, MissingRuntimeHasDistinctError) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const auto path = directory.filePath("image.png");
  QImage pixels(8, 8, QImage::Format_RGB32);
  pixels.fill(Qt::red);
  ASSERT_TRUE(pixels.save(path));
  const HolonightFileBrowser::ImageRuntimeOptions options{directory.filePath("absent")};
  PreviewService service(options);
  service.setTarget(path, false, 0, {}, S_IFREG | 0600, false, {}, {});
  ASSERT_TRUE(QTest::qWaitFor([&] { return !service.busy(); }));
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::RuntimeUnavailable);
  EXPECT_FALSE(service.hasImage());
  const std::atomic_bool cancelled{false};
  const auto thumbnail = decodeThumbnail({.path = path, .box_pixels = 128}, cancelled, options);
  EXPECT_EQ(thumbnail.runtime, HolonightImages::RuntimeStatus::Unavailable);
  EXPECT_TRUE(thumbnail.image.isNull());
  EXPECT_FALSE(thumbnail.error.contains("damaged"));
}

TEST(IsolatedPreview, CancellationDuringCacheReadPreventsCommitAndLateDisplay) {
  if (QString::fromUtf8(BROWSER_TEST_RUNTIME).isEmpty()) {
    GTEST_SKIP() << "Explicit native runtime required";
  }
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const HolonightFileBrowser::ImageRuntimeOptions options{QString::fromUtf8(BROWSER_TEST_RUNTIME)};
  const auto path = directory.filePath("image.png");
  QImage pixels(80, 40, QImage::Format_ARGB32);
  pixels.fill(QColor(220, 10, 30, 100));
  ASSERT_TRUE(pixels.save(path));
  std::atomic_bool cancelled{false};
  QFile source(path);
  ASSERT_TRUE(source.open(QIODevice::ReadOnly));
  const auto first = ThumbnailService::lookupOrDecode(source, path, {}, cancelled, options);
  ASSERT_EQ(first.runtime, HolonightImages::RuntimeStatus::Success) << first.diagnostic.toStdString();
  ASSERT_FALSE(first.image.isNull());
  const auto stopped = ThumbnailService::lookup(
      source, path, {}, ThumbnailService::Tier::Normal, {80, 40}, cancelled,
      [&](ThumbnailService::Stage stage) {
        if (stage == ThumbnailService::Stage::CacheDecode) {
          cancelled.store(true);
        }
      },
      ThumbnailService::ImageKind::Raster, {}, options);
  ASSERT_TRUE(stopped);
  EXPECT_EQ(stopped->runtime, HolonightImages::RuntimeStatus::Cancelled);
  EXPECT_TRUE(stopped->image.isNull());
  PreviewService service(options);
  service.setTarget(path, false, 0, {}, S_IFREG | 0600, false, {}, {});
  service.clear();
  service.shutdown();
  EXPECT_FALSE(service.hasImage());
}

TEST(IsolatedPreview, QualifiedFormatsAndOrientationsPreservePixelsAndBounds) {
  if (QString::fromUtf8(BROWSER_TEST_RUNTIME).isEmpty() || QString::fromUtf8(BROWSER_TEST_FIXTURES).isEmpty()) {
    GTEST_SKIP() << "Explicit runtime and fixtures required";
  }
  const HolonightFileBrowser::ImageRuntimeOptions options{QString::fromUtf8(BROWSER_TEST_RUNTIME)};
  const QDir fixtures(QString::fromUtf8(BROWSER_TEST_FIXTURES));
  const std::atomic_bool cancelled{false};
  for (const auto& name : {"sample.png", "sample.jpg", "sample.bmp", "sample.tif", "sample.webp", "sample.gif"}) {
    SCOPED_TRACE(name);
    QFile source(fixtures.filePath(QLatin1String(name)));
    ASSERT_TRUE(source.open(QIODevice::ReadOnly));
    const auto expected = HolonightImages::decode(source, {.limits = kPreviewImageLimits, .bound = {9, 7}}, cancelled);
    const auto actual = ThumbnailService::decodeScaled(source, {9, 7}, cancelled, {}, options);
    ASSERT_EQ(actual.runtime, HolonightImages::RuntimeStatus::Success) << actual.diagnostic.toStdString();
    EXPECT_EQ(actual.outcome, expected.outcome);
    EXPECT_EQ(actual.image, expected.image);
    EXPECT_LE(actual.image.width(), 9);
    EXPECT_LE(actual.image.height(), 7);
  }
  QFile jpeg(fixtures.filePath("sample.jpg"));
  ASSERT_TRUE(jpeg.open(QIODevice::ReadOnly));
  const auto original = jpeg.readAll();
  const auto field = original.indexOf(QByteArray::fromHex("120103000100000006000000"));
  ASSERT_GE(field, 0);
  QTemporaryDir directory;
  for (int orientation = 1; orientation <= 8; ++orientation) {
    auto bytes = original;
    bytes[field + 8] = static_cast<char>(orientation);
    QFile source(directory.filePath("oriented.jpg"));
    ASSERT_TRUE(source.open(QIODevice::ReadWrite | QIODevice::Truncate));
    ASSERT_EQ(source.write(bytes), bytes.size());
    source.flush();
    const auto expected =
        HolonightImages::decode(source, {.limits = kPreviewImageLimits, .bound = {100, 100}}, cancelled);
    const auto actual = ThumbnailService::decodeScaled(source, {100, 100}, cancelled, {}, options);
    EXPECT_EQ(actual.runtime, HolonightImages::RuntimeStatus::Success);
    EXPECT_EQ(actual.image, expected.image);
    EXPECT_EQ(actual.image.size(), orientation >= 5 ? QSize(20, 40) : QSize(40, 20));
    const auto metadata = ExifReader::read(source, "image/jpeg", {}, options);
    EXPECT_EQ(metadata, ExifReader::format(expected.inspection.metadata));
  }
}

TEST(IsolatedPreview, PrecisionAlphaUnsupportedProfileAndInputLimit) {
  if (QString::fromUtf8(BROWSER_TEST_RUNTIME).isEmpty()) {
    GTEST_SKIP() << "Explicit native runtime required";
  }
  const HolonightFileBrowser::ImageRuntimeOptions options{QString::fromUtf8(BROWSER_TEST_RUNTIME)};
  QTemporaryDir directory;
  const auto path = directory.filePath("precision.png");
  QImage pixels(3, 2, QImage::Format_RGBA64);
  pixels.fill(QColor::fromRgba64(12345, 23456, 34567, 45678));
  ASSERT_TRUE(pixels.save(path));
  auto result = ThumbnailService::decodeScaled(path, {100, 100}, options);
  ASSERT_EQ(result.runtime, HolonightImages::RuntimeStatus::Success);
  EXPECT_EQ(result.image.size(), pixels.size());
  EXPECT_EQ(result.image.pixelColor(0, 0).rgba64(), pixels.pixelColor(0, 0).rgba64());
  pixels.setColorSpace(QColorSpace(QColorSpace::Primaries::SRgb, QColorSpace::TransferFunction::Gamma, 1.7));
  ASSERT_TRUE(pixels.save(path));
  result = ThumbnailService::decodeScaled(path, {100, 100}, options);
  EXPECT_EQ(result.runtime, HolonightImages::RuntimeStatus::Success);
  EXPECT_EQ(result.outcome, HolonightImages::Outcome::Unsupported);
  EXPECT_TRUE(result.image.isNull());
  QFile oversized(directory.filePath("oversized.png"));
  ASSERT_TRUE(oversized.open(QIODevice::ReadWrite));
  ASSERT_TRUE(oversized.resize((256LL * 1024 * 1024) + 1));
  const std::atomic_bool cancelled{false};
  result = ThumbnailService::decodeScaled(oversized, {100, 100}, cancelled, {}, options);
  EXPECT_EQ(result.runtime, HolonightImages::RuntimeStatus::Success);
  EXPECT_EQ(result.outcome, HolonightImages::Outcome::ResourceLimit);
}

struct PreviewServiceTestAccess {
  static void memory(PreviewService& service, std::function<void()> callback) {
    service.memory_lookup_for_test_ = std::move(callback);
  }
  static bool active(const PreviewService& service) { return service.active_job_; }

  static void stage(PreviewService& service, ThumbnailService::StageCallback callback) {
    service.thumbnail_stage_for_test_ = std::move(callback);
  }
};

TEST(IsolatedPreview, TargetChangeWhileCommittingDropsOldPixelsAndCache) {
  if (QString::fromUtf8(BROWSER_TEST_RUNTIME).isEmpty()) {
    GTEST_SKIP() << "Explicit native runtime required";
  }
  QTemporaryDir directory;
  const HolonightFileBrowser::ImageRuntimeOptions options{QString::fromUtf8(BROWSER_TEST_RUNTIME)};
  const auto oldPath = directory.filePath("old.png");
  const auto newPath = directory.filePath("new.png");
  QImage pixels(40, 20, QImage::Format_RGB32);
  pixels.fill(Qt::red);
  ASSERT_TRUE(pixels.save(oldPath));
  pixels.fill(Qt::blue);
  ASSERT_TRUE(pixels.save(newPath));
  QSemaphore reached;
  QSemaphore resume;
  PreviewService service(options);
  PreviewServiceTestAccess::stage(service, [&](ThumbnailService::Stage stage) {
    if (stage == ThumbnailService::Stage::BeforeCommit) {
      reached.release();
      resume.acquire();
    }
  });
  service.setTarget(oldPath, false, 0, {}, S_IFREG | 0600, false, {}, {});
  const bool arrived = reached.tryAcquire(1, 5000);
  if (!arrived) {
    resume.release();
    FAIL() << "Cache commit checkpoint not reached";
  }
  PreviewServiceTestAccess::stage(service, {});
  service.setTarget(newPath, false, 0, {}, S_IFREG | 0600, false, {}, {});
  resume.release();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !service.busy(); }));
  ASSERT_TRUE(service.hasImage());
  EXPECT_EQ(service.image().pixelColor(0, 0), QColor(Qt::blue));
  EXPECT_FALSE(QFileInfo::exists(HolonightThumbnails::cachePath(QUrl::fromLocalFile(oldPath), 128)));
  service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {800, 800});
  service.shutdown();
}

namespace {
class WarmPreview : public testing::Test {
 protected:
  void SetUp() override {
    if (QString::fromUtf8(BROWSER_TEST_RUNTIME).isEmpty()) {
      GTEST_SKIP() << "Explicit native runtime required";
    }
    ASSERT_TRUE(directory.isValid());
    runtime = directory.filePath("runtime");
    std::filesystem::copy(BROWSER_TEST_RUNTIME, runtime.toStdString(), std::filesystem::copy_options::recursive);
  }
  QString makeImage(const QString& name, QSize size = {80, 40}, QColor color = Qt::red) {
    const auto path = directory.filePath(name);
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    EXPECT_TRUE(image.save(path));
    return path;
  }
  static void select(PreviewService& service, const QString& path, const QByteArray& native = {}) {
    service.clear();
    service.setTarget(path, false, 0, {}, S_IFREG | 0600, false, {}, {}, 0, {}, native);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !service.busy() && !PreviewServiceTestAccess::active(service); }));
  }
  void hideRuntime() { ASSERT_TRUE(QDir().rename(runtime, runtime + "-hidden")); }
  // GTest subclasses share fixture state.
  // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
  QTemporaryDir directory;
  QString runtime;
  // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)
};
}  // namespace

TEST_F(WarmPreview, RevisitPreservesOrientedDimensionsMetadataAndProgressivePixelsWithoutRuntime) {
  const auto path = directory.filePath("oriented.jpg");
  ASSERT_TRUE(QFile::copy(QDir(QString::fromUtf8(BROWSER_TEST_FIXTURES)).filePath("sample.jpg"), path));
  QFile fixture(path);
  ASSERT_TRUE(fixture.open(QIODevice::ReadWrite));
  auto bytes = fixture.readAll();
  // Append a second IFD0 field: inline ASCII Make = "Cam" beside orientation 6.
  const auto oldExif = QByteArray::fromHex("ffe1002245786966000049492a0008000000010012010300010000000600000000000000");
  const auto newExif = QByteArray::fromHex(
      "ffe1002e45786966000049492a000800000002001201030001000000060000000f0102000400000043616d0000000000");
  ASSERT_TRUE(bytes.contains(oldExif));
  bytes.replace(oldExif, newExif);
  ASSERT_TRUE(fixture.resize(0));
  ASSERT_EQ(fixture.write(bytes), bytes.size());
  fixture.close();
  PreviewService service({runtime});
  select(service, path);
  ASSERT_TRUE(service.hasImage());
  const auto image = service.image();
  const auto dimensions = service.sourcePixelSize();
  const auto camera = service.exifMake();
  EXPECT_EQ(camera, "Cam");
  EXPECT_EQ(dimensions, QSize(20, 40));
  hideRuntime();
  bool progressive = false;
  QObject::connect(&service, &PreviewService::changed, &service,
                   [&] { progressive |= service.busy() && service.hasImage(); });
  select(service, path);
  EXPECT_TRUE(progressive);
  EXPECT_EQ(service.image(), image);
  EXPECT_EQ(service.sourcePixelSize(), dimensions);
  EXPECT_EQ(service.exifMake(), camera);
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::None);
  service.setRequestedSize(PreviewService::PreviewConsumer::QuickLook, {1000, 1000});
  service.setQuickLookActive(true);
  select(service, path);
  EXPECT_EQ(service.image(), image);
  PreviewService fresh({runtime});
  select(fresh, path);
  EXPECT_EQ(fresh.previewErrorKind(), PreviewService::PreviewErrorKind::RuntimeUnavailable);
}

TEST_F(WarmPreview, ResolutionUpgradeAndEvictionStillRequireRuntime) {
  const auto first = makeImage("first.png", {1600, 1200});
  const auto second = makeImage("second.png");
  const auto third = makeImage("third.png");
  PreviewService service({runtime});
  service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {128, 128});
  select(service, first);
  select(service, second);
  select(service, first);  // Refresh A's LRU position.
  select(service, third);  // B is evicted.
  hideRuntime();
  select(service, first);
  EXPECT_TRUE(service.hasImage());
  select(service, third);
  EXPECT_TRUE(service.hasImage());
  select(service, second);
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::RuntimeUnavailable);
  service.setRequestedSize(PreviewService::PreviewConsumer::QuickLook, {1200, 1200});
  service.setQuickLookActive(true);
  select(service, first);
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::RuntimeUnavailable);
}

TEST_F(WarmPreview, ImageBudgetEvictsBeforeTwoEntries) {
  const auto first = makeImage("first.png", {3000, 3000});
  const auto second = makeImage("second.png", {3000, 3000});
  PreviewService service({runtime});
  service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {3000, 3000});
  select(service, first);
  ASSERT_TRUE(service.hasImage());
  select(service, second);
  ASSERT_TRUE(service.hasImage());
  hideRuntime();
  select(service, second);
  EXPECT_TRUE(service.hasImage());
  select(service, first);
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::RuntimeUnavailable);
}

TEST_F(WarmPreview, ChangedRemovedAndUnreadableSourcesCannotReusePixels) {
  const auto path = makeImage("source.bmp");
  PreviewService service({runtime});
  for (const auto action : {0, 1, 2, 3}) {
    SCOPED_TRACE(action);
    if (action != 0) {
      ASSERT_TRUE(QDir().rename(runtime + "-hidden", runtime));
      makeImage("source.bmp");
    }
    select(service, path);
    ASSERT_TRUE(service.hasImage());
    hideRuntime();
    struct stat original{};
    ASSERT_EQ(::stat(QFile::encodeName(path).constData(), &original), 0);
    if (action == 0) {
      makeImage("source.bmp", {80, 40}, Qt::blue);
      const std::array times{original.st_atim, original.st_mtim};
      ASSERT_EQ(::utimensat(AT_FDCWD, QFile::encodeName(path).constData(), times.data(), 0), 0);
      struct stat changed{};
      ASSERT_EQ(::stat(QFile::encodeName(path).constData(), &changed), 0);
      ASSERT_EQ(changed.st_size, original.st_size);
    } else if (action == 1) {
      const auto replacement = makeImage("replacement.bmp", {80, 40}, Qt::blue);
      ASSERT_EQ(::rename(QFile::encodeName(replacement).constData(), QFile::encodeName(path).constData()), 0);
    } else if (action == 2) {
      ASSERT_EQ(::chmod(QFile::encodeName(path).constData(), 0), 0);
    } else {
      ASSERT_TRUE(QFile::remove(path));
    }
    select(service, path);
    EXPECT_FALSE(service.hasImage());
    EXPECT_NE(service.previewErrorKind(), PreviewService::PreviewErrorKind::None);
    if (action == 2) {
      ASSERT_EQ(::chmod(QFile::encodeName(path).constData(), 0600), 0);
    }
  }
}

TEST_F(WarmPreview, MutationAtMemoryCheckpointInvalidatesMemoryAndDiskCandidate) {
  const auto path = makeImage("source.bmp");
  PreviewService service({runtime});
  select(service, path);
  ASSERT_TRUE(service.hasImage());
  struct stat original{};
  ASSERT_EQ(::stat(QFile::encodeName(path).constData(), &original), 0);
  std::atomic_bool reached{false};
  PreviewServiceTestAccess::memory(service, [&] {
    reached.store(true);
    makeImage("source.bmp", {80, 40}, Qt::blue);
    const std::array times{original.st_atim, original.st_mtim};
    EXPECT_EQ(::utimensat(AT_FDCWD, QFile::encodeName(path).constData(), times.data(), 0), 0);
  });
  select(service, path);
  EXPECT_TRUE(reached.load());
  ASSERT_TRUE(service.hasImage());
  EXPECT_EQ(service.image().pixelColor(0, 0), QColor(Qt::blue));
  PreviewServiceTestAccess::memory(service, {});
  hideRuntime();
  select(service, path);
  EXPECT_EQ(service.image().pixelColor(0, 0), QColor(Qt::blue));
}

TEST_F(WarmPreview, MemoryCheckpointSuppressesCancelledSupersededTimedOutAndShutdownReplies) {
  const auto first = makeImage("first.png");
  const auto second = makeImage("second.png", {80, 40}, Qt::blue);
  for (int action : {0, 1, 2, 3}) {
    SCOPED_TRACE(action);
    QSemaphore reached;
    QSemaphore resume;
    PreviewService service({runtime});
    select(service, first);
    select(service, second);
    PreviewServiceTestAccess::memory(service, [&] {
      reached.release();
      resume.acquire();
    });
    const auto release = qScopeGuard([&] { resume.release(); });
    service.setTarget(first, false, 0, {}, S_IFREG | 0600, false, {}, {});
    ASSERT_TRUE(reached.tryAcquire(1, 5000));
    PreviewServiceTestAccess::memory(service, {});
    if (action == 0) {
      service.clear();
    } else if (action == 1) {
      service.setTarget(second, false, 0, {}, S_IFREG | 0600, false, {}, {});
    } else if (action == 2) {
      ASSERT_TRUE(QTest::qWaitFor(
          [&] { return service.previewErrorKind() == PreviewService::PreviewErrorKind::DecodeTimeout; }, 5000));
    } else {
      service.shutdown();
    }
    resume.release();
    ASSERT_TRUE(QTest::qWaitFor([&] { return !PreviewServiceTestAccess::active(service); }));
    if (action == 1) {
      EXPECT_EQ(service.image().pixelColor(0, 0), QColor(Qt::blue));
    } else {
      EXPECT_FALSE(service.hasImage());
    }
  }
}

TEST_F(WarmPreview, NativeFilenameRevisitUsesVerifiedDescriptor) {
  const auto original = makeImage("source.png");
  const auto native =
      QFile::encodeName(directory.path()) + "/native-" + QByteArray(1, static_cast<char>(0xff)) + ".png";
  ASSERT_EQ(::rename(QFile::encodeName(original).constData(), native.constData()), 0);
  PreviewService service({runtime});
  select(service, QFile::decodeName(native), native);
  ASSERT_TRUE(service.hasImage());
  hideRuntime();
  select(service, QFile::decodeName(native), native);
  EXPECT_TRUE(service.hasImage());
  EXPECT_EQ(service.image().pixelColor(0, 0), QColor(Qt::red));
}

TEST_F(WarmPreview, OversizedImageIsDisplayedButNotRetained) {
  const auto path = makeImage("oversized.png", {4100, 4100});
  PreviewService service({runtime});
  service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {4100, 4100});
  select(service, path);
  ASSERT_TRUE(service.hasImage());
  ASSERT_GT(service.image().sizeInBytes(), 64LL * 1024 * 1024);
  hideRuntime();
  select(service, path);
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::RuntimeUnavailable);
}

TEST_F(WarmPreview, CancelledAcquisitionDoesNotPopulateMemory) {
  const auto path = makeImage("cancelled.png");
  QSemaphore reached;
  QSemaphore resume;
  PreviewService service({runtime});
  PreviewServiceTestAccess::stage(service, [&](ThumbnailService::Stage stage) {
    if (stage == ThumbnailService::Stage::BeforeCommit) {
      reached.release();
      resume.acquire();
    }
  });
  const auto release = qScopeGuard([&] { resume.release(); });
  service.setTarget(path, false, 0, {}, S_IFREG | 0600, false, {}, {});
  ASSERT_TRUE(reached.tryAcquire(1, 5000));
  service.clear();
  resume.release();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !PreviewServiceTestAccess::active(service); }));
  EXPECT_FALSE(service.hasImage());
  PreviewServiceTestAccess::stage(service, {});
  hideRuntime();
  select(service, path);
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::RuntimeUnavailable);
}
