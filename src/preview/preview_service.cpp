#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>

#include <HolonightFileBrowser/preview/exif_reader.h>
#include <HolonightFileBrowser/preview/image_policy.h>
#include <HolonightFileBrowser/preview/preview_service.h>
#include <HolonightFileBrowser/preview/text_preview_service.h>
#include <HolonightFileBrowser/preview/thumbnail_service.h>
#include <cstring>
#include <fcntl.h>
#include <holonight_images/svg.h>
#include <list>
#include <sys/stat.h>
#include <unistd.h>

// The value type that crosses the worker->UI queued connection. Only PreviewService's own
// dispatch()/applyResult() (and the free runPreviewJob() worker function below) ever touch one.
struct PreviewResult {
  HolonightImages::RuntimeStatus runtime = HolonightImages::RuntimeStatus::Success;
  QString diagnostic;
  quint64 generation = 0;
  bool final = true;
  QString mime_type;
  QString mime_type_description;
  // MIME used only for Quick Look gating: the sniffed type, or an extension-only guess when the file
  // cannot be opened or read, so an unreadable text/plain file can still open Quick Look and show
  // its error.
  QString gate_mime;
  QImage image;
  std::optional<HolonightImages::Outcome> raster_outcome;
  QSize source_pixel_size;
  QSizeF document_size;
  ThumbnailService::ImageKind image_kind = ThumbnailService::ImageKind::Raster;
  ExifReader::ExifSummary exif;
  bool has_text = false;
  TextPreviewService::TextPreviewResult text;
  PreviewService::PreviewError error;
};

struct PreviewWorkerCache {
  struct RasterFacts {
    QSize source_size;
    ExifReader::ExifSummary exif;
  };
  struct Entry {
    QString key;
    QImage image;
    std::optional<RasterFacts> raster;
  };
  std::list<Entry> entries;
  qint64 bytes = 0;
  QImage lookup(const QString& key, QSize size) {
    for (auto it = entries.begin(); it != entries.end(); ++it) {
      if (it->key == key && it->image.width() >= size.width() && it->image.height() >= size.height()) {
        auto image = it->image;
        entries.splice(entries.begin(), entries, it);
        return image;
      }
    }
    return {};
  }
  [[nodiscard]] std::optional<Entry> rasterCandidate(const QString& key, QSize bound) const {
    for (const auto& entry : entries) {
      if (entry.key != key || !entry.raster) {
        continue;
      }
      const auto needed = ThumbnailService::requiredSize(entry.raster->source_size, bound);
      if (entry.image.width() >= needed.width() && entry.image.height() >= needed.height()) {
        return entry;
      }
    }
    return std::nullopt;
  }
  void remove(const QString& key) {
    for (auto it = entries.begin(); it != entries.end();) {
      if (it->key == key) {
        bytes -= it->image.sizeInBytes();
        it = entries.erase(it);
      } else {
        ++it;
      }
    }
  }
  void insert(const QString& key, const QImage& image, std::optional<RasterFacts> raster = std::nullopt) {
    constexpr qint64 budget = 64LL * 1024 * 1024;
    if (image.isNull() || image.sizeInBytes() > budget) {
      return;
    }
    remove(key);
    while (!entries.empty() && (entries.size() >= 2 || bytes + image.sizeInBytes() > budget)) {
      bytes -= entries.back().image.sizeInBytes();
      entries.pop_back();
    }
    entries.push_front({.key = key, .image = image, .raster = std::move(raster)});
    bytes += image.sizeInBytes();
  }
};

