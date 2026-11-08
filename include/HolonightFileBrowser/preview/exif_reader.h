#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QString>

#include <HolonightFileBrowser/preview/runtime_options.h>
#include <atomic>
#include <holonight_images/image.h>
#include <memory>
#include <optional>

// Formats shared structured EXIF facts for Files presentation.
namespace ExifReader {

struct ExifSummary {
  std::optional<HolonightImages::Outcome> outcome;
  HolonightImages::RuntimeStatus runtime = HolonightImages::RuntimeStatus::Success;
  QString diagnostic;
  bool present = false;
  QString make;
  QString model;
  QString exposure_time;
  QString iso;
  QString focal_length;
  QString lens_model;
  QString aperture;  // "f/X.X"
  bool operator==(const ExifSummary&) const = default;
};

ExifSummary format(const HolonightImages::MetadataResult& result);
ExifSummary read(
    const QString& path, const QByteArray& mimeType,
    const HolonightFileBrowser::ImageRuntimeOptions& options = HolonightFileBrowser::defaultImageRuntimeOptions());
ExifSummary read(
    QIODevice& source, const QByteArray& mimeType, const std::shared_ptr<std::atomic_bool>& cancel = {},
    const HolonightFileBrowser::ImageRuntimeOptions& options = HolonightFileBrowser::defaultImageRuntimeOptions());

}  // namespace ExifReader
