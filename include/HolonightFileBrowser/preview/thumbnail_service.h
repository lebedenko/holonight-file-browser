#pragma once

#include <QFile>
#include <QImage>
#include <QSize>
#include <QString>

#include <HolonightFileBrowser/preview/runtime_options.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <holonight_images/image.h>
#include <optional>
#include <utility>

// Synchronous helpers used on PreviewService's verified source descriptor.
namespace ThumbnailService {
using HolonightFileBrowser::ImageRuntimeOptions;
// Internal per-request synchronization seam; never exposed to QML or stored globally.
enum class Stage : std::uint8_t {
  CacheInspect,
  CacheInspected,
  CacheDecode,
  OriginalDecode,
  OriginalDecoded,
  BeforeCommit,
};
using StageCallback = std::function<void(Stage)>;
struct Result {
  QImage image;
  HolonightImages::Outcome outcome;
  HolonightImages::RuntimeStatus runtime = HolonightImages::RuntimeStatus::Success;
  QString diagnostic;
  Result(QImage pixels, HolonightImages::Outcome status) : image(std::move(pixels)), outcome(status) {}
};

enum class ImageKind : std::uint8_t { Raster, Svg };

enum class Tier : std::uint16_t { Normal = 128, Large = 256, XLarge = 512, XXLarge = 1024 };
QSize requiredSize(QSizeF source, QSize bound, ImageKind kind = ImageKind::Raster);
std::optional<Tier> tierForSize(QSize required);
std::optional<Result> lookup(QFile& file, const QString& path, const QString& revision, Tier selected, QSize required,
                             const std::atomic_bool& cancelled, const StageCallback& stage = {},
                             ImageKind kind = ImageKind::Raster, const QByteArray& nativePath = {},
                             const ImageRuntimeOptions& options = HolonightFileBrowser::defaultImageRuntimeOptions());
Result renderSvg(QFile& file, const QString& path, const QString& revision, const QByteArray& bytes, QSize bound,
                 std::optional<Tier> tier, const std::atomic_bool& cancelled, const StageCallback& stage = {},
                 const QByteArray& nativePath = {});
Result lookupOrDecode(QFile& file, const QString& path, const QString& revision, Tier tier, QSize required,
                      const std::atomic_bool& cancelled, const StageCallback& stage = {},
                      const QByteArray& nativePath = {},
                      const ImageRuntimeOptions& options = HolonightFileBrowser::defaultImageRuntimeOptions());
Result lookupOrDecode(const QString& path,
                      const ImageRuntimeOptions& options = HolonightFileBrowser::defaultImageRuntimeOptions());
Result lookupOrDecode(QFile& file, const QString& path, const QString& revision, const std::atomic_bool& cancelled,
                      const ImageRuntimeOptions& options = HolonightFileBrowser::defaultImageRuntimeOptions());
Result decodeScaled(const QString& path, QSize targetSize,
                    const ImageRuntimeOptions& options = HolonightFileBrowser::defaultImageRuntimeOptions());
Result decodeScaled(QFile& file, QSize targetSize, const std::atomic_bool& cancelled, const StageCallback& stage = {},
                    const ImageRuntimeOptions& options = HolonightFileBrowser::defaultImageRuntimeOptions());
}  // namespace ThumbnailService
