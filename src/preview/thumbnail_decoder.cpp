#include <QFile>
#include <QFileInfo>
#include <QSvgRenderer>
#include <QUrl>

#include <HolonightFileBrowser/native_path.h>
#include <HolonightFileBrowser/preview/image_limits.h>
#include <HolonightFileBrowser/preview/svg_helper.h>
#include <HolonightFileBrowser/preview/thumbnail_decoder.h>
#include <HolonightFileBrowser/preview/thumbnail_size.h>
#include <fcntl.h>
#include <holonight_images/image.h>
#include <holonight_images/svg.h>
#include <holonight_thumbnails/cache.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace {
ThumbnailResult failure(QString error) { return {.image = {}, .source_size = {}, .error = std::move(error)}; }

QString outcomeError(HolonightImages::Outcome outcome) {
  using HolonightImages::Outcome;
  switch (outcome) {
    case Outcome::Success:
      return {};
    case Outcome::Cancelled:
      return QStringLiteral("Cancelled");
    case Outcome::Unsupported:
      return QStringLiteral("The image format is not recognized.");
    case Outcome::Damaged:
      return QStringLiteral("The image is damaged or could not be decoded.");
    case Outcome::ResourceLimit:
      return QStringLiteral("The image exceeds the viewing limit.");
    case Outcome::IoFailure:
      return QStringLiteral("The image could not be read.");
  }
  Q_UNREACHABLE();
}

ThumbnailResult finish(QImage image, QSize sourceSize) {
  image.convertTo(QImage::Format_ARGB32_Premultiplied);
  if (image.isNull()) {
    return failure(QStringLiteral("There is not enough memory to display this image."));
  }
  image.setDevicePixelRatio(1);
  return {.image = std::move(image), .source_size = sourceSize, .error = {}};
}

HolonightThumbnails::Request cacheRequest(const QByteArray& path, const struct stat& metadata, QSize required,
                                          HolonightThumbnails::Kind kind) {
  const qint64 milliseconds =
      (static_cast<qint64>(metadata.st_mtim.tv_sec) * 1000) + (metadata.st_mtim.tv_nsec / 1000000);
  return {.uri = HolonightFileBrowser::localUrl(path),
          .modified = QDateTime::fromMSecsSinceEpoch(milliseconds),
          .size = metadata.st_size,
          .required = required,
          .kind = kind,
          .revision = {}};
}

bool sourceMatches(const QByteArray& path, const struct stat& opened) {
  struct stat current{};
  return ::stat(path.constData(), &current) == 0 && current.st_dev == opened.st_dev &&
         current.st_ino == opened.st_ino && current.st_size == opened.st_size &&
         current.st_mtim.tv_sec == opened.st_mtim.tv_sec && current.st_mtim.tv_nsec == opened.st_mtim.tv_nsec;
}

bool useDiskCache(int boxPixels) { return boxPixels > 0 && boxPixels <= 1024; }

