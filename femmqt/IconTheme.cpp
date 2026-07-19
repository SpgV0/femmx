#include "IconTheme.h"

#include <QApplication>
#include <QFile>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSvgRenderer>

namespace {

QPixmap renderTinted(const QByteArray& svgData, int logicalSize, qreal devicePixelRatio)
{
  int physicalSize = qRound(logicalSize * devicePixelRatio);
  QPixmap pixmap(physicalSize, physicalSize);
  pixmap.fill(Qt::transparent);
  pixmap.setDevicePixelRatio(devicePixelRatio);

  QSvgRenderer renderer(svgData);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  renderer.render(&painter, QRectF(0, 0, logicalSize, logicalSize));
  return pixmap;
}

} // namespace

QIcon IconTheme::themedToolIcon(const QString& svgResourcePath)
{
  QFile file(svgResourcePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    return QIcon();

  // ButtonText (not WindowText) is the semantically correct palette role
  // for toolbar/tool-button glyphs -- it's the role Qt's own built-in
  // styles use for tool button icons/text, and the one that actually
  // flips between a dark and a light color when the palette switches
  // between light and dark, which is the whole point here.
  QColor color = qApp ? qApp->palette().color(QPalette::ButtonText) : QColor(Qt::black);
  QByteArray svgData = file.readAll();
  svgData.replace("CURRENTCOLOR", color.name().toUtf8());

  // Two raster sizes tagged with their real devicePixelRatio (1x/2x) so
  // QIcon can pick the sharp one on a HiDPI display instead of upscaling
  // a single low-res pixmap -- same convention as shipping @2x PNGs, just
  // generated from one vector source instead of maintaining two files.
  QIcon icon;
  icon.addPixmap(renderTinted(svgData, 24, 1.0));
  icon.addPixmap(renderTinted(svgData, 24, 2.0));
  return icon;
}
