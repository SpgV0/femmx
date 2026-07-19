#include "LoadMonitorDialog.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#ifndef NOMINMAX
#define NOMINMAX // windows.h's max()/min() macros otherwise clobber std::max/std::min below
#endif
#include <windows.h>

namespace {
// Mirrors NVML's public ABI struct (nvmlUtilization_t), same reasoning as
// femm/LoadMonitorDlg.cpp's own anonymous-namespace copy: avoids requiring
// the CUDA Toolkit's headers at build time since this only ever talks to
// nvml.dll via GetProcAddress.
struct NvmlUtilization {
  unsigned int gpu;
  unsigned int memory;
};

quint64 fileTimeToU64(const FILETIME& ft)
{
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  return u.QuadPart;
}

typedef int(__cdecl* NvmlInitFn)();
typedef int(__cdecl* NvmlDeviceGetHandleFn)(unsigned int, void**);
typedef int(__cdecl* NvmlDeviceGetUtilFn)(void*, void*);
typedef int(__cdecl* NvmlShutdownFn)();
} // namespace

LoadMonitorChartWidget::LoadMonitorChartWidget(QWidget* parent)
    : QWidget(parent)
{
  setMinimumHeight(160);
}

void LoadMonitorChartWidget::reset()
{
  m_cpuHistory.clear();
  m_gpuHistory.clear();
  m_ramHistory.clear();
  m_markers.clear();
  m_totalTicks = 0;
  update();
}

void LoadMonitorChartWidget::pushSample(float cpu, float gpu, bool gpuAvailable, float ram)
{
  m_gpuAvailable = gpuAvailable;
  if (m_cpuHistory.size() >= kMaxSamples)
    m_cpuHistory.removeFirst();
  if (m_gpuHistory.size() >= kMaxSamples)
    m_gpuHistory.removeFirst();
  if (m_ramHistory.size() >= kMaxSamples)
    m_ramHistory.removeFirst();
  m_cpuHistory.push_back(cpu);
  m_gpuHistory.push_back(gpu);
  m_ramHistory.push_back(ram);
  m_totalTicks++;

  long oldestVisible = m_totalTicks - (long)m_cpuHistory.size();
  while (!m_markers.isEmpty() && m_markers.first().tick < oldestVisible)
    m_markers.removeFirst();

  update();
}

void LoadMonitorChartWidget::addMarker(bool start)
{
  m_markers.push_back({ m_totalTicks, start });
  update();
}

void LoadMonitorChartWidget::paintEvent(QPaintEvent*)
{
  QPainter painter(this);
  painter.fillRect(rect(), Qt::white);

  const int xAxisHeight = 14;
  QRect plotRect(0, 0, width(), height() - xAxisHeight);

  painter.setPen(QPen(QColor(200, 200, 200)));
  for (int pct = 0; pct <= 100; pct += 25) {
    int y = plotRect.bottom() - (int)((plotRect.height() - 1) * (pct / 100.0));
    painter.drawLine(plotRect.left(), y, plotRect.right(), y);
    painter.drawText(plotRect.left() + 2, y - 2, QString("%1%").arg(pct));
  }

  int historySize = m_cpuHistory.size();
  int xDenom = historySize > 1 ? historySize - 1 : 1;
  long oldestVisible = m_totalTicks - (long)historySize;

  for (const Marker& mk : m_markers) {
    if (mk.tick < oldestVisible)
      continue;
    long relPos = mk.tick - oldestVisible;
    int x = plotRect.left() + (int)((plotRect.width() - 1) * ((double)relPos / xDenom));
    painter.setPen(QPen(mk.start ? QColor(0, 150, 0) : QColor(200, 0, 0)));
    painter.drawLine(x, plotRect.top(), x, plotRect.bottom());
  }

  auto drawSeries = [&](const QVector<float>& hist, QColor color) {
    if (hist.size() < 2)
      return;
    painter.setPen(QPen(color, 2));
    int n = hist.size();
    for (int i = 1; i < n; i++) {
      double x0 = plotRect.left() + (plotRect.width() - 1) * ((double)(i - 1) / xDenom);
      double x1 = plotRect.left() + (plotRect.width() - 1) * ((double)i / xDenom);
      double y0 = plotRect.bottom() - (plotRect.height() - 1) * (hist[i - 1] / 100.0);
      double y1 = plotRect.bottom() - (plotRect.height() - 1) * (hist[i] / 100.0);
      painter.drawLine(QPointF(x0, y0), QPointF(x1, y1));
    }
  };
  drawSeries(m_cpuHistory, QColor(30, 90, 220));
  if (m_gpuAvailable)
    drawSeries(m_gpuHistory, QColor(230, 120, 20));
  drawSeries(m_ramHistory, QColor(150, 60, 200));

  painter.setPen(QPen(QColor(120, 120, 120)));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(plotRect.adjusted(0, 0, -1, -1));

  double totalSpanSeconds = xDenom * kSampleIntervalMs / 1000.0;
  int nLabels = historySize > 1 ? 5 : 1;
  for (int i = 0; i < nLabels; i++) {
    double frac = nLabels > 1 ? (double)i / (nLabels - 1) : 0.0;
    int x = plotRect.left() + (int)((plotRect.width() - 1) * frac);
    long tick = oldestVisible + (long)(xDenom * frac);
    double seconds = std::max(0.0, tick * kSampleIntervalMs / 1000.0);
    QString label = totalSpanSeconds < 10.0 ? QString("%1s").arg(seconds, 0, 'f', 1) : QString("%1s").arg(seconds, 0, 'f', 0);
    int textX = (i == nLabels - 1 && nLabels > 1) ? x - 22 : x + 2;
    painter.drawText(textX, plotRect.bottom() + 12, label);
  }
}

