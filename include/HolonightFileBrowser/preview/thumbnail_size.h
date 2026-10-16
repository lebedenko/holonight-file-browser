#pragma once

#include <QtGlobal>

#include <algorithm>

// The one definition of the thumbnail box; scripts/check-thumbnail-constant.py rejects any other literal.
inline constexpr int kThumbnailBoxLogical = 256;
inline constexpr int kThumbnailCacheEntries = 1000;
// Bounds the cache at HiDPI, where 1000 entries alone would reach about 0.8 GB.
inline constexpr qint64 kThumbnailCacheBytes = 256LL * 1024 * 1024;

// Device pixels of the box at a scale factor; the epsilon keeps 2.0000000001 from rounding up to 513.
constexpr int thumbnailBoxPixels(qreal dpr) {
  const qreal pixels = (kThumbnailBoxLogical * dpr) - 1e-6;
  const int whole = static_cast<int>(pixels);
  return whole < pixels ? whole + 1 : whole;
}

// Half the ideal thread count leaves the rest for the GUI thread and the animation decoder.
constexpr int thumbnailThreadCount(int idealThreads) { return std::max(1, idealThreads / 2); }
