#include "PlotXYChartWidget.h"

#include <QFont>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

#include <algorithm>

PlotXYChartWidget::PlotXYChartWidget(QVector<double> arcLength, QVector<double> aMag, QVector<double> bMag, QWidget* parent)
    : QWidget(parent)
    , m_arcLength(std::move(arcLength))
    , m_aMag(std::move(aMag))
    , m_bMag(std::move(bMag))
{
  setMinimumSize(360, 260);
  // Matches the Solution Viewer's other custom-painted plots -- draw our
  // own background rather than inheriting whatever's behind this widget.
  setAutoFillBackground(true);
}

void PlotXYChartWidget::setQuantity(Quantity q)
{
  if (m_quantity == q)
    return;
  m_quantity = q;
  update();
}

void PlotXYChartWidget::setTitles(const QString& aTitle, const QString& bTitle)
{
  m_aTitle = aTitle;
  m_bTitle = bTitle;
  update();
}

void PlotXYChartWidget::paintEvent(QPaintEvent*)
{
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.fillRect(rect(), palette().color(QPalette::Base));

  QRectF full(0, 0, width(), height());
  if (m_quantity == Quantity::AMag)
    paintSubplot(painter, full, m_aMag, m_aTitle, QColor(0x4E, 0xC9, 0xB0));
  else
    paintSubplot(painter, full, m_bMag, m_bTitle, QColor(0x00, 0x7A, 0xCC));
}

void PlotXYChartWidget::paintSubplot(QPainter& painter, const QRectF& rect, const QVector<double>& y, const QString& title, const QColor& color) const
{
  painter.save();

  QColor textColor = palette().color(QPalette::Text);
  QColor gridColor = palette().color(QPalette::Mid);

  QFont titleFont = painter.font();
  titleFont.setBold(true);
  painter.setFont(titleFont);
  painter.setPen(textColor);
  painter.drawText(QRectF(rect.left(), rect.top(), rect.width(), 18), Qt::AlignLeft | Qt::AlignVCenter, title);

  // Reserve room around the actual plot area for axis tick labels.
  const qreal leftMargin = 56, rightMargin = 12, topMargin = 22, bottomMargin = 22;
  QRectF plotArea(rect.left() + leftMargin, rect.top() + topMargin,
      rect.width() - leftMargin - rightMargin, rect.height() - topMargin - bottomMargin);

  painter.setPen(gridColor);
  painter.drawRect(plotArea);

  if (m_arcLength.size() < 2 || y.size() != m_arcLength.size()) {
    painter.restore();
    return;
  }

  double xMin = m_arcLength.first();
  double xMax = m_arcLength.last();
  double yMin = *std::min_element(y.begin(), y.end());
  double yMax = *std::max_element(y.begin(), y.end());
  if (yMax <= yMin)
    yMax = yMin + 1.0; // degenerate (flat) data -- avoid a zero-range mapping
  double xRange = (xMax > xMin) ? (xMax - xMin) : 1.0;
  double yRange = yMax - yMin;

  auto toPixel = [&](double xv, double yv) {
    double px = plotArea.left() + (xv - xMin) / xRange * plotArea.width();
    double py = plotArea.bottom() - (yv - yMin) / yRange * plotArea.height();
    return QPointF(px, py);
  };

  QFont tickFont = painter.font();
  tickFont.setBold(false);
  tickFont.setPointSizeF(tickFont.pointSizeF() * 0.85);
  painter.setFont(tickFont);

  // Y-axis: min/mid/max gridlines + tick labels.
  for (int i = 0; i <= 2; i++) {
    double v = yMin + yRange * i / 2.0;
    QPointF p = toPixel(xMin, v);
    painter.setPen(QPen(gridColor, 1, Qt::DotLine));
    painter.drawLine(QPointF(plotArea.left(), p.y()), QPointF(plotArea.right(), p.y()));
    painter.setPen(textColor);
    painter.drawText(QRectF(rect.left(), p.y() - 8, leftMargin - 6, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(v, 'g', 3));
  }

  // X-axis: min/max labels at the corners.
  painter.setPen(textColor);
  painter.drawText(QRectF(plotArea.left(), plotArea.bottom() + 2, 80, 16), Qt::AlignLeft, QString::number(xMin, 'g', 3));
  painter.drawText(QRectF(plotArea.right() - 80, plotArea.bottom() + 2, 80, 16), Qt::AlignRight, QString::number(xMax, 'g', 3));

  // The actual curve.
  QPainterPath path;
  path.moveTo(toPixel(m_arcLength.first(), y.first()));
  for (int i = 1; i < m_arcLength.size(); i++)
    path.lineTo(toPixel(m_arcLength[i], y[i]));
  QPen curvePen(color);
  curvePen.setWidthF(2.0);
  curvePen.setCosmetic(true);
  painter.setPen(curvePen);
  painter.setClipRect(plotArea);
  painter.drawPath(path);

  painter.restore();
}
