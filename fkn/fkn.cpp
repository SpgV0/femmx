// fkn.cpp : Defines the class behaviors for the application.
//
// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-09:
// shows the CPU/GPU load monitor window (LoadMonitorDlg) alongside the
// existing progress dialog, before the solve thread starts. Also adds
// an atexit() gate (WaitForLoadMonitorClose) so the load monitor -- and
// the process itself -- stays open after the solve finishes until the
// user closes it: old_main() (main.cpp, running on the solve's worker
// thread) calls exit() directly on every code path, which normally
// tears down every window in the process instantly via ExitProcess()
// with no chance to see the final chart.

#include "stdafx.h"
#include "fkn.h"
#include "fknDlg.h"
#include "LoadMonitorDlg.h"
#include <process.h>
#include <stdlib.h>
#include "lua.h"

namespace {

CLoadMonitorDlg* g_pLoadMonitor = NULL;

// Registered via atexit() below. Runs on whichever thread calls exit()
// (old_main's solve worker thread, in practice). Blocks process
// termination until the user closes the load monitor window. This
// doesn't need to pump loadMonitor's own message queue itself -- it was
// created on the main thread, and the main thread's still-running
// modal loop (CFknDlg::DoModal(), which services every window owned by
// this thread, not just the modal dialog) keeps it responsive for the
// whole time this polls.
//
// Skipped entirely for scripted invocations (mi_analyze(1) and friends,
// __argc >= 3 -- see fkn/StdAfx.cpp's MsgBox, which uses this exact same
// check to self-suppress). femmx.exe's OnMenuAnalyze() polls
// GetExitCodeProcess() and blocks the calling Lua/pyfemm script until
// fkn.exe exits, for *every* scripted mi_analyze() call (hidden or not);
// every script in this repo's test suite uses mi_analyze(1), so gating
// only on the existing __argc>=3 signal keeps all of that unattended
// automation exiting exactly as promptly as before this feature existed.
void WaitForLoadMonitorClose() {
  if (__argc >= 3)
    return;
  if (g_pLoadMonitor == NULL)
    return;
  HWND hWnd = g_pLoadMonitor->GetSafeHwnd();
  if (hWnd == NULL || !::IsWindow(hWnd))
    return;
  g_pLoadMonitor->OnSolveFinished();
  while (::IsWindow(hWnd))
    Sleep(100);
}

}  // namespace

extern void lua_baselibopen(lua_State* L);
extern void lua_iolibopen(lua_State* L);
extern void lua_strlibopen(lua_State* L);
extern void lua_mathlibopen(lua_State* L);
extern void lua_dblibopen(lua_State* L);

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CFknApp

BEGIN_MESSAGE_MAP(CFknApp, CWinApp)
//{{AFX_MSG_MAP(CFknApp)
// NOTE - the ClassWizard will add and remove mapping macros here.
//    DO NOT EDIT what you see in these blocks of generated code!
//}}AFX_MSG
ON_COMMAND(ID_HELP, CWinApp::OnHelp)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CFknApp construction

CFknApp::CFknApp()
{
  // TODO: add construction code here,
  // Place all significant initialization in InitInstance
}

/////////////////////////////////////////////////////////////////////////////
// The one and only CFknApp object

CFknApp theApp;
lua_State* lua; // the main lua object

/////////////////////////////////////////////////////////////////////////////
// CFknApp initialization

BOOL CFknApp::InitInstance()
{
  // Standard initialization
  // If you are not using these features and wish to reduce the size
  //  of your final executable, you should remove from the following
  //  the specific initialization routines you do not need.

#ifdef _AFXDLL
//	Enable3dControls();			// Call this when using MFC in a shared DLL
#else
  Enable3dControlsStatic(); // Call this when linking to MFC statically
#endif

  CFknDlg dlg;
  m_pMainWnd = &dlg;

  // Initialize Lua
  lua = lua_open(4096);
  lua_baselibopen(lua);
  lua_strlibopen(lua);
  lua_mathlibopen(lua);
  lua_iolibopen(lua);

  // Modeless and unowned (NULL parent) so it isn't disabled by dlg's
  // modal loop below; it just samples system-wide CPU/GPU load on its
  // own timer and isn't otherwise coupled to the solve thread.
  CLoadMonitorDlg loadMonitor;
  loadMonitor.Create(IDD_LOADMONITOR, NULL);
  loadMonitor.ShowWindow(SW_SHOW);
  // For hidden/scripted runs, femmx.exe's CreateProcess() sets
  // STARTF_USESHOWWINDOW/SW_SHOWMINNOACTIVE (see FemmeView.cpp's
  // OnMenuAnalyze) so the fkern progress dialog doesn't interrupt the
  // user. Windows applies that as a one-shot hint to the *first*
  // top-level window this process shows, regardless of the ShowWindow()
  // call the app actually makes -- since loadMonitor is created before
  // dlg, it silently absorbs the hint and starts minimized/parked
  // off-screen instead of dlg. Force it back to a normal, visible state.
  if (loadMonitor.IsIconic())
    loadMonitor.ShowWindow(SW_RESTORE);
  g_pLoadMonitor = &loadMonitor;
  atexit(WaitForLoadMonitorClose);

  dlg.ComLine = m_lpCmdLine;
  _beginthread(old_main, 0, (void*)&dlg);
  INT_PTR nResponse = dlg.DoModal();
  if (nResponse == IDOK) {
    // TODO: Place code here to handle when the dialog is
    //  dismissed with OK
  } else if (nResponse == IDCANCEL) {
    // TODO: Place code here to handle when the dialog is
    //  dismissed with Cancel
  }

  lua_close(lua);

  // Since the dialog has been closed, return FALSE so that we exit the
  //  application, rather than start the application's message pump.
  return FALSE;
}
