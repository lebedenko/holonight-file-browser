#include <QObject>

#include <HolonightFileBrowser/preview/runtime_options.h>
namespace HolonightFileBrowser {
ImageRuntimeOptions defaultImageRuntimeOptions() { return {QString::fromUtf8(HOLONIGHT_FILE_BROWSER_IMAGE_RUNTIME)}; }
QString imageRuntimeError(HolonightImages::RuntimeStatus status) {
  using HolonightImages::RuntimeStatus;
  switch (status) {
    case RuntimeStatus::Success:
      return {};
    case RuntimeStatus::Unavailable:
      return QObject::tr("The image processing runtime is unavailable.");
    case RuntimeStatus::ProtocolFailure:
      return QObject::tr("The image processing runtime failed.");
    case RuntimeStatus::CleanupFailure:
      return QObject::tr("Image processing cleanup could not be verified.");
    case RuntimeStatus::Timeout:
      return QObject::tr("Image processing timed out.");
    case RuntimeStatus::Cancelled:
      return {};
  }
  Q_UNREACHABLE();
}
}  // namespace HolonightFileBrowser
