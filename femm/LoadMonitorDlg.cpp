// LoadMonitorDlg.cpp : implementation file
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-07-09:
// see LoadMonitorDlg.h for the overview.
#include "stdafx.h"
#include "femm.h"
#include "LoadMonitorDlg.h"
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

namespace {

// Mirrors NVML's public ABI struct (nvmlUtilization_t) without requiring
// the CUDA Toolkit's headers at build time -- this file only ever talks
// to nvml.dll via GetProcAddress.
struct NvmlUtilization {
  unsigned int gpu;
  unsigned int memory;
};

ULONGLONG FileTimeToU64(const FILETIME& ft) {
  ULARGE_INTEGER u;
  u.LowPart = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  return u.QuadPart;
}

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
  UINT num = 0, size = 0;
  Gdiplus::GetImageEncodersSize(&num, &size);
  if (size == 0)
    return -1;

  Gdiplus::ImageCodecInfo* pImageCodecInfo = (Gdiplus::ImageCodecInfo*)malloc(size);
  if (pImageCodecInfo == NULL)
    return -1;

  Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);

  for (UINT j = 0; j < num; j++) {
    if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
      *pClsid = pImageCodecInfo[j].Clsid;
      free(pImageCodecInfo);
      return j;
    }
  }
  free(pImageCodecInfo);
  return -1;
}

}  // namespace

BEGIN_MESSAGE_MAP(CLoadMonitorDlg, CDialog)
ON_WM_TIMER()
ON_WM_DESTROY()
ON_WM_DRAWITEM()
ON_BN_CLICKED(IDC_SAVEPNG, OnSavePng)
END_MESSAGE_MAP()

CLoadMonitorDlg::CLoadMonitorDlg(CWnd* pParent)
    : CDialog(CLoadMonitorDlg::IDD, pParent) {
  m_bEnabled = FALSE;
  m_lastIdle = 0;
  m_lastTotal = 0;
  m_haveLastCpuSample = FALSE;
  m_hNvml = NULL;
  m_nvmlDevice = NULL;
  m_gpuAvailable = FALSE;
  m_nvmlInit = NULL;
  m_nvmlGetHandle = NULL;
  m_nvmlGetUtil = NULL;
  m_nvmlShutdown = NULL;
  m_totalTicks = 0;
  m_bSolveInProgress = FALSE;
  m_solveStartTick = 0;
  m_solveCpuMax = m_solveCpuSum = 0.0f;
  m_solveGpuMax = m_solveGpuSum = 0.0f;
  m_solveRamMax = m_solveRamSum = 0.0f;
  m_solveSampleCount = 0;
}

CLoadMonitorDlg::~CLoadMonitorDlg() {
  if (m_hNvml != NULL) {
    if (m_nvmlShutdown != NULL)
      m_nvmlShutdown();
    FreeLibrary(m_hNvml);
  }
}

void CLoadMonitorDlg::DoDataExchange(CDataExchange* pDX) {
  CDialog::DoDataExchange(pDX);
}

BOOL CLoadMonitorDlg::OnInitDialog() {
  CDialog::OnInitDialog();

  m_cpuHistory.reserve(kMaxSamples);
  m_gpuHistory.reserve(kMaxSamples);
  m_ramHistory.reserve(kMaxSamples);

  InitCpuSampling();
  InitGpuSampling();

  CString legend;
  legend.Format("CPU (blue)   RAM (purple)%s   |   green/red markers: solve start/end",
      m_gpuAvailable ? "   GPU (orange)" : "   GPU: not available");
  SetDlgItemText(IDC_LOADLEGEND, legend);

  // Whoever creates this dialog (CMainFrame::OnCreate) calls Enable()
  // explicitly based on the current View-menu toggle state -- don't
  // start sampling here unconditionally.
  return TRUE;
}

void CLoadMonitorDlg::OnCancel() {
  Enable(FALSE);
}

void CLoadMonitorDlg::OnDestroy() {
  KillTimer(1);
  CDialog::OnDestroy();
}

void CLoadMonitorDlg::Enable(BOOL bEnable) {
  if (!::IsWindow(m_hWnd) || bEnable == m_bEnabled)
    return;
  m_bEnabled = bEnable;
  if (bEnable) {
    InitCpuSampling(); // reset the delta baseline so the first sample isn't stale
    ShowWindow(SW_SHOW);
    SetTimer(1, kSampleIntervalMs, NULL);
  } else {
    KillTimer(1);
    ShowWindow(SW_HIDE);
  }
}

