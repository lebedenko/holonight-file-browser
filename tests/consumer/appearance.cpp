// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Andrii L <lebeden@gmail.com>

#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QPalette>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>

#include <HolonightFileBrowser/quick.h>
#include <memory>

namespace {
bool write(const QString& path, const QByteArray& bytes) {
  QSaveFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QByteArray appearance(const QByteArray& scheme, const QByteArray& accent) {
  return "version = 2\n[theme]\nscheme = \"" + scheme + "\"\naccent = \"" + accent +
         "\"\n[icons]\ntheme = \"live-test\"\nfallback = \"live-test\"\n";
}
}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir directory;
  const auto path = directory.filePath("appearance.toml");
  if (!write(path, appearance("holonight-dark", "blue"))) return 1;
  qputenv("HOLONIGHT_APPEARANCE_FILE", path.toUtf8());
  QGuiApplication app(argc, argv);
  const auto theme = directory.filePath("live-test");
  QDir().mkpath(theme + "/32x32/places");
  if (!write(theme + "/index.theme",
             "[Icon "
             "Theme]\nName=live-test\nDirectories=32x32/places\n[32x32/"
             "places]\nSize=32\nType=Scalable\nMinSize=16\nMaxSize=256\n") ||
      !write(theme + "/32x32/places/folder.svg", R"(<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32">
<style id="current-color-scheme">.ColorScheme-Accent{color:#0000ff}.ColorScheme-Text{color:#ffffff}</style>
<rect class="ColorScheme-Accent" fill="currentColor" width="32" height="32"/>
<rect fill="#000000" fill-opacity=".35" y="16" width="32" height="16"/>
<rect class="ColorScheme-Text" fill="currentColor" x="24" y="4" width="4" height="4"/>
</svg>)"))
    return 2;
  QIcon::setThemeSearchPaths({directory.path()});
  QQmlEngine engine;
  HolonightFileBrowser::initializeEngine(engine);
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQuick
import Holonight.Core
import Holonight.FileBrowser
Item {
  width: 320; height: 110
  property int revision: HnAppearance.revision
  DirectoryListing {
    objectName: "listing"
    y: 40; width: 320; height: 70
    selectedPaths: ["/kept"]
    currentIndex: 0
    model: ListModel {
      ListElement { name: "Folder"; path: "/kept"; isDir: true; iconName: "missing/folder"; statFailed: false; statError: "" }
    }
  }
  // Requests share the same candidate chain at different physical sizes.
  Repeater {
    model: 3
    Image {
      required property int index
      x: index * 32; width: 32; height: 32
      sourceSize: Qt.size(index === 2 ? 128 : 32, index === 2 ? 128 : 32)
      source: "image://icon/missing/folder?revision=" + HnAppearance.revision
    }
  }
}
)",
                    QUrl("file:///live-appearance.qml"));
  std::unique_ptr<QObject> object(component.create());
  if (!object) {
    qCritical() << component.errors();
    return 3;
  }
  auto* item = qobject_cast<QQuickItem*>(object.get());
  QQuickWindow window;
  window.resize(320, 110);
  item->setParentItem(window.contentItem());
  window.show();
  QImage previous;
  for (const auto& selection : {qMakePair(QByteArray("holonight-dark"), QByteArray("blue")),
                                qMakePair(QByteArray("holonight-mocha"), QByteArray("default")),
                                qMakePair(QByteArray("holonight-mocha"), QByteArray("violet")),
                                qMakePair(QByteArray("holonight-light"), QByteArray("blue")),
                                qMakePair(QByteArray("holonight-dark"), QByteArray("blue")),
                                qMakePair(QByteArray("holonight-mocha"), QByteArray("violet"))}) {
    const auto revision = object->property("revision").toInt();
    if (!write(path, appearance(selection.first, selection.second))) return 4;
    if (!previous.isNull() && !QTest::qWaitFor([&] { return object->property("revision").toInt() > revision; }))
      return 5;
    QTest::qWait(150);
    const auto image = window.grabWindow();
    if (image.isNull()) return 6;
    const auto body = image.pixelColor(8, 8);
    if (body != QGuiApplication::palette().color(QPalette::Accent)) {
      qCritical() << "Stale body" << selection << body << QGuiApplication::palette().color(QPalette::Accent);
      return 7;
    }
    if (image.pixelColor(26, 6) != QGuiApplication::palette().color(QPalette::Text) ||
        image.pixelColor(8, 24).lightnessF() >= body.lightnessF())
      return 8;
    for (int x : {40, 72})
      if (image.pixelColor(x, 8) != body) return 9;
    if (!previous.isNull() && previous == image) return 10;
    auto* listing = object->findChild<QQuickItem*>("listing");
    if (!listing || listing->property("selectedPaths").toStringList() != QStringList{"/kept"} ||
        listing->property("currentIndex").toInt() != 0 || listing->property("count").toInt() != 1)
      return 14;
    bool checked = false;
    auto inspect = [&](auto&& self, QQuickItem* parent) -> void {
      for (auto* child : parent->childItems()) {
        const auto source = child->property("source").toUrl().toString();
        if (child->property("rendering").isValid() && source.startsWith("image://icon/")) {
          const auto point = child->mapToScene({child->width() / 4, child->height() / 4}).toPoint();
          checked = source.endsWith("?revision=" + object->property("revision").toString()) &&
                    image.pixelColor(point) == body;
        }
        self(self, child);
      }
    };
    inspect(inspect, listing);
    if (!checked) return 15;
    previous = image;
  }
  // Icon-only updates must reach Qt independently of palette changes.
  auto bytes = appearance("holonight-mocha", "violet");
  bytes.replace("theme = \"live-test\"", "theme = \"different-theme\"");
  if (!write(path, bytes) || !QTest::qWaitFor([] { return QIcon::themeName() == "different-theme"; })) return 11;
  QIcon::setThemeName("ExplicitApplicationTheme");
  bytes.replace("different-theme", "next-system-theme");
  bytes.replace("fallback = \"live-test\"", "fallback = \"different-fallback\"");
  if (!write(path, bytes) || !QTest::qWaitFor([] { return QIcon::fallbackThemeName() == "different-fallback"; }))
    return 12;
  if (QIcon::themeName() != "ExplicitApplicationTheme") return 13;
  item->setParentItem(nullptr);
  return 0;
}
