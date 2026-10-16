#pragma once
#include <QList>
#include <QSet>
#include <QString>

#include <atomic>
namespace HolonightFileBrowser {
struct FolderSearchEntry {
  QByteArray path;
  QString name;
  bool directory;
};
QSet<QByteArray> matchFolder(const QList<FolderSearchEntry>& records, const QString& query,
                             const std::atomic_bool& cancelled);
}  // namespace HolonightFileBrowser