namespace {
using HolonightFileBrowser::ImageRuntimeOptions;
PreviewService::PreviewError runtimeError(HolonightImages::RuntimeStatus status) {
  using HolonightImages::RuntimeStatus;
  using Kind = PreviewService::PreviewErrorKind;
  Kind kind = Kind::None;
  switch (status) {
    case RuntimeStatus::Success:
    case RuntimeStatus::Cancelled:
      return {};
    case RuntimeStatus::Unavailable:
      kind = Kind::RuntimeUnavailable;
      break;
    case RuntimeStatus::ProtocolFailure:
      kind = Kind::RuntimeFailure;
      break;
    case RuntimeStatus::CleanupFailure:
      kind = Kind::CleanupFailure;
      break;
    case RuntimeStatus::Timeout:
      kind = Kind::DecodeTimeout;
      break;
  }
  return {.kind = kind, .message = HolonightFileBrowser::imageRuntimeError(status)};
}
constexpr int kDecodeTimeoutMs = 3000;
constexpr int kResizeDebounceMs = 150;
constexpr qint64 kTextViewerMaxBytes = 102400;
constexpr qint64 kSniffBytes = 8192;

QString sourceIdentity(const struct stat& info) {
  return QStringLiteral("%1:%2:%3:%4:%5:%6:%7")
      .arg(info.st_dev)
      .arg(info.st_ino)
      .arg(info.st_size)
      .arg(info.st_mtim.tv_sec)
      .arg(info.st_mtim.tv_nsec)
      .arg(info.st_ctim.tv_sec)
      .arg(info.st_ctim.tv_nsec);
}

bool unchangedSource(QFile& file, const QString& identity) {
  struct stat info{};
  return ::fstat(file.handle(), &info) == 0 && S_ISREG(info.st_mode) && sourceIdentity(info) == identity;
}

QString formatPermissions(quint32 rawMode) {
  const auto mode = static_cast<mode_t>(rawMode);
  QString result = QStringLiteral("----------");
  if (S_ISDIR(mode)) {
    result[0] = u'd';
  } else if (S_ISLNK(mode)) {
    result[0] = u'l';
  } else if (S_ISCHR(mode)) {
    result[0] = u'c';
  } else if (S_ISBLK(mode)) {
    result[0] = u'b';
  } else if (S_ISFIFO(mode)) {
    result[0] = u'p';
  } else if (S_ISSOCK(mode)) {
    result[0] = u's';
  }
  const auto setBit = [&](int index, quint32 bit, QChar letter) {
    if ((rawMode & bit) != 0) {
      result[index] = letter;
    }
  };
  setBit(1, S_IRUSR, u'r');
  setBit(2, S_IWUSR, u'w');
  setBit(3, S_IXUSR, u'x');
  setBit(4, S_IRGRP, u'r');
  setBit(5, S_IWGRP, u'w');
  setBit(6, S_IXGRP, u'x');
  setBit(7, S_IROTH, u'r');
  setBit(8, S_IWOTH, u'w');
  setBit(9, S_IXOTH, u'x');
  if ((rawMode & S_ISUID) != 0) {
    result[3] = (rawMode & S_IXUSR) != 0 ? u's' : u'S';
  }
  if ((rawMode & S_ISGID) != 0) {
    result[6] = (rawMode & S_IXGRP) != 0 ? u's' : u'S';
  }
  if ((rawMode & S_ISVTX) != 0) {
    result[9] = (rawMode & S_IXOTH) != 0 ? u't' : u'T';
  }
  return result;
}

// Keep progressive publication and cancellation checkpoints in request order.
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
PreviewResult decodeImage(QFile& file, const QString& path, const QByteArray& nativePath, QString identity,
                          PreviewResult result, const std::shared_ptr<std::atomic_bool>& cancel, QSize requestedSize,
                          PreviewWorkerCache& cache, const std::function<void(const PreviewResult&)>& publish,
                          const std::function<void()>& beforeFullDecode, const ThumbnailService::StageCallback& stage,
                          const ImageRuntimeOptions& options, const std::function<void()>& memoryCheckpoint) {
  if (const auto candidate = cache.rasterCandidate(identity, requestedSize)) {
    if (memoryCheckpoint) {
      memoryCheckpoint();
    }
    if (cancel->load()) {
      return result;
    }
    if (unchangedSource(file, identity)) {
      result.image = cache.lookup(identity, candidate->image.size());
      result.source_pixel_size = candidate->raster->source_size;
      result.document_size = result.source_pixel_size;
      result.raster_outcome = HolonightImages::Outcome::Success;
      result.final = false;
      if (!cancel->load()) {
        publish(result);
      }
      result.final = true;
      result.image = {};
      result.exif = candidate->raster->exif;
      return result;
    }
    cache.remove(identity);
    // The miss path must not read a disk entry using the identity we just rejected.
    struct stat current{};
    if (::fstat(file.handle(), &current) != 0 || !S_ISREG(current.st_mode)) {
      result.raster_outcome = HolonightImages::Outcome::IoFailure;
      return result;
    }
    identity = sourceIdentity(current);
  }
  std::optional<ThumbnailService::Tier> tier;
  QSize needed;
  bool decodedSource = false;
  const auto reply =
      HolonightImages::IsolatedClient(options.directory)
          .sourceTransaction(
              file, kPreviewImageLimits, *cancel,
              [&](const HolonightImages::Inspection& inspection, HolonightImages::SourceTransaction& transaction) {
                HolonightImages::DecodeResult selected;
                selected.outcome = inspection.outcome;
                if (inspection.outcome != HolonightImages::Outcome::Success || cancel->load()) {
                  return selected;
                }
                needed = ThumbnailService::requiredSize(inspection.orientedSize, requestedSize);
                tier = inspection.orientedSize.isValid() && !inspection.orientedSize.isEmpty()
                           ? ThumbnailService::tierForSize(needed)
                           : std::nullopt;
                selected.image = cache.lookup(identity, needed);
                if (selected.image.isNull() && tier) {
                  auto cached = ThumbnailService::lookup(file, path, identity, *tier, needed, transaction, *cancel,
                                                         stage, nativePath);
                  if (cached) {
                    selected.image = std::move(cached->image);
                  }
                }
                if (!selected.image.isNull() || cancel->load()) {
                  return selected;
                }
                if (beforeFullDecode) {
                  beforeFullDecode();
                }
                if (cancel->load()) {
                  return selected;
                }
                const auto bound = tier ? QSize(static_cast<int>(*tier), static_cast<int>(*tier)) : requestedSize;
                if (stage) {
                  stage(ThumbnailService::Stage::OriginalDecode);
                }
                selected = transaction.decodeSource({.limits = kPreviewImageLimits, .bound = bound});
                decodedSource = true;
                if (stage) {
                  stage(ThumbnailService::Stage::OriginalDecoded);
                }
                return selected;
              });
  result.runtime = reply.runtime;
  result.diagnostic = reply.diagnostic;
  if (reply.runtime != HolonightImages::RuntimeStatus::Success || !reply.result) {
    return result;
  }
  const auto& inspection = reply.result->inspection;
  result.raster_outcome = reply.result->outcome;
  result.source_pixel_size = inspection.orientedSize;
  result.document_size = inspection.orientedSize;
  result.image = reply.result->image;
  if (cancel->load() || result.image.isNull()) {
    return result;
  }
  if (decodedSource && tier && result.raster_outcome == HolonightImages::Outcome::Success &&
      unchangedSource(file, identity)) {
    ThumbnailService::storeRaster(file, path, identity, *tier, needed, result.image, *cancel, stage, nativePath);
  }
  if (cancel->load()) {
    return result;
  }
  const auto image = result.image;
  result.final = false;
  if (cancel->load()) {
    return result;
  }
  publish(result);
  result.final = true;
  result.image = {};  // EXIF completion updates metadata without replacing pixels.
  if (cancel->load()) {
    return result;
  }
  if (result.mime_type == QLatin1String("image/jpeg") || result.mime_type == QLatin1String("image/png")) {
    result.exif = ExifReader::format(inspection.metadata);
  }
  if (!cancel->load() && result.raster_outcome == HolonightImages::Outcome::Success &&
      unchangedSource(file, identity) && !cancel->load()) {
    cache.insert(identity, image,
                 PreviewWorkerCache::RasterFacts{.source_size = result.source_pixel_size, .exif = result.exif});
  }
  return result;
}

PreviewResult decodeSvg(QFile& file, const QString& path, const QByteArray& nativePath, const QString& identity,
                        PreviewResult result, const std::shared_ptr<std::atomic_bool>& cancel, QSize requestedSize,
                        PreviewWorkerCache& cache, const std::function<void()>& beforeFullDecode,
                        const ThumbnailService::StageCallback& stage, const ImageRuntimeOptions& options) {
  if (!requestedSize.isValid() || requestedSize.isEmpty()) {
    requestedSize = QSize(1024, 1024);
  }
  result.image_kind = ThumbnailService::ImageKind::Svg;
  result.mime_type = QStringLiteral("image/svg+xml");
  result.gate_mime = result.mime_type;
  const auto source = HolonightImages::loadSvg(file, kPreviewSvgInputBytes, *cancel);
  result.raster_outcome = source.outcome;
  if (source.outcome != HolonightImages::Outcome::Success) {
    return result;
  }
  const auto inspection = HolonightImages::inspectSvg(source.bytes, *cancel);
  result.raster_outcome = inspection.outcome;
  if (inspection.outcome == HolonightImages::Outcome::Unsupported) {
    result.raster_outcome.reset();
    result.error = {
        .kind = PreviewService::PreviewErrorKind::Unsupported,
        .message = QObject::tr("This SVG cannot be previewed because it contains unsupported resource "
                               "references. Open it in Viewer to view local linked images."),
    };
    return result;
  }
  if (inspection.outcome != HolonightImages::Outcome::Success || cancel->load()) {
    return result;
  }
  result.document_size = inspection.facts.documentSize;
  result.source_pixel_size = result.document_size.toSize().expandedTo(QSize(1, 1));
  const auto needed = ThumbnailService::requiredSize(result.document_size, requestedSize, result.image_kind);
  const auto tier = ThumbnailService::tierForSize(needed);
  // Resource validation precedes both memory and disk lookup, on every request.
  const auto cacheIdentity = identity + QStringLiteral(":svg-self-contained-static-v1");
  result.image = cache.lookup(cacheIdentity, needed);
  if (result.image.isNull() && tier) {
    const auto cached = ThumbnailService::lookup(file, path, identity, *tier, needed, *cancel, stage, result.image_kind,
                                                 nativePath, options);
    if (cached) {
      result.image = cached->image;
      result.raster_outcome = cached->outcome;
      result.runtime = cached->runtime;
      result.diagnostic = cached->diagnostic;
      if (result.runtime != HolonightImages::RuntimeStatus::Success) {
        return result;
      }
    }
  }
  if (cancel->load() || result.raster_outcome == HolonightImages::Outcome::Cancelled) {
    return result;
  }
  if (result.image.isNull()) {
    if (beforeFullDecode) {
      beforeFullDecode();
    }
    auto rendered = ThumbnailService::renderSvg(file, path, identity, source.bytes, requestedSize, tier, *cancel, stage,
                                                nativePath);
    result.image = std::move(rendered.image);
    result.raster_outcome = rendered.outcome;
  }
  if (!cancel->load() && !result.image.isNull()) {
    cache.insert(cacheIdentity, result.image);
  }
  return result;
}

// Runs entirely on PreviewService's worker thread. Mirrors DirectoryModel's walkDirectory: a free
// function with no Qt object identity, cooperatively cancellable at each stage boundary.
PreviewResult runPreviewJob(const QString& path, const QByteArray& nativePath, quint64 generation,
                            const std::shared_ptr<std::atomic_bool>& cancel, QSize requestedSize,
                            PreviewWorkerCache& cache, const std::function<void(const PreviewResult&)>& publish,
                            const std::function<void()>& beforeFullDecode, const ThumbnailService::StageCallback& stage,
                            const ImageRuntimeOptions& options, bool textOnly = false,
                            const std::function<void()>& memoryCheckpoint = {}) {
  PreviewResult result;
  result.generation = generation;
  if (cancel->load()) {
    return result;
  }
  if (!HolonightFileBrowser::absoluteNativePath(nativePath)) {
    result.error = {.kind = PreviewService::PreviewErrorKind::IoFailure,
                    .message = QObject::tr("Preview path must be an absolute local path")};
    return result;
  }
  // In-memory glob match only: no file I/O.
  result.gate_mime = QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchExtension).name();
  // POSIX open is required for nonblocking, close-on-exec descriptor verification.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  const int descriptor = ::open(nativePath.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (descriptor < 0) {
    result.error = {
        .kind = PreviewService::PreviewErrorKind::PermissionDenied,
        .message = QString::fromLocal8Bit(std::strerror(errno)),
    };
    return result;
  }
  QFile file(path);
  if (!file.open(descriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
    ::close(descriptor);
    result.error = {.kind = PreviewService::PreviewErrorKind::PermissionDenied, .message = file.errorString()};
    return result;
  }
  struct stat info{};
  if (::fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode)) {
    result.error = {
        .kind = PreviewService::PreviewErrorKind::Unsupported,
        .message = QObject::tr("No preview available"),
    };
    return result;
  }
  const QString identity = sourceIdentity(info);
  const auto sniff = file.read(kSniffBytes);
  if (file.error() != QFileDevice::NoError) {
    result.error = {.kind = PreviewService::PreviewErrorKind::DecodeFailed, .message = file.errorString()};
    return result;
  }
  if (cancel->load()) {
    return result;
  }
  QMimeDatabase mimeDatabase;
  const auto mime = mimeDatabase.mimeTypeForFileNameAndData(path, sniff);
  result.mime_type = mime.name();
  result.gate_mime = mime.name();
  result.mime_type_description = mime.comment();
  if (textOnly) {
    const bool supported =
        mime.name() == QLatin1String("text/plain") || mime.name() == QLatin1String("application/x-zerosize") ||
        mime.name() == QLatin1String("text/markdown") || mime.name() == QLatin1String("text/x-markdown");
    if (!supported || TextPreviewService::looksBinary(sniff)) {
      result.error = {
          .kind = PreviewService::PreviewErrorKind::Unsupported,
          .message = QObject::tr("No preview available"),
      };
    } else if (!cancel->load()) {
      result.text = TextPreviewService::readHead(file, kTextViewerMaxBytes);
      result.text.canonical_parent = QFileInfo(path).dir().canonicalPath();
      result.has_text = result.text.error.isEmpty();
      if (!result.has_text) {
        result.error = {.kind = PreviewService::PreviewErrorKind::DecodeFailed, .message = result.text.error};
      }
    }
    return result;
  }
  if (mime.name() == QLatin1String("image/svg+xml") ||
      QFileInfo(path).suffix().compare(QLatin1String("svg"), Qt::CaseInsensitive) == 0) {
    return decodeSvg(file, path, nativePath, identity, result, cancel, requestedSize, cache, beforeFullDecode, stage,
                     options);
  }
  if (mime.name() == QLatin1String("image/svg+xml-compressed")) {
    result.error = {
        .kind = PreviewService::PreviewErrorKind::Unsupported,
        .message = QObject::tr("Compressed SVG previews are not supported."),
    };
    return result;
  }
  if (mime.name().startsWith(QStringLiteral("image/"))) {
    return decodeImage(file, path, nativePath, identity, result, cancel, requestedSize, cache, publish,
                       beforeFullDecode, stage, options, memoryCheckpoint);
  }
  return result;
}
}  // namespace

