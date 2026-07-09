// LoadMonitorDlg.h : header file
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-07-09:
// persistent, always-on CPU/GPU/RAM load monitor hosted in femm.exe
// (CMainFrame::m_LoadMonitor), toggled via the View menu
// (ID_VIEW_LOADMONITOR). Unlike the earlier fkn.exe-hosted version this
// replaces, it isn't tied to any single solve's process lifetime -- it
// keeps sampling continuously for as long as it's enabled, and the
// various analyze entry points (FemmeView.cpp, hdrawView.cpp,
// cdrawView.cpp, beladrawView.cpp) call MarkSolveStart()/MarkSolveEnd()
// to bracket each solve with a marker on the chart and a max/avg stats
// line appended to the log below it. CPU usage comes from
// GetSystemTimes(); GPU usage comes from NVIDIA's NVML, loaded
// dynamically (LoadLibrary/GetProcAddress) so this has no hard link-time
// CUDA dependency and degrades gracefully (GPU trace just stays empty)
// on non-NVIDIA machines; RAM usage comes from GlobalMemoryStatusEx.
#pragma once

#include <vector>

class CLoadMonitorDlg : public CDialog {
  public:
  CLoadMonitorDlg(CWnd* pParent = NULL);
  ~CLoadMonitorDlg();

  enum { IDD = IDD_LOADMONITOR };

  // Starts/stops sampling and shows/hides the window. The View-menu
  // toggle (CMainFrame::OnViewLoadMonitor) and this window's own [X]
  // button both funnel through here, so the menu checkmark and the
  // window's actual state never disagree.
  void Enable(BOOL bEnable);
  BOOL IsEnabled() const { return m_bEnabled; }

  // Brackets one solve with a start/end marker on the chart and a
  // max/avg/RAM summary line appended to the log once it ends. Safe to
  // call even when the monitor is disabled (no-ops). `label` identifies
  // the problem, e.g. "magnetics: motor.fem".
  void MarkSolveStart(LPCTSTR label);
  void MarkSolveEnd();

  protected:
  virtual void DoDataExchange(CDataExchange* pDX);
  virtual BOOL OnInitDialog();
  virtual void OnCancel(); // [X] / Esc -- same as toggling the View menu off
  afx_msg void OnTimer(UINT_PTR nIDEvent);
  afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);
  afx_msg void OnSavePng();
  afx_msg void OnDestroy();
  DECLARE_MESSAGE_MAP()

  private:
  static const int kMaxSamples = 240; // 240 * 250ms = 60s rolling window
  static const UINT kSampleIntervalMs = 250;
  static const int kMaxLogLines = 50; // cap the stats log's memory use

  BOOL m_bEnabled;

  std::vector<float> m_cpuHistory;
  std::vector<float> m_gpuHistory;
  std::vector<float> m_ramHistory;

  // Solve start/end marker events, tracked by absolute sample tick (not
  // array index) so they age out of the rolling window in step with the
  // history samples they refer to.
  struct Marker {
    long tick;
    BOOL bStart;
  };
  std::vector<Marker> m_markers;
  long m_totalTicks;

  // CPU sampling state (deltas between successive GetSystemTimes calls)
  ULONGLONG m_lastIdle;
  ULONGLONG m_lastTotal;
  BOOL m_haveLastCpuSample;

  // NVML dynamic loading
  HMODULE m_hNvml;
  void* m_nvmlDevice;
  BOOL m_gpuAvailable;
  // NVML uses __cdecl on Windows (see nvml.h's DECLDIR), not __stdcall.
  typedef int(__cdecl* NvmlInitFn)();
  typedef int(__cdecl* NvmlDeviceGetHandleFn)(unsigned int, void**);
  typedef int(__cdecl* NvmlDeviceGetUtilFn)(void*, void*);
  typedef int(__cdecl* NvmlShutdownFn)();
  NvmlInitFn m_nvmlInit;
  NvmlDeviceGetHandleFn m_nvmlGetHandle;
  NvmlDeviceGetUtilFn m_nvmlGetUtil;
  NvmlShutdownFn m_nvmlShutdown;

  // Per-solve incremental stats, accumulated between MarkSolveStart/End
  // so they stay correct even after the chart's rolling window has
  // scrolled past the solve's start.
  BOOL m_bSolveInProgress;
  CString m_solveLabel;
  DWORD m_solveStartTick;
  float m_solveCpuMax, m_solveCpuSum;
  float m_solveGpuMax, m_solveGpuSum;
  float m_solveRamMax, m_solveRamSum;
  int m_solveSampleCount;

  void InitCpuSampling();
  float SampleCpuPercent();
  void InitGpuSampling();
  float SampleGpuPercent();
  float SampleRamPercent();
  void DrawChart(CDC* pDC, const CRect& rect);
  BOOL SaveChartAsPng(const CRect& rect, LPCTSTR path);
};
