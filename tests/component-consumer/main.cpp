#include <HolonightFileBrowser/native_path.h>
#if defined(COMPONENT_PreviewQuick) || defined(COMPONENT_Markdown)
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#else
#include <QCoreApplication>
#endif
#ifdef COMPONENT_Search
#include <HolonightFileBrowser/search/folder_search.h>
#endif
#ifdef COMPONENT_Storage
#include <HolonightFileBrowser/storage/capacity_probe.h>
#endif
#ifdef COMPONENT_Preview
#include <HolonightFileBrowser/preview/text_lines.h>
#endif
int main(int argc, char** argv) {
#if defined(COMPONENT_PreviewQuick) || defined(COMPONENT_Markdown)
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent component(&engine);
#ifdef COMPONENT_Markdown
  component.setData("import Holonight.FileBrowser.Markdown; MarkdownDocument { markdown: \"hello\" }", {});
#else
  component.setData(
      "import QtQuick; import Holonight.FileBrowser.Preview; Item { TextPreview {} FilePreview { service: null } }",
      {});
#endif
  auto* object = component.create();
  if (!object) {
    qWarning() << component.errors();
    return 1;
  }
  delete object;
#else
  QCoreApplication app(argc, argv);
#ifdef COMPONENT_Preview
  if (TextLines::splitLines(QStringLiteral("one\ntwo")).size() != 2) {
    return 1;
  }
#endif
#ifdef COMPONENT_Search
  std::atomic_bool cancel{false};
  if (HolonightFileBrowser::matchFolder({{"/a", "alpha", false}}, "alpha", cancel).size() != 1) {
    return 1;
  }
#endif
#ifdef COMPONENT_Storage
  StorageInfoCapacityProbe probe;
  if (!probe.measure("/").valid) {
    return 1;
  }
#endif
#endif
  return HolonightFileBrowser::nativePath(HolonightFileBrowser::localUrl("/a")) == "/a" ? 0 : 1;
}