void CLoadMonitorDlg::InitCpuSampling() {
  FILETIME idleTime, kernelTime, userTime;
  if (GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
    m_lastIdle = FileTimeToU64(idleTime);
    m_lastTotal = FileTimeToU64(kernelTime) + FileTimeToU64(userTime);
    m_haveLastCpuSample = TRUE;
  }
}

float CLoadMonitorDlg::SampleCpuPercent() {
  FILETIME idleTime, kernelTime, userTime;
  if (!GetSystemTimes(&idleTime, &kernelTime, &userTime))
    return 0.0f;

  ULONGLONG idle = FileTimeToU64(idleTime);
  ULONGLONG total = FileTimeToU64(kernelTime) + FileTimeToU64(userTime);

  float percent = 0.0f;
  if (m_haveLastCpuSample) {
    ULONGLONG dIdle = idle - m_lastIdle;
    ULONGLONG dTotal = total - m_lastTotal;
    if (dTotal > 0)
      percent = (float)(100.0 * (double)(dTotal - dIdle) / (double)dTotal);
  }

  m_lastIdle = idle;
  m_lastTotal = total;
  m_haveLastCpuSample = TRUE;

  if (percent < 0.0f)
    percent = 0.0f;
  if (percent > 100.0f)
    percent = 100.0f;
  return percent;
}

void CLoadMonitorDlg::InitGpuSampling() {
  // Try the driver's default search path first, then NVSMI's install dir
  // (where the driver installer typically also drops nvml.dll).
  m_hNvml = LoadLibraryA("nvml.dll");
  if (m_hNvml == NULL) {
    char path[MAX_PATH];
    ExpandEnvironmentStringsA(
        "%ProgramFiles%\\NVIDIA Corporation\\NVSMI\\nvml.dll", path, MAX_PATH);
    m_hNvml = LoadLibraryA(path);
  }
  if (m_hNvml == NULL)
    return;

  m_nvmlInit = (NvmlInitFn)GetProcAddress(m_hNvml, "nvmlInit_v2");
  m_nvmlGetHandle = (NvmlDeviceGetHandleFn)GetProcAddress(m_hNvml, "nvmlDeviceGetHandleByIndex_v2");
  m_nvmlGetUtil = (NvmlDeviceGetUtilFn)GetProcAddress(m_hNvml, "nvmlDeviceGetUtilizationRates");
  m_nvmlShutdown = (NvmlShutdownFn)GetProcAddress(m_hNvml, "nvmlShutdown");

  if (m_nvmlInit == NULL || m_nvmlGetHandle == NULL || m_nvmlGetUtil == NULL) {
    FreeLibrary(m_hNvml);
    m_hNvml = NULL;
    return;
  }

  if (m_nvmlInit() != 0) {
    FreeLibrary(m_hNvml);
    m_hNvml = NULL;
    return;
  }

  if (m_nvmlGetHandle(0, &m_nvmlDevice) != 0) {
    if (m_nvmlShutdown != NULL)
      m_nvmlShutdown();
    FreeLibrary(m_hNvml);
    m_hNvml = NULL;
    return;
  }

  m_gpuAvailable = TRUE;
}

float CLoadMonitorDlg::SampleGpuPercent() {
  if (!m_gpuAvailable)
    return 0.0f;

  NvmlUtilization util = { 0, 0 };
  if (m_nvmlGetUtil(m_nvmlDevice, &util) != 0)
    return 0.0f;

  return (float)util.gpu;
}

float CLoadMonitorDlg::SampleRamPercent() {
  MEMORYSTATUSEX statex;
  statex.dwLength = sizeof(statex);
  if (!GlobalMemoryStatusEx(&statex))
    return 0.0f;
  return (float)statex.dwMemoryLoad; // already a 0-100 system-wide percentage
}

