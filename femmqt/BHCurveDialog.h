#pragma once

#include <QDialog>
#include <QWidget>

class QTableWidget;
struct FemmMaterialProp;

// Plots a set of (B, H) points as a line/scatter chart, linear or log-log
// -- Qt-painted, not a port of femm/bhplot.cpp's CBHPlot (that class is a
// generic GDI scatter-plot widget shared with femm's own XY-plot dialog;
// this is purpose-built for just the BH curve case, which is all this
// phase needs).
class BHCurveChartWidget : public QWidget {
  Q_OBJECT

  public:
  explicit BHCurveChartWidget(QWidget* parent = nullptr);
  void setPoints(const QVector<QPair<double, double>>& points);
  void setLogScale(bool log);

  protected:
  void paintEvent(QPaintEvent* event) override;

  private:
  QVector<QPair<double, double>> m_points;
  bool m_logScale = false;
};

// Qt port of femm/BHData.h's CBHData -- edits a nonlinear material's BH
// curve (FemmMaterialProp::bhData, already read/written by FemmFileIO.cpp
// but not exposed for editing until now) via a two-column table instead
// of classic's two parallel comma-separated text fields, plus a live
// linear/log plot. "Read from file..." is simplified relative to
// classic's own CBHDatafile unit-selection dialog: assumes Tesla and A/m
// (the common case), not the full B/H unit picker.
class BHCurveDialog : public QDialog {
  Q_OBJECT

  public:
  BHCurveDialog(FemmMaterialProp& material, QWidget* parent = nullptr);

  private slots:
  void onAddPoint();
  void onRemovePoint();
  void onReadFromFile();
  void onTableChanged();
  void onAccept();

  private:
  void refreshChart();

  FemmMaterialProp& m_material;
  QTableWidget* m_table = nullptr;
  BHCurveChartWidget* m_chart = nullptr;
};
