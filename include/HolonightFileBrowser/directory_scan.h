#pragma once

#include <QList>

#include <HolonightFileBrowser/directory_entry.h>
#include <HolonightFileBrowser/places/place_list.h>
#include <atomic>
#include <functional>
#include <memory>

namespace HolonightFileBrowser {
using PublishBatch = std::function<void(QList<DirectoryEntry>, bool, QString)>;
// Supported worker interface used by DirectoryReader and application models. Batches contain at
// most 250 entries; callbacks execute on the calling worker and may apply delivery backpressure.
// Cancellation suppresses callbacks but cannot interrupt a blocked filesystem call. Consumers
// dispatch on a worker, reject stale generations and retire it asynchronously (see DirectoryReader).
// beforeOpen/readErrorAfter are injectable filesystem failure seams; production uses {}/-1.
void walkDirectory(const QString& path, const std::shared_ptr<const std::atomic_bool>& cancel,
                   const std::function<void()>& beforeOpen, int readErrorAfter,
                   const std::shared_ptr<const PlaceList::IconMap>& places, const PublishBatch& publish,
                   const QByteArray& nativePath = {});
DirectoryEntry syntheticParentEntry(const QString& path, const PlaceList::IconMap* places = nullptr,
                                    const QByteArray& nativeParent = {});
}  // namespace HolonightFileBrowser
