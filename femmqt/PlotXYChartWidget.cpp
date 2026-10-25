#include "PlotXYChartWidget.h"

#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

#include <algorithm>
#include <cmath>

PlotXYChartWidget::PlotXYChartWidget(QVector<double> x, QString xTitle, QWidget* parent)
    : QWidget(parent)
    , m_x(std::move(x))
    , m_xTitle(std::move(xTitle))
{
  setMinimumSize(360, 260);
  setAutoFillBackground(true);
}

void PlotXYChartWidget::setSeries(const QString& name, const QString& unit, QVector<double> y)
{
  m_name = name;
  m_unit = unit;
  m_y = std::move(y);
  update();
}

QString PlotXYChartWidget::yTitle() const
{
  return m_unit.isEmpty() ? m_name : QStringLiteral("%1 (%2)").arg(m_name, m_unit);
}

QVector<double> PlotXYChartWidget::niceTicks(double lo, double hi, int target)
{
  QVector<double> ticks;
  if (!(hi > lo) || target < 1)
    return ticks;
  const double raw = (hi - lo) / target;
  const double mag = std::pow(10., std::floor(std::log10(raw)));
  double step = mag;
  for (double m : { 1., 2., 5., 10. }) {
    step = m * mag;
    if (step >= raw)
      break;
  }
  // Integer multiples of the step, not a running sum, which drifts.
  const long long k0 = (long long)std::ceil(lo / step - 1e-9);
  const long long k1 = (long long)std::floor(hi / step + 1e-9);
  for (long long k = k0; k <= k1; k++)
    ticks << k * step;
  return ticks;
}

void PlotXYChartWidget::paintEvent(QPaintEvent*)
{
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.fillRect(rect(), palette().color(QPalette::Base));
  const QColor text = palette().color(QPalette::Text);
  const QColor grid = palette().color(QPalette::Mid);

  QFont font = painter.font();
  const QFontMetrics fm(font);
  const qreal left = 64 + fm.height(), right = 14, top = 12, bottom = 28 + fm.height();
  const QRectF area(left, top, width() - left - right, height() - top - bottom);
  painter.setPen(grid);
  painter.drawRect(area);

  // Axis titles: what the curve is, and what it is plotted against.
  painter.setPen(text);
  painter.drawText(QRectF(area.left(), height() - fm.height() - 4, area.width(), fm.height()),
      Qt::AlignHCenter | Qt::AlignVCenter, m_xTitle);
  painter.save();
  painter.translate(fm.height() / 2. + 4, area.center().y());
  painter.rotate(-90);
  painter.drawText(QRectF(-area.height() / 2, -fm.height() / 2., area.height(), fm.height()),
      Qt::AlignCenter, yTitle());
  painter.restore();

  double yMin = 0, yMax = 0;
  bool any = false;
  for (double v : m_y) {
    if (std::isnan(v))
      continue;
    yMin = any ? std::min(yMin, v) : v;
    yMax = any ? std::max(yMax, v) : v;
    any = true;
  }
  if (m_x.size() < 2 || m_y.size() != m_x.size() || !any)
    return;
  if (yMax - yMin <= 1e-12 * std::max(std::abs(yMax), 1e-300)) {
    const double pad = std::abs(yMax) > 0 ? 0.05 * std::abs(yMax) : 1.;
    yMin -= pad;
    yMax += pad;
  }
  const double xMin = m_x.first(), xMax = m_x.last() > m_x.first() ? m_x.last() : m_x.first() + 1.;
  auto px = [&](double x) { return area.left() + (x - xMin) / (xMax - xMin) * area.width(); };
  auto py = [&](double y) { return area.bottom() - (y - yMin) / (yMax - yMin) * area.height(); };

  QFont tickFont = font;
  tickFont.setPointSizeF(font.pointSizeF() * 0.85);
  painter.setFont(tickFont);
  for (double t : niceTicks(yMin, yMax)) {
    painter.setPen(QPen(grid, 1, Qt::DotLine));
    painter.drawLine(QPointF(area.left(), py(t)), QPointF(area.right(), py(t)));
    painter.setPen(text);
    painter.drawText(QRectF(fm.height() + 6, py(t) - 8, area.left() - fm.height() - 10, 16), Qt::AlignRight | Qt::AlignVCenter,
        QString::number(t, 'g', 4));
  }
  for (double t : niceTicks(xMin, xMax)) {
    painter.setPen(QPen(grid, 1, Qt::DotLine));
    painter.drawLine(QPointF(px(t), area.top()), QPointF(px(t), area.bottom()));
    painter.setPen(text);
    painter.drawText(QRectF(px(t) - 40, area.bottom() + 2, 80, 16), Qt::AlignHCenter | Qt::AlignTop, QString::number(t, 'g', 4));
  }

  QPainterPath path;
  bool pen = false;
  for (int i = 0; i < m_x.size(); i++) {
    if (std::isnan(m_y[i])) {
      pen = false; // the contour left the mesh: a gap, not a line to zero
      continue;
    }
    const QPointF p(px(m_x[i]), py(m_y[i]));
    if (pen)
      path.lineTo(p);
    else
      path.moveTo(p);
    pen = true;
  }
  QPen curve(QColor(0x00, 0x7A, 0xCC));
  curve.setWidthF(2.0);
  curve.setCosmetic(true);
  painter.setClipRect(area);
  painter.setPen(curve);
  painter.drawPath(path);
}