PreviewService::PreviewService(QObject* parent)
    : PreviewService(HolonightFileBrowser::defaultImageRuntimeOptions(), parent) {}

PreviewService::PreviewService(ImageRuntimeOptions options, QObject* parent)
    : QObject(parent),
      runtime_options_(std::move(options)),
      worker_(new QObject),
      cache_(std::make_shared<PreviewWorkerCache>()) {
  worker_->moveToThread(&thread_);
  connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
  connect(&thread_, &QThread::finished, this, &PreviewService::shutdownFinished);
  timeout_timer_.setSingleShot(true);
  connect(&timeout_timer_, &QTimer::timeout, this, [this] {
    if (!busy_) {
      return;
    }
    busy_ = false;
    timed_out_ = true;
    if (cancellation_) {
      cancellation_->store(true);
    }
    pending_job_ = false;
    resetDisplayState();
    error_ = {.kind = PreviewErrorKind::DecodeTimeout, .message = tr("Cannot decode image")};
    quick_look_error_ = error_;
    notifyChanged();
  });
  resize_debounce_timer_.setSingleShot(true);
  connect(&resize_debounce_timer_, &QTimer::timeout, this, [this] {
    if (has_entry_ && !is_dir_ && !timed_out_) {
      const auto needed = ThumbnailService::requiredSize(document_size_, requested_size_, image_kind_);
      const auto available = busy_ ? dispatched_size_ : display_image_.size();
      if (needed.width() > available.width() || needed.height() > available.height()) {
        dispatch();
      }
    }
  });
  thread_.start();
}

