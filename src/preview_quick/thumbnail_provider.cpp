#include <QFile>
#include <QMutexLocker>
#include <QQuickTextureFactory>
#include <QQuickWindow>
#include <QRunnable>

#include <HolonightFileBrowser/native_path.h>
#include <HolonightFileBrowser/preview_quick/thumbnail_provider.h>
#include <memory>
#include <sys/stat.h>
#include <utility>

namespace {
// Reports the logical size as the texture size while the texture keeps the device pixels.
class ThumbnailTexture : public QQuickTextureFactory {
 public:
  ThumbnailTexture(QImage image, QSize logicalSize) : image_(std::move(image)), logicalSize_(logicalSize) {}
  QSGTexture* createTexture(QQuickWindow* window) const override { return window->createTextureFromImage(image_); }
  [[nodiscard]] QSize textureSize() const override { return logicalSize_; }
  [[nodiscard]] int textureByteCount() const override { return static_cast<int>(image_.sizeInBytes()); }
  [[nodiscard]] QImage image() const override { return image_; }

 private:
  QImage image_;
  QSize logicalSize_;
};

QSize logicalSize(QSize source, QSize decoded) {
  if (source.isEmpty()) {
    return decoded;
  }
  const QSize box(kThumbnailBoxLogical, kThumbnailBoxLogical);
  if (source.width() <= box.width() && source.height() <= box.height()) {
    return source;
  }
  return source.scaled(box, Qt::KeepAspectRatio).expandedTo({1, 1});
}

// The logical size lives in the image's scale factor so a cached image carries it.
QImage tagged(QImage image, QSize source) {
  const auto logical = logicalSize(source, image.size());
  image.setDevicePixelRatio(static_cast<qreal>(image.width()) / logical.width());
  return image;
}

QSize logicalSizeOf(const QImage& image) {
  const auto size = image.deviceIndependentSize();
  return {qRound(size.width()), qRound(size.height())};
}

ThumbnailSource sourceFacts(const QByteArray& path) {
  struct stat metadata{};
  if (::stat(path.constData(), &metadata) != 0 || !S_ISREG(metadata.st_mode)) {
    return {};
  }
  const qint64 milliseconds =
      (static_cast<qint64>(metadata.st_mtim.tv_sec) * 1000) + (metadata.st_mtim.tv_nsec / 1000000);
  return {.size = metadata.st_size, .modified = QDateTime::fromMSecsSinceEpoch(milliseconds)};
}

struct RequestState {
  std::atomic_bool cancelled{false};
};

struct ParsedId {
  QByteArray path;
  int box_pixels = 0;
  int generation = 0;
  bool valid = false;
};

ParsedId parseId(const QString& identifier) {
  const auto parts = identifier.split(u'/');
  if (parts.size() != 3) {
    return {};
  }
  bool boxOk = false;
  bool generationOk = false;
  const int box = parts[0].toInt(&boxOk);
  const int generation = parts[1].toInt(&generationOk);
  const auto path = QByteArray::fromBase64(parts[2].toLatin1(), QByteArray::Base64UrlEncoding);
  if (!boxOk || !generationOk || box < 1 || !HolonightFileBrowser::absoluteNativePath(path)) {
    return {};
  }
  return {.path = path, .box_pixels = box, .generation = generation, .valid = true};
}
}  // namespace

class ThumbnailResponse : public QQuickImageResponse {
 public:
  ThumbnailResponse(ThumbnailProvider& owner, const QString& identifier)
      : owner_(owner), state_(std::make_shared<RequestState>()) {
    const auto parsed = parseId(identifier);
    if (!parsed.valid) {
      error_ = QStringLiteral("Invalid thumbnail request.");
      finishLater();
      return;
    }
    key_ = {.path = QFile::decodeName(parsed.path),
            .box_pixels = parsed.box_pixels,
            .generation = parsed.generation,
            .native_path = parsed.path};
    if (auto hit = owner_.cachedImage(key_)) {
      image_ = std::move(*hit);
      finishLater();
      return;
    }
    source_ = sourceFacts(key_.native_path);
    start();
  }

  [[nodiscard]] QQuickTextureFactory* textureFactory() const override {
    return image_.isNull() ? nullptr : new ThumbnailTexture(image_, logicalSizeOf(image_));
  }
  [[nodiscard]] QString errorString() const override { return error_; }

