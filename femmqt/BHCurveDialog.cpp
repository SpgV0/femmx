#include "BHCurveDialog.h"

#include "FemmProblem.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTableWidget>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

BHCurveChartWidget::BHCurveChartWidget(QWidget* parent)
    : QWidget(parent)
{
  setMinimumHeight(220);
}

void BHCurveChartWidget::setPoints(const QVector<QPair<double, double>>& points)
{
  m_points = points;
  update();
}

void BHCurveChartWidget::setLogScale(bool log)
{
  m_logScale = log;
  update();
}

void BHCurveChartWidget::paintEvent(QPaintEvent*)
{
  QPainter painter(this);
  painter.fillRect(rect(), palette().color(QPalette::Base));

  QVector<QPair<double, double>> pts;
  for (const auto& p : m_points) {
    if (m_logScale && (p.first <= 0 || p.second <= 0))
      continue; // log scale can't plot non-positive values -- silently skipped, matches femm/bhplot.cpp's own logscale handling
    pts.push_back(p);
  }
  if (pts.size() < 2) {
    painter.drawText(rect(), Qt::AlignCenter, "Not enough points to plot (need at least 2).");
    return;
  }

  double hMin = pts[0].second, hMax = pts[0].second;
  double bMin = pts[0].first, bMax = pts[0].first;
  for (const auto& p : pts) {
    hMin = std::min(hMin, p.second);
    hMax = std::max(hMax, p.second);
    bMin = std::min(bMin, p.first);
    bMax = std::max(bMax, p.first);
  }
  if (m_logScale) {
    hMin = std::log10(hMin);
    hMax = std::log10(hMax);
    bMin = std::log10(bMin);
    bMax = std::log10(bMax);
  }
  if (hMax <= hMin)
    hMax = hMin + 1;
  if (bMax <= bMin)
    bMax = bMin + 1;

  const int margin = 40;
  QRect plotRect(margin, 10, width() - margin - 10, height() - margin - 10);

  painter.setPen(QPen(palette().color(QPalette::Mid)));
  painter.drawRect(plotRect);

  auto toScreen = [&](double h, double b) {
    double hv = m_logScale ? std::log10(h) : h;
    double bv = m_logScale ? std::log10(b) : b;
    double x = plotRect.left() + (hv - hMin) / (hMax - hMin) * plotRect.width();
    double y = plotRect.bottom() - (bv - bMin) / (bMax - bMin) * plotRect.height();
    return QPointF(x, y);
  };

  painter.setPen(QPen(palette().color(QPalette::WindowText)));
  painter.drawText(QRect(0, height() - margin + 4, width(), margin - 4), Qt::AlignHCenter, m_logScale ? "H (A/m, log)" : "H (A/m)");
  painter.save();
  painter.translate(12, height() / 2);
  painter.rotate(-90);
  painter.drawText(QRect(-height() / 2, -14, height(), 14), Qt::AlignHCenter, m_logScale ? "B (T, log)" : "B (T)");
  painter.restore();

  painter.setPen(QPen(QColor(30, 90, 220), 2));
  QPointF prev = toScreen(pts[0].second, pts[0].first);
  for (int i = 1; i < pts.size(); i++) {
    QPointF cur = toScreen(pts[i].second, pts[i].first);
    painter.drawLine(prev, cur);
    prev = cur;
  }
  painter.setBrush(QColor(30, 90, 220));
  for (const auto& p : pts) {
    QPointF pt = toScreen(p.second, p.first);
    painter.drawEllipse(pt, 2.5, 2.5);
  }
}

