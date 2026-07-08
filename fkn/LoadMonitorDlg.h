// LoadMonitorDlg.h : header file
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-07-09:
// live CPU/GPU load monitor shown while fkn.exe solves. CPU usage comes
// from GetSystemTimes(); GPU usage comes from NVIDIA's NVML, loaded
// dynamically (LoadLibrary/GetProcAddress) so this has no hard link-time
// CUDA dependency and degrades gracefully (GPU trace just stays empty)
// on non-NVIDIA machines or CUDA-less builds.
// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-09:
// added OnSolveFinished(); see fkn.cpp's atexit gate, which keeps this
// window (and the process) alive after the solve completes until the
// user closes it, instead of exit() tearing it down instantly.
#pragma once

#include <vector>

class CLoadMonitorDlg : public CDialog {
  public:
  CLoadMonitorDlg(CWnd* pParent = NULL);
  ~CLoadMonitorDlg();

  enum { IDD = IDD_LOADMONITOR };

  // Called once the solve has actually finished (see fkn.cpp's atexit
  // gate) so the window's title makes it clear why fkn.exe is still
  // running: it's waiting for the user to close this window.
  void OnSolveFinished();

  protected:
  virtual void DoDataExchange(CDataExchange* pDX);
  virtual BOOL OnInitDialog();
  virtual void OnCancel(); // destroys the window (X button / Esc)
  afx_msg void OnTimer(UINT_PTR nIDEvent);
  afx_msg void OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct);
  afx_msg void OnSavePng();
  afx_msg void OnDestroy();
  DECLARE_MESSAGE_MAP()

  private:
  static const int kMaxSamples = 240; // 240 * 250ms = 60s rolling window
  static const UINT kSampleIntervalMs = 250;

  std::vector<float> m_cpuHistory;
  std::vector<float> m_gpuHistory;

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

  void InitCpuSampling();
  float SampleCpuPercent();
  void InitGpuSampling();
  float SampleGpuPercent();
  void DrawChart(CDC* pDC, const CRect& rect);
  BOOL SaveChartAsPng(const CRect& rect, LPCTSTR path);
};
