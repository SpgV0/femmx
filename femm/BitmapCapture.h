// BitmapCapture.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-09.
//
// One correct implementation of "capture a view into a .bmp", replacing the
// eight near-identical copies of the MSDN "Storing an Image" sample that
// each document class carried as CreateBitmapInfoStruct/CreateBMPFile.
//
// Those copies shared three defects, all of which this file fixes (see
// issue #4):
//
//  1. A degenerate client rect produced a degenerate image, silently.
//     A view that has never been shown -- the normal case when FEMM is
//     driven over COM automation -- reports a client rect with a zero
//     dimension. CreateCompatibleBitmap is documented to return a 1x1
//     MONOCHROME bitmap when either dimension is zero, so savebitmap wrote
//     a valid, useless 58-byte file and reported success. Measured: a
//     hidden session reports 120x0 and yields 1x1 at 1bpp, while the same
//     session after main_maximize() reports 2527x1268 and yields a real
//     12.8MB image. femmCaptureSize() substitutes a documented default
//     instead, so the scripting API works headless.
//
//  2. Every failure reported the same text: "Critical error on getting bmp
//     info, possible page fault ahoyN". MsgBox here accumulates rather than
//     aborting, so a failure early in the sequence let every later step
//     fail too and only the LAST message was reported -- a bad path failed
//     at CreateFile but surfaced as "ahoy23", the final pixel WriteFile,
//     pointing investigation at the DIB code instead of at the filename.
//     These functions fail fast and say what actually went wrong, including
//     GetLastError() and the path.
//
//  3. Seven of the eight copies leaked. Only CFemmviewDoc's restored the
//     previously-selected bitmap and released its DCs; the rest dropped the
//     SelectObject return value, never called ReleaseDC, and let CBitmap's
//     destructor delete a bitmap that was still selected into a DC.
//     Cleanup lives here now, so there is one copy to get right.

#pragma once

#include <windows.h>

// Size to capture a view at, given its client rect. A rect with a
// non-positive dimension (a view that has never been laid out, i.e. any
// hidden COM-automation session) falls back to FEMM_CAPTURE_DEFAULT_CX by
// FEMM_CAPTURE_DEFAULT_CY so the caller still gets a usable image.
#define FEMM_CAPTURE_DEFAULT_CX 1024
#define FEMM_CAPTURE_DEFAULT_CY 768

SIZE femmCaptureSize(const RECT& client);

// Writes hbmp to path as a 24/32-bit .bmp. Returns true on success; on
// failure returns false and fills errOut with a specific, human-readable
// reason. hdc must be a DC compatible with hbmp.
bool femmWriteBitmapFile(HBITMAP hbmp, HDC hdc, LPCTSTR path, CString& errOut);
