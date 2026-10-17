#pragma once

#include <QtQml/qqmlregistration.h>

#include <HolonightFileBrowser/directory_proxy_model.h>
#include <HolonightFileBrowser/directory_reader.h>

struct BrowserReaderRegistration {
  Q_GADGET
  QML_FOREIGN(HolonightFileBrowser::DirectoryReader)
  QML_NAMED_ELEMENT(DirectoryReader)
};
struct BrowserProxyRegistration {
  Q_GADGET
  QML_FOREIGN(DirectoryProxyModel)
  QML_NAMED_ELEMENT(DirectorySortModel)
};

#include <HolonightFileBrowser/breadcrumb_adapter.h>
struct BreadcrumbAdapterRegistration {
  Q_GADGET
  QML_FOREIGN(HolonightFileBrowser::BreadcrumbAdapter)
  QML_NAMED_ELEMENT(BreadcrumbAdapter)
};