LoadMonitorDialog::LoadMonitorDialog(QWidget* parent)
    : QDialog(parent)
{
  setWindowTitle("CPU / GPU / RAM Load");
  resize(520, 360);
  // Non-modal, stays around across multiple solves -- closing it (the [X]
  // button, routed through closeEvent below) just disables monitoring,
  // matching femm/LoadMonitorDlg.cpp's OnCancel.
  setWindowFlag(Qt::WindowContextHelpButtonHint, false);

  initGpuSampling();

  auto* layout = new QVBoxLayout(this);
  m_legend = new QLabel(this);
  m_legend->setText(QString("CPU (blue)   RAM (purple)%1   |   green/red markers: solve start/end")
                         .arg(m_gpuAvailable ? "   GPU (orange)" : "   GPU: not available"));
  layout->addWidget(m_legend);

  m_chart = new LoadMonitorChartWidget(this);
  layout->addWidget(m_chart, 1);

  m_log = new QPlainTextEdit(this);
  m_log->setReadOnly(true);
  m_log->setMaximumBlockCount(1200); // ~200 solves worth of 6-line groups, matches femm/LoadMonitorDlg.h's kMaxLogLines intent
  m_log->setFixedHeight(120);
  layout->addWidget(m_log);

  auto* saveButton = new QPushButton("Save Chart as PNG...", this);
  connect(saveButton, &QPushButton::clicked, this, &LoadMonitorDialog::onSavePng);
  layout->addWidget(saveButton);

  m_timer = new QTimer(this);
  connect(m_timer, &QTimer::timeout, this, &LoadMonitorDialog::onTick);
}

void LoadMonitorDialog::closeEvent(QCloseEvent* event)
{
  setMonitoring(false);
  QDialog::closeEvent(event);
}

void LoadMonitorDialog::setMonitoring(bool enable)
{
  if (enable == m_enabled)
    return;
  m_enabled = enable;
  if (enable) {
    initCpuSampling();
    show();
    m_timer->start(250);
  } else {
    m_timer->stop();
    hide();
    m_solveInProgress = false;
  }
}

void LoadMonitorDialog::initCpuSampling()
{
  FILETIME idleTime, kernelTime, userTime;
  if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
    m_lastIdle = fileTimeToU64(idleTime);
    m_lastTotal = fileTimeToU64(kernelTime) + fileTimeToU64(userTime);
    m_haveLastCpuSample = true;
  }
}

float LoadMonitorDialog::sampleCpuPercent()
{
  FILETIME idleTime, kernelTime, userTime;
  if (!GetSystemTimes(&idleTime, &kernelTime, &userTime))
    return 0.0f;

  quint64 idle = fileTimeToU64(idleTime);
  quint64 total = fileTimeToU64(kernelTime) + fileTimeToU64(userTime);

  float percent = 0.0f;
  if (m_haveLastCpuSample) {
    quint64 dIdle = idle - m_lastIdle;
    quint64 dTotal = total - m_lastTotal;
    if (dTotal > 0)
      percent = (float)(100.0 * (double)(dTotal - dIdle) / (double)dTotal);
  }
  m_lastIdle = idle;
  m_lastTotal = total;
  m_haveLastCpuSample = true;
  return qBound(0.0f, percent, 100.0f);
}

void LoadMonitorDialog::initGpuSampling()
{
  HMODULE hNvml = LoadLibraryA("nvml.dll");
  if (hNvml == nullptr) {
    char path[MAX_PATH];
    ExpandEnvironmentStringsA("%ProgramFiles%\\NVIDIA Corporation\\NVSMI\\nvml.dll", path, MAX_PATH);
    hNvml = LoadLibraryA(path);
  }
  if (hNvml == nullptr)
    return;

  auto nvmlInit = (NvmlInitFn)GetProcAddress(hNvml, "nvmlInit_v2");
  auto nvmlGetHandle = (NvmlDeviceGetHandleFn)GetProcAddress(hNvml, "nvmlDeviceGetHandleByIndex_v2");
  auto nvmlGetUtil = (NvmlDeviceGetUtilFn)GetProcAddress(hNvml, "nvmlDeviceGetUtilizationRates");

  if (!nvmlInit || !nvmlGetHandle || !nvmlGetUtil || nvmlInit() != 0) {
    FreeLibrary(hNvml);
    return;
  }
  void* device = nullptr;
  if (nvmlGetHandle(0, &device) != 0) {
    FreeLibrary(hNvml);
    return;
  }
  m_nvmlHandle = hNvml;
  m_nvmlDevice = device;
  m_gpuAvailable = true;
}

