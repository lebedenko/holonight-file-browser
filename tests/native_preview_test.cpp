#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

#include <HolonightFileBrowser/preview/preview_service.h>
#include <HolonightFileBrowser/preview/thumbnail_decoder.h>
#ifdef BROWSER_TEST_PREVIEW_QUICK
#include <QSignalSpy>

#include <HolonightFileBrowser/preview_quick/thumbnail_provider.h>
#endif
#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

TEST(NativePreview, IdenticalDisplayNamesOpenDistinctDescriptors) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  const auto root = QFile::encodeName(directory.path());
  const auto first = root + "/image" + static_cast<char>(0xfe) + ".png";
  const auto second = root + "/image" + static_cast<char>(0xff) + ".png";
  ASSERT_EQ(QFile::decodeName(first), QFile::decodeName(second));
  for (const auto& path : {first, second}) {
    // POSIX open requires a mode argument when creating a file.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
    const int descriptor = ::open(path.constData(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    ASSERT_GE(descriptor, 0);
    QFile file;
    ASSERT_TRUE(file.open(descriptor, QIODevice::WriteOnly, QFileDevice::AutoCloseHandle));
    QImage pixels(8, 8, QImage::Format_RGB32);
    pixels.fill(path == first ? Qt::red : Qt::blue);
    ASSERT_TRUE(pixels.save(&file, "PNG"));
  }
  PreviewService preview;
  for (const auto& path : {first, second, first}) {
    // Deliberately identical display name and caller metadata: native identity must
    // invalidate the current generation even when the presentation values match.
    preview.setTarget(QFile::decodeName(path), false, 0, {}, S_IFREG | 0600, false, {}, {}, 0, {}, path);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !preview.busy(); }));
    ASSERT_TRUE(preview.hasImage()) << preview.previewErrorMessage().toStdString();
    EXPECT_EQ(preview.image().pixelColor(0, 0), QColor(path == first ? Qt::red : Qt::blue));
    EXPECT_EQ(HolonightFileBrowser::nativePath(preview.markdownBaseUrl()), path);
    const std::atomic_bool cancelled{false};
    const auto thumbnail =
        decodeThumbnail({.path = QFile::decodeName(path), .box_pixels = 128, .native_path = path}, cancelled);
    ASSERT_FALSE(thumbnail.image.isNull()) << thumbnail.error.toStdString();
    EXPECT_EQ(thumbnail.image.pixelColor(0, 0), QColor(path == first ? Qt::red : Qt::blue));
  }
#ifdef BROWSER_TEST_PREVIEW_QUICK
  ThumbnailProvider provider;
  for (const auto& path : {first, second, first}) {
    std::unique_ptr<QQuickImageResponse> response(
        provider.requestImageResponse(ThumbnailProvider::idForNative(path, 128, 0), {}));
    QSignalSpy finished(response.get(), &QQuickImageResponse::finished);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !finished.isEmpty(); }));
    EXPECT_TRUE(response->errorString().isEmpty());
    std::unique_ptr<QQuickTextureFactory> texture(response->textureFactory());
    ASSERT_NE(texture, nullptr);
    EXPECT_EQ(texture->image().pixelColor(0, 0), QColor(path == first ? Qt::red : Qt::blue));
  }
  EXPECT_EQ(provider.cache().count(), 2);
#endif
}
