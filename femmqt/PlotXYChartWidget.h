#pragma once

#include <QColor>
#include <QString>
#include <QVector>
#include <QWidget>

// Simple, dependency-free line-chart widget for the Solution Viewer's
// "Plot X-Y along Contour" dialog -- no charting library is linked into
// this target (see femmqt/CMakeLists.txt), so this draws its own axes/
// gridlines/curve directly via QPainter rather than pulling in Qt Charts,
// matching how the Density/Contour plots are already custom-painted rather
// than using a charting widget.
//
// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-26: per
// direct user request ("select what to plot, only show one plot based on
// the selection") -- was always two stacked subplots (|A| and |B| both at
// once); now holds both datasets but paints only the currently selected
// one, full-height, via setQuantity(). The two-stacked-subplots approach
// this replaced was itself a deliberate choice (the two quantities' units
// and typical magnitudes differ enough that sharing one y-axis would
// flatten one curve) -- showing one full-size plot at a time keeps that
// same benefit while giving the requested single-plot-at-a-time view.
class PlotXYChartWidget : public QWidget {
  public:
  enum class Quantity { AMag, BMag };

  PlotXYChartWidget(QVector<double> arcLength, QVector<double> aMag, QVector<double> bMag, QWidget* parent = nullptr);

  void setQuantity(Quantity q);
  Quantity quantity() const { return m_quantity; }

  QSize sizeHint() const override { return QSize(560, 320); }

  protected:
  void paintEvent(QPaintEvent* event) override;

  private:
  void paintSubplot(QPainter& painter, const QRectF& rect, const QVector<double>& y, const QString& title, const QColor& color) const;

  QVector<double> m_arcLength;
  QVector<double> m_aMag;
  QVector<double> m_bMag;
  Quantity m_quantity = Quantity::AMag;
};
