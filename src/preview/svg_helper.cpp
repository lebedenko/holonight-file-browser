#include <QPainter>
#include <QSvgRenderer>

#include <HolonightFileBrowser/preview/image_limits.h>
#include <HolonightFileBrowser/preview/svg_helper.h>

QSizeF svgIntrinsicSize(const QSvgRenderer& renderer) {
  return HolonightImages::svgDocumentSize(renderer.defaultSize(), renderer.viewBoxF());
}

HolonightImages::SvgInspection inspectViewerSvg(const QByteArray& bytes, const QString& path, QSvgRenderer& renderer,
                                                const std::atomic_bool& cancelled) {
  auto inspection = HolonightImages::inspectSvg(bytes, cancelled);
  if (!cancelled.load() && inspection.outcome == HolonightImages::Outcome::Unsupported &&
      inspection.resourceReason == HolonightImages::SvgResourceReason::LocalImageReference) {
    renderer.setOptions(QtSvg::DisableAnimations);
    if (renderer.load(path)) {
      inspection.facts.documentSize = svgIntrinsicSize(renderer);
    }
  }
  return inspection;
}

QImage renderLocalSvg(QSvgRenderer& renderer, QSizeF intrinsicSize, QSize bound, const std::atomic_bool& cancelled) {
  if (cancelled.load() || !renderer.isValid()) {
    return {};
  }
  const auto size = HolonightImages::svgPixelSize(intrinsicSize, bound);
  if (size.isEmpty() || static_cast<qint64>(size.width()) * size.height() > kImageLimitBytes / 4) {
    return {};
  }
  QImage image(size, QImage::Format_ARGB32_Premultiplied);
  if (image.isNull()) {
    return {};
  }
  image.fill(Qt::transparent);
  QPainter painter(&image);
  renderer.render(&painter, image.rect());
  painter.end();
  return cancelled.load() ? QImage{} : image;
}
