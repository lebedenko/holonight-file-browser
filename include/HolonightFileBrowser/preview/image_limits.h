#pragma once

#include <QImage>
#include <QImageReader>
#include <QSize>

#include <cstdlib>
#include <holonight_images/image.h>

// Decode policy shared by the static path and animation playback.
inline constexpr qint64 kImageLimitPixels = 64000000;
inline constexpr qint64 kImageLimitBytes = 256 * 1024 * 1024;
inline constexpr qint64 kFileLimitBytes = 256 * 1024 * 1024;
// SVG is XML text, not a raster codec; kFileLimitBytes/kImageLimitBytes do not apply to it.
inline constexpr qint64 kSvgFileLimitBytes = 10 * 1024 * 1024;

inline bool acceptableSize(QSize size) {
  return size.width() > 0 && size.height() > 0 && size.width() <= 32768 && size.height() <= 32768 &&
         static_cast<qint64>(size.width()) * size.height() <= kImageLimitPixels;
}

// Whether a decoded image fits the viewing limits.
inline bool acceptableImage(const QImage& image) {
  return !image.isNull() && acceptableSize(image.size()) && image.sizeInBytes() <= kImageLimitBytes;
}

// Sets the process-wide allocation policy, including Qt's environment override. Idempotent.
inline void configureDecodeLimits() {
  qputenv("QT_IMAGEIO_MAXALLOC", "256");
  QImageReader::setAllocationLimit(256);
}

inline constexpr HolonightImages::Limits kRasterLimits{.inputBytes = kFileLimitBytes,
                                                       .sourcePixels = kImageLimitPixels,
                                                       .sourceExtent = 32768,
                                                       .decodedBytes = kImageLimitBytes,
                                                       .metadataBytes = 1024 * 1024,
                                                       .tiffMetadataBytes = 64 * 1024 * 1024,
                                                       .metadataRecords = 4096};
