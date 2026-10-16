#include <QFileInfo>

#include <HolonightFileBrowser/places/place_availability_checker.h>

bool StatPlaceAvailabilityChecker::isAvailable(const QString& path) const {
  const QFileInfo info(path);
  return info.isDir() && info.isReadable();
}
