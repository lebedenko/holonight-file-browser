#pragma once

#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QString>

#include <HolonightFileBrowser/preview/thumbnail_size.h>
#include <list>
#include <optional>

struct ThumbnailSource {
  qint64 size = -1;
  QDateTime modified;
  bool operator==(const ThumbnailSource&) const = default;
};

struct ThumbnailKey {
  QString path;
  int box_pixels = 0;
  int generation = 0;
  QByteArray native_path = {};
  bool operator==(const ThumbnailKey&) const = default;
};

size_t qHash(const ThumbnailKey& key, size_t seed = 0);

// The owner serializes access. The front is the most recently used entry.
class ThumbnailCache {
 public:
  explicit ThumbnailCache(qsizetype maxEntries = kThumbnailCacheEntries, qint64 maxBytes = kThumbnailCacheBytes);
  ~ThumbnailCache() = default;
  ThumbnailCache(const ThumbnailCache&) = delete;
  ThumbnailCache& operator=(const ThumbnailCache&) = delete;
  ThumbnailCache(ThumbnailCache&&) = delete;
  ThumbnailCache& operator=(ThumbnailCache&&) = delete;
  // Promotes a hit to most recent.
  std::optional<QImage> find(const ThumbnailKey& key, const ThumbnailSource& source = {});
  // Replaces an existing entry, then evicts least recently used entries until both limits hold. An image
  // larger than the byte limit on its own is therefore not kept.
  void insert(const ThumbnailKey& key, QImage image, const ThumbnailSource& source = {});
  [[nodiscard]] qsizetype count() const { return static_cast<qsizetype>(entries_.size()); }
  [[nodiscard]] qint64 bytes() const { return bytes_; }

 private:
  struct Entry {
    ThumbnailKey key;
    QImage image;
    ThumbnailSource source;
  };
  void erase(std::list<Entry>::iterator entry);

  std::list<Entry> entries_;
  QHash<ThumbnailKey, std::list<Entry>::iterator> index_;
  qsizetype maxEntries_;
  qint64 maxBytes_;
  qint64 bytes_ = 0;
};
