// MyCommandLineInfo.cpp: implementation of the CMyCommandLineInfo class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "femm.h"
#include "MyCommandLineInfo.h"
#include "afxwin.h"
#include "lua.h"

extern lua_State* lua;
extern int m_luaWindowStatus;

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#define new DEBUG_NEW
#endif

extern CString luascriptname;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CMyCommandLineInfo::CMyCommandLineInfo()
{
  //	m_luaWindowStatus=SW_SHOW; // default unless otherwise
}

CMyCommandLineInfo::~CMyCommandLineInfo()
{
}

void CMyCommandLineInfo::ParseParam(LPCTSTR lpszParam, BOOL bFlag, BOOL bLast)
{
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-11:
  // MakeLower() used to be applied to the WHOLE parameter before anything
  // was parsed out of it, so -lua-var=MyVar=C:\Path\File lowercased the
  // variable name AND its value: a script asking for a case-sensitive
  // string got a mangled one, silently (issue #18, cenit/FEMM#18). The
  // lowercasing exists so the FLAGS can be matched case-insensitively, so
  // it now applies to a separate copy used only for those comparisons,
  // while names and values are taken from the original text.
  CString original;
  original.Format("%s", lpszParam);
  CString theparam(original);
  theparam.MakeLower();

  if (theparam.Left(11) == "lua-script=" && bFlag == 1)
    luascriptname = original.Mid(11);

  if (theparam == "windowhide" && bFlag == 1)
    m_luaWindowStatus = SW_SHOWMINNOACTIVE;
  if (theparam == "hidewindow" && bFlag == 1)
    m_luaWindowStatus = SW_SHOWMINNOACTIVE;

  if (theparam == "filelink")
    ((CFemmApp*)AfxGetApp())->bFileLink = TRUE;

  if (theparam.Left(8) == "lua-var=" && bFlag == 1) {
    CString varname;
    CString vardata;
    for (int pos = 8; pos < original.GetLength(); pos++) {
      if (original.Mid(pos, 1) == "=") {
        // both taken from `original`, so case survives -- a Lua variable
        // holding a path, a material name or any identifier is not the
        // command line's to fold
        varname = original.Mid(8, pos - 8);
        vardata = original.Mid(pos + 1, original.GetLength() - pos + 1);
        if (lua != NULL) {
          lua_pushstring(lua, vardata);
          lua_setglobal(lua, varname);
        }
        pos = theparam.GetLength() + 1;
      }
    }
  }

  CCommandLineInfo::ParseParam(lpszParam, bFlag, bLast);
}