PreviewService::~PreviewService() {
  cancelText();
  if (cancellation_) {
    cancellation_->store(true);
  }
  thread_.quit();
  thread_.wait();
}

void PreviewService::setTarget(const QString& path, bool isDir, qint64 size, const QDateTime& modified, quint32 mode,
                               bool statFailed, const QString& statError, const QString& iconName, quint64 revision,
                               const QString& displayName, const QByteArray& nativePath) {
  if (path.isEmpty()) {
    clear();
    return;
  }
  const auto native = nativePath.isEmpty() ? QFile::encodeName(path) : nativePath;
  const auto effectiveName = displayName.isEmpty() ? QFileInfo(path).fileName() : displayName;
  if (has_entry_ && native_path_ == native && path_ == path && name_ == effectiveName && size_ == size &&
      modified_ == modified && mode_ == mode && is_dir_ == isDir && stat_failed_ == statFailed &&
      stat_error_ == statError && icon_name_ == iconName && revision_ == revision) {
    return;
  }
  mode_ = mode;
  icon_name_ = iconName;
  stat_failed_ = statFailed;
  stat_error_ = statError;
  revision_ = revision;
  cancelInFlight();
  busy_ = false;
  if (native_path_ != native || path_ != path || name_ != effectiveName) {
    retained_line_ = 0;
  }
  path_ = path;
  native_path_ = native;
  name_ = effectiveName;
  size_ = size;
  modified_ = modified;
  permissions_ = formatPermissions(mode);
  is_dir_ = isDir;
  has_entry_ = true;
  resetDisplayState();
  mime_type_.clear();
  if (statFailed) {
    // REQ-F-024: stat-level failures (broken symlinks, EACCES on the containing directory) are
    // already fully described by Stage 1's StatFailedRole/StatErrorRole; no new I/O needed.
    mime_type_.clear();
    error_ = {
        .kind = statError.contains(QStringLiteral("Broken symbolic link")) ? PreviewErrorKind::BrokenSymlink
                                                                           : PreviewErrorKind::PermissionDenied,
        .message = statError,
    };
  } else if (!S_ISREG(mode) && !S_ISLNK(mode) && !isDir) {
    mime_type_ = S_ISFIFO(mode) ? QStringLiteral("inode/fifo") : QStringLiteral("application/octet-stream");
  } else if (isDir) {
    mime_type_ = QStringLiteral("inode/directory");
  } else {
    busy_ = !stopping_;
  }
  quick_look_error_ = error_;
  // Reset presentation before property notifications, with the complete initial state readable.
  emit selectionChanged();
  if (busy_) {
    dispatch();
  } else {
    notifyChanged();
  }
}