void CLoadMonitorDlg::MarkSolveStart(LPCTSTR label) {
  if (!m_bEnabled || !::IsWindow(m_hWnd))
    return;
  m_bSolveInProgress = TRUE;
  m_solveLabel = label;
  m_solveStartTick = GetTickCount();
  m_solveCpuMax = m_solveCpuSum = 0.0f;
  m_solveGpuMax = m_solveGpuSum = 0.0f;
  m_solveRamMax = m_solveRamSum = 0.0f;
  m_solveSampleCount = 0;

  // The chart only ever shows the solve currently (or most recently)
  // running -- wipe it clean so this solve starts from an empty graph
  // instead of picking up wherever the last one (or idle time) left off.
  m_cpuHistory.clear();
  m_gpuHistory.clear();
  m_ramHistory.clear();
  m_markers.clear();
  m_totalTicks = 0;
  InitCpuSampling(); // reset the delta baseline so the first sample isn't stale

  Marker mk;
  mk.tick = m_totalTicks;
  mk.bStart = TRUE;
  m_markers.push_back(mk);

  CWnd* pChart = GetDlgItem(IDC_LOADCHART);
  if (pChart != NULL)
    pChart->Invalidate(FALSE);
}

void CLoadMonitorDlg::MarkSolveEnd() {
  if (!m_bEnabled || !::IsWindow(m_hWnd) || !m_bSolveInProgress)
    return;
  m_bSolveInProgress = FALSE;

  Marker mk;
  mk.tick = m_totalTicks;
  mk.bStart = FALSE;
  m_markers.push_back(mk);

  // No more OnTimer samples will arrive now that m_bSolveInProgress is
  // FALSE, so the chart freezes here -- make sure this final frame
  // (including the end marker) actually gets drawn.
  CWnd* pChart = GetDlgItem(IDC_LOADCHART);
  if (pChart != NULL)
    pChart->Invalidate(FALSE);

  double durationSec = (GetTickCount() - m_solveStartTick) / 1000.0;
  float cpuAvg = m_solveSampleCount > 0 ? m_solveCpuSum / m_solveSampleCount : 0.0f;
  float gpuAvg = m_solveSampleCount > 0 ? m_solveGpuSum / m_solveSampleCount : 0.0f;
  float ramAvg = m_solveSampleCount > 0 ? m_solveRamSum / m_solveSampleCount : 0.0f;

  CTime now = CTime::GetCurrentTime();

  // One short line per stat instead of a single long one, so nothing gets
  // clipped in the listbox regardless of the dialog's width.
  CString lines[6];
  int nLines = 0;
  lines[nLines++].Format("%s  %s", now.Format("%H:%M:%S"), (LPCTSTR)m_solveLabel);
  lines[nLines++].Format("  Time: %.1fs", durationSec);
  lines[nLines++].Format("  CPU: %.0f%% max / %.0f%% avg", m_solveCpuMax, cpuAvg);
  if (m_gpuAvailable)
    lines[nLines++].Format("  GPU: %.0f%% max / %.0f%% avg", m_solveGpuMax, gpuAvg);
  lines[nLines++].Format("  RAM: %.0f%% max / %.0f%% avg", m_solveRamMax, ramAvg);
  lines[nLines++] = ""; // blank separator before the next (older) entry

  CListBox* pLog = (CListBox*)GetDlgItem(IDC_LOADLOG);
  if (pLog != NULL) {
    for (int i = 0; i < nLines; i++)
      pLog->InsertString(i, lines[i]); // newest group on top, in order
    while (pLog->GetCount() > kMaxLogLines)
      pLog->DeleteString(pLog->GetCount() - 1);
  }
}

void CLoadMonitorDlg::OnTimer(UINT_PTR nIDEvent) {
  if (nIDEvent == 1) {
    // Only sample -- and thus only advance/redraw the chart -- while a
    // solve is actually running. Once it ends the chart simply stops
    // getting new points and freezes on whatever it last showed, until
    // the next MarkSolveStart() clears it again.
    if (m_bSolveInProgress) {
      float cpu = SampleCpuPercent();
      float gpu = SampleGpuPercent();
      float ram = SampleRamPercent();

      if ((int)m_cpuHistory.size() >= kMaxSamples)
        m_cpuHistory.erase(m_cpuHistory.begin());
      if ((int)m_gpuHistory.size() >= kMaxSamples)
        m_gpuHistory.erase(m_gpuHistory.begin());
      if ((int)m_ramHistory.size() >= kMaxSamples)
        m_ramHistory.erase(m_ramHistory.begin());
      m_cpuHistory.push_back(cpu);
      m_gpuHistory.push_back(gpu);
      m_ramHistory.push_back(ram);
      m_totalTicks++;

      // prune markers that have scrolled out of the rolling window
      long oldestVisible = m_totalTicks - (long)m_cpuHistory.size();
      while (!m_markers.empty() && m_markers.front().tick < oldestVisible)
        m_markers.erase(m_markers.begin());

      m_solveCpuSum += cpu;
      if (cpu > m_solveCpuMax)
        m_solveCpuMax = cpu;
      m_solveGpuSum += gpu;
      if (gpu > m_solveGpuMax)
        m_solveGpuMax = gpu;
      m_solveRamSum += ram;
      if (ram > m_solveRamMax)
        m_solveRamMax = ram;
      m_solveSampleCount++;

      CWnd* pChart = GetDlgItem(IDC_LOADCHART);
      if (pChart != NULL)
        pChart->Invalidate(FALSE);
    }
  }
  CDialog::OnTimer(nIDEvent);
}