ThumbnailResult decodeSvg(QFile& file, const QByteArray& path, const struct stat& metadata, int boxPixels,
                          const std::atomic_bool& cancelled) {
  const auto source = HolonightImages::loadSvg(file, kSvgFileLimitBytes, cancelled);
  if (source.outcome != HolonightImages::Outcome::Success) {
    return failure(outcomeError(source.outcome));
  }
  QSvgRenderer renderer;
  // Qt's filename-based local-resource fallback cannot represent undecodable paths.
  const auto displayPath = QFile::decodeName(path);
  const auto inspection = QFile::encodeName(displayPath) == path
                              ? inspectViewerSvg(source.bytes, displayPath, renderer, cancelled)
                              : HolonightImages::inspectSvg(source.bytes, cancelled);
  if (renderer.isValid() && !cancelled.load()) {
    const auto size = inspection.facts.documentSize;
    auto result = finish(renderLocalSvg(renderer, size, {boxPixels, boxPixels}, cancelled),
                         HolonightImages::svgPixelSize(size, {kThumbnailBoxLogical, kThumbnailBoxLogical}));
    result.cache_eligible = false;
    return result;
  }
  if (inspection.outcome != HolonightImages::Outcome::Success) {
    return failure(outcomeError(inspection.outcome));
  }
  const QSize box(kThumbnailBoxLogical, kThumbnailBoxLogical);
  const auto sourceSize = HolonightImages::svgPixelSize(inspection.facts.documentSize, box);
  const auto cache =
      cacheRequest(path, metadata, HolonightImages::svgPixelSize(inspection.facts.documentSize, {boxPixels, boxPixels}),
                   HolonightThumbnails::Kind::Svg);
  if (useDiskCache(boxPixels)) {
    if (auto hit = HolonightThumbnails::lookup(cache, cancelled)) {
      return finish(std::move(*hit), sourceSize);
    }
  }
  auto rendered = HolonightImages::rasterizeSvg(
      source.bytes, {.bound = {boxPixels, boxPixels}, .outputBytes = kImageLimitBytes}, cancelled);
  if (rendered.inspection.outcome != HolonightImages::Outcome::Success || rendered.image.isNull()) {
    return failure(outcomeError(rendered.inspection.outcome));
  }
  auto result = finish(std::move(rendered.image), sourceSize);
  if (!result.image.isNull() && useDiskCache(boxPixels) && !cancelled.load() && sourceMatches(path, metadata)) {
    HolonightThumbnails::store(cache, result.image, cancelled);
  }
  return result;
}
}  // namespace

ThumbnailResult decodeThumbnail(const ThumbnailRequest& request, const std::atomic_bool& cancelled) {
  if (cancelled.load()) {
    return failure(outcomeError(HolonightImages::Outcome::Cancelled));
  }
  const auto path = request.native_path.isEmpty() ? QFile::encodeName(QFileInfo(request.path).absoluteFilePath())
                                                  : request.native_path;
  if (!HolonightFileBrowser::absoluteNativePath(path)) {
    return failure(QStringLiteral("Invalid image path."));
  }
  // POSIX open has a variadic signature even when no creation mode is needed.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  const int descriptor = ::open(path.constData(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
  if (descriptor < 0) {
    return failure(QStringLiteral("The file could not be opened for reading."));
  }
  QFile file;
  if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
    ::close(descriptor);
    return failure(QStringLiteral("The file could not be opened for reading."));
  }
  struct stat metadata{};
  if (::fstat(descriptor, &metadata) != 0 || !S_ISREG(metadata.st_mode)) {
    return failure(QStringLiteral("Not a regular image file."));
  }
  if (QFileInfo(QFile::decodeName(path)).suffix().compare(QLatin1String("svg"), Qt::CaseInsensitive) == 0) {
    return decodeSvg(file, path, metadata, request.box_pixels, cancelled);
  }
  const auto inspection = HolonightImages::inspect(file, kRasterLimits, cancelled);
  if (inspection.outcome != HolonightImages::Outcome::Success) {
    return failure(outcomeError(inspection.outcome));
  }
  const QSize pixelBox(request.box_pixels, request.box_pixels);
  const auto required =
      inspection.orientedSize.width() <= request.box_pixels && inspection.orientedSize.height() <= request.box_pixels
          ? inspection.orientedSize
          : inspection.orientedSize.scaled(pixelBox, Qt::KeepAspectRatio);
  const auto cache = cacheRequest(path, metadata, required, HolonightThumbnails::Kind::Raster);
  if (useDiskCache(request.box_pixels)) {
    if (auto hit = HolonightThumbnails::lookup(cache, cancelled)) {
      return finish(std::move(*hit), inspection.orientedSize);
    }
  }
  auto decoded = HolonightImages::decode(
      file, {.limits = kRasterLimits, .bound = {request.box_pixels, request.box_pixels}}, cancelled);
  if (decoded.outcome != HolonightImages::Outcome::Success || decoded.image.isNull()) {
    return failure(outcomeError(decoded.outcome));
  }
  auto result = finish(std::move(decoded.image), decoded.inspection.orientedSize);
  if (!result.image.isNull() && useDiskCache(request.box_pixels) && !cancelled.load() &&
      sourceMatches(path, metadata)) {
    HolonightThumbnails::store(cache, result.image, cancelled);
  }
  return result;
}