void PreviewService::clear() {
  cancelInFlight();
  has_entry_ = false;
  path_.clear();
  native_path_.clear();
  name_.clear();
  size_ = -1;
  modified_ = QDateTime();
  permissions_.clear();
  mime_type_.clear();
  icon_name_.clear();
  is_dir_ = false;
  busy_ = false;
  retained_line_ = 0;
  resetDisplayState();
  emit selectionChanged();
  notifyChanged();
}

void PreviewService::setRequestedSize(PreviewConsumer consumer, QSize pixels) {
  if (!pixels.isValid() || pixels.isEmpty()) {
    return;
  }
  auto& stored = consumer == PreviewConsumer::Pane ? pane_size_ : quick_look_size_;
  stored = pixels;
  updateRequestedSize();
}

void PreviewService::setQuickLookActive(bool active) {
  if (active == quick_look_active_) {
    return;
  }
  cancelText();
  quick_look_active_ = active;
  quick_look_error_ = error_;
  retained_line_ = 0;
  if (active) {
    dispatchText();
  }
  notifyChanged();
  updateRequestedSize();
}

void PreviewService::cancelText() {
  ++text_request_;
  if (text_cancellation_) {
    text_cancellation_->store(true);
  }
  text_cancellation_.reset();
  text_busy_ = false;
  has_text_ = false;
  text_ = {};
  text_lines_.clear();
  current_line_ = -1;
  quick_look_error_ = {};
}