void CLoadMonitorDlg::OnDrawItem(int nIDCtl, LPDRAWITEMSTRUCT lpDrawItemStruct) {
  if (nIDCtl == IDC_LOADCHART) {
    CDC dc;
    dc.Attach(lpDrawItemStruct->hDC);
    CRect rect(lpDrawItemStruct->rcItem);
    DrawChart(&dc, rect);
    dc.Detach();
    return;
  }
  CDialog::OnDrawItem(nIDCtl, lpDrawItemStruct);
}

void CLoadMonitorDlg::DrawChart(CDC* pDC, const CRect& rect) {
  pDC->FillSolidRect(rect, RGB(255, 255, 255));

  // Reserve a strip along the bottom for the time axis labels so they
  // don't overlap the plotted data or the 0% gridline.
  const int kXAxisHeight = 14;
  CRect plotRect(rect.left, rect.top, rect.right, rect.bottom - kXAxisHeight);

  pDC->SetBkMode(TRANSPARENT);

  CPen axisPen(PS_SOLID, 1, RGB(200, 200, 200));
  CPen* pOldPen = pDC->SelectObject(&axisPen);
  for (int pct = 0; pct <= 100; pct += 25) {
    int y = plotRect.bottom - (int)((plotRect.Height() - 1) * (pct / 100.0));
    pDC->MoveTo(plotRect.left, y);
    pDC->LineTo(plotRect.right, y);
    CString label;
    label.Format("%d%%", pct);
    pDC->TextOut(plotRect.left + 2, y - 12, label);
  }
  pDC->SelectObject(pOldPen);

  // The x axis autoscales to whatever's actually been sampled so far,
  // stretched to fill the available width, rather than always spanning
  // the full 60s rolling-window capacity -- otherwise a short solve
  // would be squeezed into an unreadable sliver on the left.
  int historySize = (int)m_cpuHistory.size();
  int xDenom = historySize > 1 ? historySize - 1 : 1;
  long oldestVisible = m_totalTicks - (long)historySize;

  // Solve start/end markers, drawn before the trace lines so the traces
  // stay legible on top.
  for (size_t i = 0; i < m_markers.size(); i++) {
    const Marker& mk = m_markers[i];
    if (mk.tick < oldestVisible)
      continue;
    long relPos = mk.tick - oldestVisible;
    int x = plotRect.left + (int)((plotRect.Width() - 1) * ((double)relPos / xDenom));
    CPen markerPen(PS_SOLID, 1, mk.bStart ? RGB(0, 150, 0) : RGB(200, 0, 0));
    CPen* pOldMarker = pDC->SelectObject(&markerPen);
    pDC->MoveTo(x, plotRect.top);
    pDC->LineTo(x, plotRect.bottom);
    pDC->SelectObject(pOldMarker);
  }

  auto drawSeries = [&](const std::vector<float>& hist, COLORREF color) {
    if (hist.size() < 2)
      return;
    CPen pen(PS_SOLID, 2, color);
    CPen* pOld = pDC->SelectObject(&pen);
    int n = (int)hist.size();
    for (int i = 1; i < n; i++) {
      int x0 = plotRect.left + (int)((plotRect.Width() - 1) * ((double)(i - 1) / xDenom));
      int x1 = plotRect.left + (int)((plotRect.Width() - 1) * ((double)i / xDenom));
      int y0 = plotRect.bottom - (int)((plotRect.Height() - 1) * (hist[i - 1] / 100.0));
      int y1 = plotRect.bottom - (int)((plotRect.Height() - 1) * (hist[i] / 100.0));
      pDC->MoveTo(x0, y0);
      pDC->LineTo(x1, y1);
    }
    pDC->SelectObject(pOld);
  };

  drawSeries(m_cpuHistory, RGB(30, 90, 220));
  if (m_gpuAvailable)
    drawSeries(m_gpuHistory, RGB(230, 120, 20));
  drawSeries(m_ramHistory, RGB(150, 60, 200));

  CPen borderPen(PS_SOLID, 1, RGB(120, 120, 120));
  pOldPen = pDC->SelectObject(&borderPen);
  pDC->SelectStockObject(NULL_BRUSH);
  pDC->Rectangle(plotRect);
  pDC->SelectObject(pOldPen);

  // Time axis labels, using the same autoscaled mapping as the traces
  // above. Sub-second solves get one decimal place so the labels aren't
  // all just "0s".
  double totalSpanSeconds = xDenom * kSampleIntervalMs / 1000.0;
  LPCTSTR secFmt = totalSpanSeconds < 10.0 ? "%.1fs" : "%.0fs";
  int nLabels = historySize > 1 ? 5 : 1;
  for (int i = 0; i < nLabels; i++) {
    double frac = nLabels > 1 ? (double)i / (nLabels - 1) : 0.0;
    int x = plotRect.left + (int)((plotRect.Width() - 1) * frac);
    long tick = oldestVisible + (long)(xDenom * frac);
    double seconds = tick * kSampleIntervalMs / 1000.0;
    if (seconds < 0)
      seconds = 0;
    CString label;
    label.Format(secFmt, seconds);
    int textX = (i == nLabels - 1 && nLabels > 1) ? x - 22 : x + 2; // keep the last label from running off the right edge
    pDC->TextOut(textX, plotRect.bottom + 1, label);
  }
}

