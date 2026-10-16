#pragma once

#include <QImage>
#include <QSize>
#include <QString>

#include <atomic>

struct ThumbnailRequest {
  QString path;
  int box_pixels = 0;
  QByteArray native_path = {};
};

// image.isNull() means the decode failed and error says why. source_size is the size the picture would
// have at scale factor 1 if the box did not limit it: the EXIF-oriented original for a raster, and the
// rendering at the logical box for an SVG, which scales up to it. Only meaningful when image is set.
struct ThumbnailResult {
  QImage image;
  QSize source_size;
  QString error;
  bool cache_eligible = true;
};

// Runs on a worker thread and touches no GUI state. The image is at most box_pixels on each side, is never
// enlarged unless it is an SVG, and holds the first frame of an animation.
ThumbnailResult decodeThumbnail(const ThumbnailRequest& request, const std::atomic_bool& cancelled);
