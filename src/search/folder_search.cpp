#include <HolonightFileBrowser/search/folder_search.h>
#include <holonight/search/index.h>
namespace HolonightFileBrowser {
QSet<QByteArray> matchFolder(const QList<FolderSearchEntry>& records, const QString& query,
                             const std::atomic_bool& cancelled) {
  HolonightSearch::Index index;
  QVector<HolonightSearch::Record> values;
  for (const auto& entry : records) {
    if (cancelled.load()) {
      return {};
    }
    values.append({.id = QString::fromLatin1(entry.path.toHex()),
                   .fields = {{.name = "name", .text = entry.name, .weight = 1}},
                   .boost = 0});
  }
  index.replace("folder", std::move(values));
  HolonightSearch::Options options;
  options.limit = static_cast<int>(records.size());
  options.profile = HolonightSearch::Profile::Path;
  options.cancelled = &cancelled;
  QSet<QByteArray> result;
  for (const auto& match : index.search(query, options)) {
    result.insert(QByteArray::fromHex(match.id.toLatin1()));
  }
  return cancelled.load() ? QSet<QByteArray>{} : result;
}
}  // namespace HolonightFileBrowser