BOOL CLoadMonitorDlg::SaveChartAsPng(const CRect& rect, LPCTSTR path) {
  CWnd* pChart = GetDlgItem(IDC_LOADCHART);
  if (pChart == NULL)
    return FALSE;

  CClientDC screenDC(pChart);
  CDC memDC;
  memDC.CreateCompatibleDC(&screenDC);
  CBitmap bmp;
  bmp.CreateCompatibleBitmap(&screenDC, rect.Width(), rect.Height());
  CBitmap* pOldBmp = memDC.SelectObject(&bmp);

  DrawChart(&memDC, CRect(0, 0, rect.Width(), rect.Height()));

  ULONG_PTR gdiplusToken;
  Gdiplus::GdiplusStartupInput gdiplusStartupInput;
  Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

  BOOL ok = FALSE;
  {
    Gdiplus::Bitmap gpBitmap((HBITMAP)bmp.GetSafeHandle(), NULL);
    CLSID pngClsid;
    if (GetEncoderClsid(L"image/png", &pngClsid) >= 0) {
      // Manual ANSI->wide conversion (this is a plain-MFC project, not
      // ATL, so CT2CW isn't reliably available without extra headers).
      int wlen = MultiByteToWideChar(CP_ACP, 0, path, -1, NULL, 0);
      std::vector<WCHAR> wPath(wlen > 0 ? wlen : 1);
      if (wlen > 0)
        MultiByteToWideChar(CP_ACP, 0, path, -1, &wPath[0], wlen);
      ok = (gpBitmap.Save(&wPath[0], &pngClsid, NULL) == Gdiplus::Ok);
    }
  }

  Gdiplus::GdiplusShutdown(gdiplusToken);

  memDC.SelectObject(pOldBmp);
  return ok;
}

void CLoadMonitorDlg::OnSavePng() {
  CFileDialog dlg(FALSE, "png", "load_monitor.png",
      OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT,
      "PNG Image (*.png)|*.png|All Files (*.*)|*.*||");
  if (dlg.DoModal() != IDOK)
    return;

  CWnd* pChart = GetDlgItem(IDC_LOADCHART);
  CRect rect;
  pChart->GetClientRect(&rect);

  if (SaveChartAsPng(rect, dlg.GetPathName()))
    MessageBox("Chart saved.", "CPU / GPU / RAM Load", MB_OK | MB_ICONINFORMATION);
  else
    MessageBox("Could not save the chart as PNG.", "CPU / GPU / RAM Load", MB_OK | MB_ICONERROR);
}
