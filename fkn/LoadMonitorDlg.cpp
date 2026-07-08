// LoadMonitorDlg.cpp : implementation file
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-07-09:
// see LoadMonitorDlg.h for the overview.
#include "stdafx.h"
#include "fkn.h"
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
// to nvml.dll via GetProcAddress, so it builds and runs the same whether
// or not fkn was built with ENABLE_CUDA_SOLVER.
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

  InitCpuSampling();
  InitGpuSampling();

  CString legend;
  legend.Format("CPU (blue)%s", m_gpuAvailable ? "   GPU (orange)" : "   GPU: not available");
  SetDlgItemText(IDC_LOADLEGEND, legend);

  SetTimer(1, kSampleIntervalMs, NULL);

  return TRUE;
}

void CLoadMonitorDlg::OnCancel() {
  // The [X] button / Esc should just hide the monitor, not tear it down
  // mid-solve -- the owning process exits shortly after solving finishes
  // anyway, which is what actually closes this window in the normal case.
  ShowWindow(SW_HIDE);
}

void CLoadMonitorDlg::OnDestroy() {
  KillTimer(1);
  CDialog::OnDestroy();
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

void CLoadMonitorDlg::OnTimer(UINT_PTR nIDEvent) {
  if (nIDEvent == 1) {
    float cpu = SampleCpuPercent();
    float gpu = SampleGpuPercent();

    if ((int)m_cpuHistory.size() >= kMaxSamples)
      m_cpuHistory.erase(m_cpuHistory.begin());
    if ((int)m_gpuHistory.size() >= kMaxSamples)
      m_gpuHistory.erase(m_gpuHistory.begin());
    m_cpuHistory.push_back(cpu);
    m_gpuHistory.push_back(gpu);

    CWnd* pChart = GetDlgItem(IDC_LOADCHART);
    if (pChart != NULL)
      pChart->Invalidate(FALSE);
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

  CPen axisPen(PS_SOLID, 1, RGB(200, 200, 200));
  CPen* pOldPen = pDC->SelectObject(&axisPen);
  for (int pct = 0; pct <= 100; pct += 25) {
    int y = rect.bottom - (int)((rect.Height() - 1) * (pct / 100.0));
    pDC->MoveTo(rect.left, y);
    pDC->LineTo(rect.right, y);
    CString label;
    label.Format("%d%%", pct);
    pDC->SetBkMode(TRANSPARENT);
    pDC->TextOut(rect.left + 2, y - 12, label);
  }
  pDC->SelectObject(pOldPen);

  auto drawSeries = [&](const std::vector<float>& hist, COLORREF color) {
    if (hist.size() < 2)
      return;
    CPen pen(PS_SOLID, 2, color);
    CPen* pOld = pDC->SelectObject(&pen);
    int n = (int)hist.size();
    for (int i = 1; i < n; i++) {
      int x0 = rect.left + (int)((rect.Width() - 1) * ((double)(i - 1) / (kMaxSamples - 1)));
      int x1 = rect.left + (int)((rect.Width() - 1) * ((double)i / (kMaxSamples - 1)));
      int y0 = rect.bottom - (int)((rect.Height() - 1) * (hist[i - 1] / 100.0));
      int y1 = rect.bottom - (int)((rect.Height() - 1) * (hist[i] / 100.0));
      pDC->MoveTo(x0, y0);
      pDC->LineTo(x1, y1);
    }
    pDC->SelectObject(pOld);
  };

  drawSeries(m_cpuHistory, RGB(30, 90, 220));
  if (m_gpuAvailable)
    drawSeries(m_gpuHistory, RGB(230, 120, 20));

  CPen borderPen(PS_SOLID, 1, RGB(120, 120, 120));
  pOldPen = pDC->SelectObject(&borderPen);
  pDC->SelectStockObject(NULL_BRUSH);
  pDC->Rectangle(rect);
  pDC->SelectObject(pOldPen);
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
    MessageBox("Chart saved.", "CPU / GPU Load", MB_OK | MB_ICONINFORMATION);
  else
    MessageBox("Could not save the chart as PNG.", "CPU / GPU Load", MB_OK | MB_ICONERROR);
}
