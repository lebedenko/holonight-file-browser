#pragma once

#include <QMutex>
#include <QObject>
#include <QQuickAsyncImageProvider>
#include <QThread>
#include <QThreadPool>

#include <HolonightFileBrowser/preview/thumbnail_cache.h>
#include <HolonightFileBrowser/preview/thumbnail_decoder.h>
#include <HolonightFileBrowser/preview/thumbnail_size.h>
#include <atomic>
#include <functional>
#include <optional>

// Registered as image://thumbnail/. The id is "<boxPixels>/<generation>/<base64url path>"; idFor is the only
// place that builds it. Qt calls requestImageResponse and cancel on its image-loading thread, not the GUI
// thread; each response lives there, and a decode finishes by a queued signal back to it. Decodes run on a
// private pool, the cache is guarded by a mutex, and a cancelled request is dropped before it starts or its
// result is never cached. The image reports its logical size (the
// source fitted into the logical box, never enlarged) as its texture size, so Image.implicitWidth is logical
// while the pixels stay at the device resolution.
// NOLINTNEXTLINE(cppcoreguidelines-special-member-functions)
class ThumbnailProvider : public QQuickAsyncImageProvider {
 public:
  using Decoder = std::function<ThumbnailResult(const ThumbnailRequest&, const std::atomic_bool&)>;

  explicit ThumbnailProvider(Decoder decoder = decodeThumbnail,
                             int threadCount = thumbnailThreadCount(QThread::idealThreadCount()),
                             qsizetype cacheEntries = kThumbnailCacheEntries);
  ~ThumbnailProvider() override;

  static QString idFor(const QString& path, int boxPixels, int generation);
  static QString idForNative(const QByteArray& path, int boxPixels, int generation);

  QQuickImageResponse* requestImageResponse(const QString& identifier, const QSize& requestedSize) override;

  [[nodiscard]] int maxThreadCount() const { return pool_.maxThreadCount(); }
  // Only for tests that have no request in flight: the cache is guarded by cacheMutex_ while requests run.
  [[nodiscard]] const ThumbnailCache& cache() const { return cache_; }
  // Queued plus running decodes.
  [[nodiscard]] int pendingCount() const { return pending_.load(); }

 private:
  friend class ThumbnailResponse;
  friend class ThumbnailTask;

  std::optional<QImage> cachedImage(const ThumbnailKey& key);
  void storeImage(const ThumbnailKey& key, const QImage& image, const ThumbnailSource& source);

  Decoder decoder_;
  QMutex cacheMutex_;
  ThumbnailCache cache_;
  QThreadPool pool_;
  std::atomic_int pending_{0};
};