  void cancel() override;

  // On the response's own thread, by the queued result of a decode; a cancelled request is ignored.
  void complete(ThumbnailResult result) {
    if (state_->cancelled.load()) {
      return;
    }
    if (result.image.isNull()) {
      error_ = result.error.isEmpty() ? QStringLiteral("The image could not be decoded.") : result.error;
    } else {
      image_ = tagged(std::move(result.image), result.source_size);
      if (result.cache_eligible && source_.size >= 0 && sourceFacts(key_.native_path) == source_) {
        owner_.storeImage(key_, image_, source_);
      }
    }
    emitFinished();
  }

 private:
  void start();
  void finishLater() {
    QMetaObject::invokeMethod(this, [this] { emitFinished(); }, Qt::QueuedConnection);
  }
  void emitFinished() {
    if (!finished_) {
      finished_ = true;
      emit finished();
    }
  }

  ThumbnailProvider& owner_;
  std::shared_ptr<RequestState> state_;
  ThumbnailKey key_;
  ThumbnailSource source_;
  QImage image_;
  QString error_;
  bool finished_ = false;
};

// Reports through a signal, so the response can be destroyed at any moment without the worker touching it.
class ThumbnailTask : public QObject, public QRunnable {
  Q_OBJECT

 public:
  ThumbnailTask(ThumbnailProvider& owner, ThumbnailRequest request, std::shared_ptr<RequestState> state)
      : owner_(owner), request_(std::move(request)), state_(std::move(state)) {}

  void run() override {
    // Leave queued tasks owned by the pool. Cancellation only marks shared state,
    // so it never dereferences an auto-deleted runnable or removes a reused address.
    if (state_->cancelled.load()) {
      --owner_.pending_;
      return;
    }
    auto result = owner_.decoder_(request_, state_->cancelled);
    const bool cancelled = state_->cancelled.load();
    --owner_.pending_;
    if (!cancelled) {
      emit decoded(std::move(result));
    }
  }

 signals:
  // NOLINTNEXTLINE(readability-inconsistent-declaration-parameter-name)
  void decoded(ThumbnailResult result);

 private:
  ThumbnailProvider& owner_;
  ThumbnailRequest request_;
  std::shared_ptr<RequestState> state_;
};

void ThumbnailResponse::cancel() {
  state_->cancelled = true;
  image_ = {};
  finishLater();
}

void ThumbnailResponse::start() {
  auto* task = new ThumbnailTask(
      owner_, {.path = key_.path, .box_pixels = key_.box_pixels, .native_path = key_.native_path}, state_);
  // Queued, so the result is handled on this response's thread; the connection ends with the response.
  connect(
      task, &ThumbnailTask::decoded, this, [this](ThumbnailResult result) { complete(std::move(result)); },
      Qt::QueuedConnection);
  ++owner_.pending_;
  owner_.pool_.start(task);
}

ThumbnailProvider::ThumbnailProvider(Decoder decoder, int threadCount, qsizetype cacheEntries)
    : decoder_(std::move(decoder)), cache_(cacheEntries) {
  pool_.setMaxThreadCount(threadCount);
}

ThumbnailProvider::~ThumbnailProvider() {
  pool_.clear();
  pool_.waitForDone();
}

std::optional<QImage> ThumbnailProvider::cachedImage(const ThumbnailKey& key) {
  const auto source = sourceFacts(key.native_path);
  const QMutexLocker lock(&cacheMutex_);
  return cache_.find(key, source);
}

void ThumbnailProvider::storeImage(const ThumbnailKey& key, const QImage& image, const ThumbnailSource& source) {
  const QMutexLocker lock(&cacheMutex_);
  cache_.insert(key, image, source);
}

QString ThumbnailProvider::idFor(const QString& path, int boxPixels, int generation) {
  return idForNative(QFile::encodeName(path), boxPixels, generation);
}

QString ThumbnailProvider::idForNative(const QByteArray& path, int boxPixels, int generation) {
  const auto encoded = path.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
  return QStringLiteral("%1/%2/%3").arg(boxPixels).arg(generation).arg(QString::fromLatin1(encoded));
}

QQuickImageResponse* ThumbnailProvider::requestImageResponse(const QString& identifier,
                                                             const QSize& /*requestedSize*/) {
  return new ThumbnailResponse(*this, identifier);
}

#include "thumbnail_provider.moc"