float LoadMonitorDialog::sampleGpuPercent()
{
  if (!m_gpuAvailable || !m_nvmlHandle)
    return 0.0f;
  auto nvmlGetUtil = (NvmlDeviceGetUtilFn)GetProcAddress((HMODULE)m_nvmlHandle, "nvmlDeviceGetUtilizationRates");
  if (!nvmlGetUtil)
    return 0.0f;
  NvmlUtilization util{ 0, 0 };
  if (nvmlGetUtil(m_nvmlDevice, &util) != 0)
    return 0.0f;
  return (float)util.gpu;
}

float LoadMonitorDialog::sampleRamPercent()
{
  MEMORYSTATUSEX statex;
  statex.dwLength = sizeof(statex);
  if (!GlobalMemoryStatusEx(&statex))
    return 0.0f;
  return (float)statex.dwMemoryLoad;
}

void LoadMonitorDialog::markSolveStart(const QString& label)
{
  if (!m_enabled)
    return;
  m_solveInProgress = true;
  m_solveLabel = label;
  m_solveStartMs = QDateTime::currentMSecsSinceEpoch();
  m_solveCpuMax = m_solveCpuSum = 0.0f;
  m_solveGpuMax = m_solveGpuSum = 0.0f;
  m_solveRamMax = m_solveRamSum = 0.0f;
  m_solveSampleCount = 0;

  m_chart->reset(); // matches femm/LoadMonitorDlg.cpp: each solve starts from a blank chart
  initCpuSampling();
  m_chart->addMarker(true);
}

void LoadMonitorDialog::markSolveEnd()
{
  if (!m_enabled || !m_solveInProgress)
    return;
  m_solveInProgress = false;
  m_chart->addMarker(false);

  double durationSec = (QDateTime::currentMSecsSinceEpoch() - m_solveStartMs) / 1000.0;
  float cpuAvg = m_solveSampleCount > 0 ? m_solveCpuSum / m_solveSampleCount : 0.0f;
  float gpuAvg = m_solveSampleCount > 0 ? m_solveGpuSum / m_solveSampleCount : 0.0f;
  float ramAvg = m_solveSampleCount > 0 ? m_solveRamSum / m_solveSampleCount : 0.0f;
  appendLogLines(m_solveLabel, durationSec, m_solveCpuMax, cpuAvg, m_solveGpuMax, gpuAvg, m_solveRamMax, ramAvg);
}

void LoadMonitorDialog::appendLogLines(const QString& label, double durationSec, float cpuMax, float cpuAvg,
    float gpuMax, float gpuAvg, float ramMax, float ramAvg)
{
  QStringList lines;
  lines << QString("%1  %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss")).arg(label);
  lines << QString("  Time: %1s").arg(durationSec, 0, 'f', 1);
  lines << QString("  CPU: %1%% max / %2%% avg").arg(cpuMax, 0, 'f', 0).arg(cpuAvg, 0, 'f', 0);
  if (m_gpuAvailable)
    lines << QString("  GPU: %1%% max / %2%% avg").arg(gpuMax, 0, 'f', 0).arg(gpuAvg, 0, 'f', 0);
  lines << QString("  RAM: %1%% max / %2%% avg").arg(ramMax, 0, 'f', 0).arg(ramAvg, 0, 'f', 0);
  lines << QString();

  // Newest group on top, matching femm/LoadMonitorDlg.cpp's InsertString(i, ...).
  QString existing = m_log->toPlainText();
  m_log->setPlainText(lines.join("\n") + (existing.isEmpty() ? QString() : "\n" + existing));
  m_log->moveCursor(QTextCursor::Start);
}

void LoadMonitorDialog::onTick()
{
  if (!m_solveInProgress)
    return;
  float cpu = sampleCpuPercent();
  float gpu = sampleGpuPercent();
  float ram = sampleRamPercent();
  m_chart->pushSample(cpu, gpu, m_gpuAvailable, ram);

  m_solveCpuSum += cpu;
  m_solveCpuMax = std::max(m_solveCpuMax, cpu);
  m_solveGpuSum += gpu;
  m_solveGpuMax = std::max(m_solveGpuMax, gpu);
  m_solveRamSum += ram;
  m_solveRamMax = std::max(m_solveRamMax, ram);
  m_solveSampleCount++;
}

void LoadMonitorDialog::onSavePng()
{
  QString path = QFileDialog::getSaveFileName(this, "Save Chart", "load_monitor.png", "PNG Image (*.png)");
  if (path.isEmpty())
    return;
  QPixmap pixmap = m_chart->grab();
  if (pixmap.save(path, "PNG"))
    QMessageBox::information(this, "CPU / GPU / RAM Load", "Chart saved.");
  else
    QMessageBox::warning(this, "CPU / GPU / RAM Load", "Could not save the chart as PNG.");
}