void PreviewService::dispatchText() {
  if (stopping_ || !quick_look_active_ || busy_ || !quickLookText() || text_busy_ || has_text_) {
    return;
  }
  text_busy_ = true;
  const auto request = ++text_request_;
  const auto generation = generation_;
  const auto revision = revision_;
  const auto path = path_;
  const auto nativePath = native_path_;
  const auto cancel = std::make_shared<std::atomic_bool>(false);
  text_cancellation_ = cancel;
  const auto beforeText = before_text_for_test_;
  const auto options = runtime_options_;
  QMetaObject::invokeMethod(
      worker_,
      [this, request, generation, revision, path, nativePath, cancel, beforeText, options] {
        if (!cancel->load() && beforeText) {
          beforeText();
        }
        const auto result = runPreviewJob(path, nativePath, generation, cancel, {}, *cache_, {}, {}, {}, options, true);
        if (cancel->load()) {
          return;
        }
        QMetaObject::invokeMethod(
            this,
            [this, request, revision, result] {
              if (stopping_ || !quick_look_active_ || request != text_request_ || revision != revision_ ||
                  result.generation != generation_) {
                return;
              }
              text_busy_ = false;
              quick_look_error_ = result.error;
              has_text_ = result.has_text;
              text_ = result.text;
              text_lines_.setLines(text_.lines);
              current_line_ = has_text_ ? qMin(retained_line_, static_cast<int>(text_.lines.size()) - 1) : -1;
              notifyChanged();
            },
            Qt::QueuedConnection);
      },
      Qt::QueuedConnection);
}

void PreviewService::updateRequestedSize() {
  const auto size = quick_look_active_ ? quick_look_size_ : pane_size_;
  if (!size.isValid() || size.isEmpty() || size == requested_size_) {
    return;
  }
  requested_size_ = size;
  if (has_entry_ && mime_type_.startsWith(QStringLiteral("image/")) && !timed_out_) {
    resize_debounce_timer_.start(kResizeDebounceMs);
  }
}