BHCurveDialog::BHCurveDialog(FemmMaterialProp& material, QWidget* parent)
    : QDialog(parent)
    , m_material(material)
{
  setWindowTitle(QString("BH Curve -- %1").arg(material.name));
  resize(520, 560);

  auto* layout = new QVBoxLayout(this);

  m_table = new QTableWidget(0, 2, this);
  m_table->setHorizontalHeaderLabels({ "B (Tesla)", "H (A/m)" });
  m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  for (const auto& pt : material.bhData) {
    int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(QString::number(pt.first, 'g', 10)));
    m_table->setItem(row, 1, new QTableWidgetItem(QString::number(pt.second, 'g', 10)));
  }
  connect(m_table, &QTableWidget::itemChanged, this, &BHCurveDialog::onTableChanged);
  layout->addWidget(m_table, 1);

  auto* rowButtons = new QVBoxLayout;
  auto* rowButtonsH = new QHBoxLayout;
  auto* addButton = new QPushButton("Add Point", this);
  connect(addButton, &QPushButton::clicked, this, &BHCurveDialog::onAddPoint);
  auto* removeButton = new QPushButton("Remove Selected", this);
  connect(removeButton, &QPushButton::clicked, this, &BHCurveDialog::onRemovePoint);
  auto* readButton = new QPushButton("Read from File...", this);
  connect(readButton, &QPushButton::clicked, this, &BHCurveDialog::onReadFromFile);
  rowButtonsH->addWidget(addButton);
  rowButtonsH->addWidget(removeButton);
  rowButtonsH->addWidget(readButton);
  rowButtons->addLayout(rowButtonsH);
  layout->addLayout(rowButtons);

  m_chart = new BHCurveChartWidget(this);
  layout->addWidget(m_chart, 1);

  auto* logCheck = new QCheckBox("Log-log scale", this);
  connect(logCheck, &QCheckBox::toggled, m_chart, &BHCurveChartWidget::setLogScale);
  layout->addWidget(logCheck);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &BHCurveDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);

  refreshChart();
}

void BHCurveDialog::onAddPoint()
{
  int row = m_table->rowCount();
  m_table->insertRow(row);
  m_table->setItem(row, 0, new QTableWidgetItem("0"));
  m_table->setItem(row, 1, new QTableWidgetItem("0"));
}

void BHCurveDialog::onRemovePoint()
{
  QList<QTableWidgetItem*> selected = m_table->selectedItems();
  QSet<int> rows;
  for (auto* item : selected)
    rows.insert(item->row());
  QList<int> sorted = rows.values();
  std::sort(sorted.begin(), sorted.end(), std::greater<int>());
  for (int r : sorted)
    m_table->removeRow(r);
  refreshChart();
}

void BHCurveDialog::onReadFromFile()
{
  QString path = QFileDialog::getOpenFileName(this, "Read BH Curve",
      QString(), "Two column text data file (*.dat);;All Files (*.*)");
  if (path.isEmpty())
    return;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QMessageBox::warning(this, "Read BH Curve", "Couldn't open that file.");
    return;
  }

  // Simplified relative to classic FEMM's own CBHDatafile dialog (which
  // offers a B/H unit picker -- Tesla/Gauss/kGauss, A/m/kA/m/Oersted/
  // kOersted) -- assumes Tesla and A/m, the common case.
  QTextStream in(&file);
  int added = 0;
  while (!in.atEnd()) {
    QStringList fields = in.readLine().trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (fields.size() < 2)
      continue;
    bool ok1 = false, ok2 = false;
    double b = fields[0].toDouble(&ok1);
    double h = fields[1].toDouble(&ok2);
    if (!ok1 || !ok2)
      continue;
    int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, 0, new QTableWidgetItem(QString::number(b, 'g', 10)));
    m_table->setItem(row, 1, new QTableWidgetItem(QString::number(h, 'g', 10)));
    added++;
  }
  if (added == 0)
    QMessageBox::warning(this, "Read BH Curve", "No valid \"B H\" pairs found in that file.");
  refreshChart();
}

void BHCurveDialog::onTableChanged()
{
  refreshChart();
}

void BHCurveDialog::refreshChart()
{
  QVector<QPair<double, double>> points;
  for (int r = 0; r < m_table->rowCount(); r++) {
    auto* bItem = m_table->item(r, 0);
    auto* hItem = m_table->item(r, 1);
    if (!bItem || !hItem)
      continue;
    points.push_back({ bItem->text().toDouble(), hItem->text().toDouble() });
  }
  std::sort(points.begin(), points.end(), [](const QPair<double, double>& a, const QPair<double, double>& b) {
    return a.second < b.second; // sort by H, matching a proper BH curve's monotonic H axis
  });
  m_chart->setPoints(points);
}

void BHCurveDialog::onAccept()
{
  QVector<QPair<double, double>> points;
  for (int r = 0; r < m_table->rowCount(); r++) {
    auto* bItem = m_table->item(r, 0);
    auto* hItem = m_table->item(r, 1);
    if (!bItem || !hItem)
      continue;
    points.push_back({ bItem->text().toDouble(), hItem->text().toDouble() });
  }
  std::sort(points.begin(), points.end(), [](const QPair<double, double>& a, const QPair<double, double>& b) {
    return a.second < b.second;
  });
  m_material.bhData = points;
  accept();
}
