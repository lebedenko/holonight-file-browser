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
#if defined(COMPONENT_Preview) || defined(COMPONENT_PreviewQuick)
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>

#include <HolonightFileBrowser/preview/preview_service.h>
#include <sys/stat.h>
#endif
#ifdef COMPONENT_PreviewQuick
#include <QQuickItem>
#include <QQuickWindow>

#include <HolonightFileBrowser/preview_quick/thumbnail_provider.h>
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
#if defined(COMPONENT_Preview) || defined(COMPONENT_PreviewQuick)
  if (argc > 1 && !QString::fromLocal8Bit(argv[1]).isEmpty()) {
    const HolonightFileBrowser::ImageRuntimeOptions options{QString::fromLocal8Bit(argv[1])};
    QTemporaryDir directory;
    const auto path = directory.filePath("pixels.png");
    QImage pixels(32, 32, QImage::Format_RGB32);
    pixels.fill(Qt::red);
    if (!pixels.save(path)) {
      return 2;
    }
    PreviewService preview(options);
    preview.setTarget(path, false, 0, {}, S_IFREG | 0600, false, {}, {});
    QElapsedTimer clock;
    clock.start();
    while (preview.busy() && clock.elapsed() < 5000) {
      QCoreApplication::processEvents();
      QThread::msleep(1);
    }
    if (!preview.hasImage() || preview.image().pixelColor(0, 0) != QColor(Qt::red)) {
      return 3;
    }
#ifdef COMPONENT_PreviewQuick
    engine.addImageProvider("thumbnail", new ThumbnailProvider(options));
    QQmlComponent imageComponent(&engine);
    imageComponent.setData("import QtQuick; Image { width: 32; height: 32; cache: false }", {});
    std::unique_ptr<QObject> imageObject(imageComponent.create());
    auto* item = qobject_cast<QQuickItem*>(imageObject.get());
    if (!item) {
      return 4;
    }
    QQuickWindow window;
    window.resize(32, 32);
    item->setParentItem(window.contentItem());
    item->setProperty("source", "image://thumbnail/" + ThumbnailProvider::idFor(path, 128, 0));
    window.show();
    clock.restart();
    QImage delivered;
    while (clock.elapsed() < 5000) {
      QCoreApplication::processEvents();
      if (item->property("status").toInt() == 1) {
        delivered = window.grabWindow();
        if (!delivered.isNull() &&
            delivered.pixelColor(delivered.width() / 2, delivered.height() / 2) == QColor(Qt::red)) {
          break;
        }
      }
      QThread::msleep(1);
    }
    if (delivered.isNull() || delivered.pixelColor(delivered.width() / 2, delivered.height() / 2) != QColor(Qt::red)) {
      return 5;
    }
#endif
  }
#endif
  return HolonightFileBrowser::nativePath(HolonightFileBrowser::localUrl("/a")) == "/a" ? 0 : 1;
}