void PreviewService::shutdown() {
  if (stopping_) {
    return;
  }
  stopping_ = true;
  cancelInFlight();
  if (cancellation_) {
    cancellation_->store(true);
  }
  thread_.quit();
}

void PreviewService::dispatch() {
  if (stopping_) {
    return;
  }
  cancelInFlight();
  cancellation_ = std::make_shared<std::atomic_bool>(false);
  busy_ = true;
  timeout_timer_.start(kDecodeTimeoutMs);
  pending_job_ = true;
  notifyChanged();
  if (!active_job_) {
    startJob();
  }
}

void PreviewService::startJob() {
  if (!pending_job_ || stopping_) {
    return;
  }
  pending_job_ = false;
  active_job_ = true;
  const auto generation = generation_;
  const auto cancel = cancellation_;
  const auto path = path_;
  const auto nativePath = native_path_;
  const auto size = requested_size_.isValid() && !requested_size_.isEmpty() ? requested_size_ : QSize(1024, 1024);
  dispatched_size_ = ThumbnailService::requiredSize(document_size_, size, image_kind_);
  const auto beforeDispatch = before_dispatch_for_test_;
  const auto stage = thumbnail_stage_for_test_;
  const auto beforeFull = before_full_decode_for_test_;
  const auto memoryCheckpoint = memory_lookup_for_test_;
  const auto options = runtime_options_;
  QMetaObject::invokeMethod(
      worker_,
      [this, path, nativePath, generation, cancel, size, beforeDispatch, beforeFull, stage, options, memoryCheckpoint] {
        const auto publish = [this](const PreviewResult& result) {
          QMetaObject::invokeMethod(this, [this, result] { applyResult(result); }, Qt::QueuedConnection);
        };
        if (!cancel->load() && beforeDispatch) {
          beforeDispatch();
        }
        auto result = runPreviewJob(path, nativePath, generation, cancel, size, *cache_, publish, beforeFull, stage,
                                    options, false, memoryCheckpoint);
        if (result.runtime != HolonightImages::RuntimeStatus::Success &&
            result.runtime != HolonightImages::RuntimeStatus::Cancelled) {
          qWarning() << "Preview runtime failure" << static_cast<int>(result.runtime) << result.diagnostic;
        }
        if (!cancel->load()) {
          publish(result);
        }
        QMetaObject::invokeMethod(
            this,
            [this] {
              active_job_ = false;
              startJob();
            },
            Qt::QueuedConnection);
      },
      Qt::QueuedConnection);
}

void PreviewService::applyResult(const PreviewResult& result) {
  if (result.runtime == HolonightImages::RuntimeStatus::Cancelled ||
      (result.runtime == HolonightImages::RuntimeStatus::Success &&
       result.raster_outcome == HolonightImages::Outcome::Cancelled)) {
    if (result.generation == generation_ && !timed_out_) {
      timeout_timer_.stop();
      busy_ = false;
    }
    return;
  }
  if (result.generation != generation_) {
    return;  // Stale: the target (or its requested size) has moved on.
  }
  if (timed_out_) {
    // A slow-but-eventually-successful decode must not silently replace an already-shown
    // "Cannot decode image" notice — only a new setTarget()/dispatch() call clears timed_out_.
    return;
  }
  if (result.final) {
    timeout_timer_.stop();
  }
  busy_ = !result.final;
  mime_type_ = result.mime_type;
  mime_type_description_ = result.mime_type_description;
  gate_mime_ = result.gate_mime;
  if (!result.image.isNull() && (display_image_.isNull() || (result.image.width() >= display_image_.width() &&
                                                             result.image.height() >= display_image_.height()))) {
    display_image_ = result.image;
  }
  source_pixel_size_ = result.source_pixel_size;
  document_size_ = result.document_size;
  image_kind_ = result.image_kind;
  exif_ = result.exif;
  if (result.runtime != HolonightImages::RuntimeStatus::Success) {
    error_ = runtimeError(result.runtime);
  } else {
    error_ = result.raster_outcome ? rasterError(*result.raster_outcome) : result.error;
  }
  quick_look_error_ = error_;
  if (result.final && quick_look_active_) {
    if (quickLookEligibility() == QuickLookEligibility::Unsupported && error_.kind == PreviewErrorKind::None) {
      quick_look_error_ = {.kind = PreviewErrorKind::Unsupported, .message = tr("No preview available")};
    }
    dispatchText();
  }
  notifyChanged();
  if (result.final && mime_type_.startsWith(QStringLiteral("image/")) && !display_image_.isNull()) {
    resize_debounce_timer_.start(kResizeDebounceMs);
  }
}

