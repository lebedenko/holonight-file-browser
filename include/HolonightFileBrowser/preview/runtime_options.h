#pragma once
#include <QString>

#include <holonight_images/isolated.h>
namespace HolonightFileBrowser {
// An explicit bundle selection, copied before worker dispatch. No discovery.
struct ImageRuntimeOptions {
  QString directory;
};
ImageRuntimeOptions defaultImageRuntimeOptions();
QString imageRuntimeError(HolonightImages::RuntimeStatus status);
}  // namespace HolonightFileBrowser
