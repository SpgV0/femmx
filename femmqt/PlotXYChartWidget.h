#pragma once

// PlotXYChartWidget.h
//
// A line chart of one quantity along a contour, for Plot X-Y.
//
// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-10-25
// (issue #114): generic -- any series ContourSampler produces, not just
// |A| and |B| -- with an x-axis title in the model's length unit, a
// y-axis title naming the quantity and its unit, "nice" 1/2/5 ticks on
// both axes with grid lines (it had only the two end values), and gaps
// where the contour leaves the mesh.

#include <QString>
#include <QVector>
#include <QWidget>

class PlotXYChartWidget : public QWidget {
  public:
  PlotXYChartWidget(QVector<double> x, QString xTitle, QWidget* parent = nullptr);

  // Shows one series; NaN samples are drawn as gaps.
  void setSeries(const QString& name, const QString& unit, QVector<double> y);

  // The tick positions a [lo, hi] axis gets: 1/2/5 x 10^n steps, about
  // `target` of them. Public for the test.
  static QVector<double> niceTicks(double lo, double hi, int target = 6);

  QString xTitle() const { return m_xTitle; }
  QString yTitle() const;

  QSize sizeHint() const override { return QSize(560, 320); }

  protected:
  void paintEvent(QPaintEvent* event) override;

  private:
  QVector<double> m_x;
  QVector<double> m_y;
  QString m_xTitle;
  QString m_name, m_unit;
};