void PreviewService::cancelInFlight() {
  cancelText();
  ++generation_;
  pending_job_ = false;
  if (cancellation_) {
    cancellation_->store(true);
  }
  cancellation_.reset();
  timeout_timer_.stop();
  resize_debounce_timer_.stop();
  timed_out_ = false;
}

void PreviewService::resetDisplayState() {
  display_image_ = QImage();
  source_pixel_size_ = QSize();
  document_size_ = {};
  image_kind_ = ThumbnailService::ImageKind::Raster;
  exif_ = ExifReader::ExifSummary();
  mime_type_description_.clear();
  gate_mime_.clear();
  has_text_ = false;
  text_ = TextPreviewService::TextPreviewResult();
  text_lines_.clear();
  current_line_ = -1;
  error_ = PreviewError();
}

void PreviewService::notifyChanged() {
  emit changed();
  notifyCurrentLine();
}

void PreviewService::notifyCurrentLine() {
  if (current_line_ != notified_line_) {
    notified_line_ = current_line_;
    emit currentLineIndexChanged();
  }
}

bool PreviewService::quickLookText() const {
  return gate_mime_ == QStringLiteral("text/plain") || gate_mime_ == QStringLiteral("application/x-zerosize") ||
         quickLookMarkdown();
}

bool PreviewService::quickLookMarkdown() const {
  if (gate_mime_ == QLatin1String("text/markdown") || gate_mime_ == QLatin1String("text/x-markdown")) {
    return true;
  }
  const auto suffix = QFileInfo(path_).suffix();
  return (gate_mime_ == QLatin1String("text/plain") || gate_mime_ == QLatin1String("application/x-zerosize")) &&
         (suffix.compare(QLatin1String("md"), Qt::CaseInsensitive) == 0 ||
          suffix.compare(QLatin1String("markdown"), Qt::CaseInsensitive) == 0);
}

QList<QUrl> PreviewService::markdownFileRoots() const {
  // QMarkdown's local-root checks use Unicode paths. Do not authorize a different
  // decoded directory when the source's native name cannot round-trip through QString.
  if (QFile::encodeName(path_) != native_path_) {
    return {};
  }
  const auto& parent = text_.canonical_parent;
  return parent.isEmpty() ? QList<QUrl>() : QList<QUrl>{QUrl::fromLocalFile(parent + QLatin1Char('/'))};
}

PreviewService::QuickLookEligibility PreviewService::quickLookEligibility() const {
  if (!has_entry_ || stat_failed_ || is_dir_) {
    return QuickLookEligibility::Unsupported;
  }
  if (busy_ && gate_mime_.isEmpty()) {
    return QuickLookEligibility::Checking;
  }
  return gate_mime_.startsWith(QStringLiteral("image/")) || quickLookText() ? QuickLookEligibility::Supported
                                                                            : QuickLookEligibility::Unsupported;
}

bool PreviewService::quickLookEligible() const { return quickLookEligibility() != QuickLookEligibility::Unsupported; }

void PreviewService::moveCurrentLine(int delta) {
  const int count = text_lines_.rowCount();
  if (current_line_ < 0 || count == 0) {
    return;
  }
  const int target = qBound(0, current_line_ + delta, count - 1);
  if (target == current_line_) {
    return;
  }
  current_line_ = target;
  retained_line_ = target;
  notifyCurrentLine();
}

PreviewService::PreviewError PreviewService::rasterError(HolonightImages::Outcome outcome) {
  using HolonightImages::Outcome;
  switch (outcome) {
    case Outcome::Success:
    case Outcome::Cancelled:
      return {};
    case Outcome::Unsupported:
      return {.kind = PreviewErrorKind::Unsupported,
              .message = tr("The image format or color profile is not supported.")};
    case Outcome::Damaged:
      return {.kind = PreviewErrorKind::DecodeFailed, .message = tr("The image is damaged or could not be decoded.")};
    case Outcome::ResourceLimit:
      return {.kind = PreviewErrorKind::ResourceLimit, .message = tr("Image exceeds the decode memory limit.")};
    case Outcome::IoFailure:
      return {.kind = PreviewErrorKind::IoFailure, .message = tr("The image could not be read.")};
  }
  Q_UNREACHABLE();
}
