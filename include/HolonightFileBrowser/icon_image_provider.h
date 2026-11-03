#pragma once

#include <QCache>
#include <QPixmap>
#include <QQuickImageProvider>
#include <QString>
#include <QStringList>

// Serves image://icon/<name1>/<name2>/... (SPEC.md REQ-F-002).
// Optionally followed by ?revision=<appearance-revision>; query metadata is not an icon name.
// The chain is produced by IconNameResolver and carried by DirectoryModel::IconNameRole. Candidates
// use the shared semantic engine in order, with Qt's unthemed fallback (REQ-F-001). A total miss
// returns a null pixmap so the consuming HnIcon reports hasError and QML shows the bundled glyph
// (REQ-F-016). Pixmap-type (not Image, not async): Qt Quick only guarantees GUI-thread calls for
// Pixmap providers, which QIconLoader requires (REQ-NF-001) — so no locking is needed here.
class IconImageProvider final : public QQuickImageProvider {
 public:
  IconImageProvider();
  QPixmap requestPixmap(const QString& chain, QSize* size, const QSize& requestedSize) override;

 private:
  friend struct IconImageProviderTestAccess;
  // Key "<name>@<w>x<h>" (REQ-F-020). Misses are cached as null pixmaps too, so a name the theme
  // lacks costs one theme lookup per pixel size, not one per row.
  QCache<QString, QPixmap> cache_;
  QStringList appearance_context_;
  int theme_lookup_count_for_test_ = 0;
};
