#include <HolonightFileBrowser/preview/thumbnail_cache.h>
#include <utility>

size_t qHash(const ThumbnailKey& key, size_t seed) {
  return qHashMulti(seed, key.path, key.box_pixels, key.generation, key.native_path);
}

ThumbnailCache::ThumbnailCache(qsizetype maxEntries, qint64 maxBytes) : maxEntries_(maxEntries), maxBytes_(maxBytes) {}

std::optional<QImage> ThumbnailCache::find(const ThumbnailKey& key, const ThumbnailSource& source) {
  const auto found = index_.constFind(key);
  if (found == index_.constEnd()) {
    return std::nullopt;
  }
  if (found.value()->source != source) {
    erase(found.value());
    return std::nullopt;
  }
  entries_.splice(entries_.begin(), entries_, found.value());
  return entries_.front().image;
}

void ThumbnailCache::insert(const ThumbnailKey& key, QImage image, const ThumbnailSource& source) {
  if (const auto existing = index_.constFind(key); existing != index_.constEnd()) {
    erase(existing.value());
  }
  bytes_ += image.sizeInBytes();
  entries_.push_front(Entry{.key = key, .image = std::move(image), .source = source});
  index_.insert(key, entries_.begin());
  while (!entries_.empty() && (count() > maxEntries_ || bytes_ > maxBytes_)) {
    erase(std::prev(entries_.end()));
  }
}

void ThumbnailCache::erase(std::list<Entry>::iterator entry) {
  bytes_ -= entry->image.sizeInBytes();
  index_.remove(entry->key);
  entries_.erase(entry);
}
