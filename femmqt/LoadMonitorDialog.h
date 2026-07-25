#pragma once

#include <QWidget>
#include <QDialog>
#include <QVector>

class QLabel;
class QPlainTextEdit;
class QTimer;

// Renders the rolling CPU/GPU/RAM history + solve start/end markers --
// pure painting, no sampling of its own (LoadMonitorDialog owns the
// WinAPI/NVML calls and pushes samples in here). Split out from
// LoadMonitorDialog for the same reason femm/LoadMonitorDlg.cpp's chart is
// a separate OnDrawItem-handled child control rather than painted by the
// dialog itself: keeps the "how do I read CPU%" code and the "how do I
// draw a rolling line chart" code from tangling together.
class LoadMonitorChartWidget : public QWidget {
  Q_OBJECT

  public:
  explicit LoadMonitorChartWidget(QWidget* parent = nullptr);

  void reset();
  void pushSample(float cpu, float gpu, bool gpuAvailable, float ram);
  void addMarker(bool start);

  protected:
  void paintEvent(QPaintEvent* event) override;

  private:
  static constexpr int kMaxSamples = 4000; // 4000 * 250ms = 1000s, matches femm/LoadMonitorDlg.h's own window
  static constexpr int kSampleIntervalMs = 250;

  QVector<float> m_cpuHistory;
  QVector<float> m_gpuHistory;
  QVector<float> m_ramHistory;
  bool m_gpuAvailable = false;

  struct Marker {
    long tick;
    bool start;
  };
  QVector<Marker> m_markers;
  long m_totalTicks = 0;
};

// Qt port of femm/LoadMonitorDlg.h's CLoadMonitorDlg -- see that header's
// comment for the overall design this mirrors (CPU via GetSystemTimes,
// GPU via NVML loaded dynamically so there's no hard CUDA link dependency,
// RAM via GlobalMemoryStatusEx; only samples -- and only advances the
// chart -- while a solve is actually bracketed between markSolveStart()/
// markSolveEnd(), same as the classic dialog's own OnTimer). Owned by
// MainWindow (only the geometry editor ever triggers a solve in femmqt;
// SolutionWindow has no matching "Load Monitor" menu item, see
// MainWindow.cpp's View menu comment for why that scoping is enough).
//
// Reachability caveat specific to this Qt port: SolveRunner::solve() is
// synchronous (QProcess::waitForFinished pumping QCoreApplication::
// processEvents() in a loop, see SolveRunner.cpp) rather than classic
// FEMM's fire-and-return interactive path -- this dialog's own QTimer can
// only actually fire during that pumping, which is why that pumping loop
// exists at all now (it didn't before this feature).
class LoadMonitorDialog : public QDialog {
  Q_OBJECT

  public:
  explicit LoadMonitorDialog(QWidget* parent = nullptr);

  void setMonitoring(bool enable);
  bool isMonitoring() const { return m_enabled; }

  // Safe to call even when monitoring is disabled (no-ops), same as
  // femm/LoadMonitorDlg.h's MarkSolveStart/MarkSolveEnd.
  void markSolveStart(const QString& label);
  void markSolveEnd();

  protected:
  void closeEvent(QCloseEvent* event) override;

  private slots:
  void onTick();
  void onSavePng();

  private:
  void initCpuSampling();
  float sampleCpuPercent();
  void initGpuSampling();
  float sampleGpuPercent();
  float sampleRamPercent();
  void appendLogLines(const QString& label, double durationSec, float cpuMax, float cpuAvg,
      float gpuMax, float gpuAvg, float ramMax, float ramAvg);

  bool m_enabled = false;
  QTimer* m_timer = nullptr;
  LoadMonitorChartWidget* m_chart = nullptr;
  QLabel* m_legend = nullptr;
  QPlainTextEdit* m_log = nullptr;

  quint64 m_lastIdle = 0, m_lastTotal = 0;
  bool m_haveLastCpuSample = false;

  void* m_nvmlHandle = nullptr; // HMODULE, void* so this header doesn't need <windows.h>
  void* m_nvmlDevice = nullptr;
  bool m_gpuAvailable = false;

  bool m_solveInProgress = false;
  QString m_solveLabel;
  qint64 m_solveStartMs = 0;
  float m_solveCpuMax = 0, m_solveCpuSum = 0;
  float m_solveGpuMax = 0, m_solveGpuSum = 0;
  float m_solveRamMax = 0, m_solveRamSum = 0;
  int m_solveSampleCount = 0;
};
