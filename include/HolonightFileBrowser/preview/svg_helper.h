#pragma once

#include <QImage>
class QSvgRenderer;

#include <atomic>
#include <holonight_images/svg.h>

QSizeF svgIntrinsicSize(const QSvgRenderer& renderer);
// Only the provider's exclusively local-image fallback may load by filename.
HolonightImages::SvgInspection inspectViewerSvg(const QByteArray& bytes, const QString& path, QSvgRenderer& renderer,
                                                const std::atomic_bool& cancelled);
QImage renderLocalSvg(QSvgRenderer& renderer, QSizeF intrinsicSize, QSize bound, const std::atomic_bool& cancelled);
