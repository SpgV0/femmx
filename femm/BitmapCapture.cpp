// BitmapCapture.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-09.
// See BitmapCapture.h for why this exists (issue #4).

#include "stdafx.h"
#include "BitmapCapture.h"

SIZE femmCaptureSize(const RECT& client)
{
  SIZE s;
  s.cx = client.right - client.left;
  s.cy = client.bottom - client.top;

  // CreateCompatibleBitmap returns a 1x1 monochrome bitmap if either
  // dimension is zero, which is how savebitmap used to produce a valid but
  // useless file without reporting anything. Substitute a real size.
  // If EITHER dimension is degenerate the view has not been laid out at all,
  // so neither number means anything -- a hidden session reports 120x0, and
  // honouring that 120 produced a 120x768 sliver. Fall back on both.
  if (s.cx <= 0 || s.cy <= 0) {
    s.cx = FEMM_CAPTURE_DEFAULT_CX;
    s.cy = FEMM_CAPTURE_DEFAULT_CY;
  }
  return s;
}

bool femmWriteBitmapFile(HBITMAP hbmp, HDC hdc, LPCTSTR path, CString& errOut)
{
  if (hbmp == NULL) {
    errOut = "savebitmap: no bitmap to write (bitmap creation failed).";
    return false;
  }

  BITMAP bmp;
  ZeroMemory(&bmp, sizeof(bmp));
  if (!GetObject(hbmp, sizeof(BITMAP), (LPSTR)&bmp)) {
    errOut.Format("savebitmap: GetObject failed on the captured bitmap (error %lu).",
        (unsigned long)GetLastError());
    return false;
  }
  if (bmp.bmWidth <= 0 || bmp.bmHeight <= 0) {
    errOut.Format("savebitmap: captured bitmap is %ldx%ld, nothing to write.",
        (long)bmp.bmWidth, (long)bmp.bmHeight);
    return false;
  }

  // Always ask GDI for a plain 24-bit bottom-up DIB. The originals derived a
  // bit depth from the source bitmap and then built a palette for anything
  // under 24bpp, which is where the 1x1 monochrome capture ended up; asking
  // for a fixed format removes the palette path entirely.
  const LONG cx = bmp.bmWidth;
  const LONG cy = bmp.bmHeight;
  const DWORD rowBytes = (DWORD)(((cx * 24 + 31) & ~31) / 8);
  const DWORD imageBytes = rowBytes * (DWORD)cy;

  BITMAPINFO bi;
  ZeroMemory(&bi, sizeof(bi));
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = cx;
  bi.bmiHeader.biHeight = cy;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 24;
  bi.bmiHeader.biCompression = BI_RGB;
  bi.bmiHeader.biSizeImage = imageBytes;

  LPBYTE bits = (LPBYTE)GlobalAlloc(GMEM_FIXED, imageBytes);
  if (bits == NULL) {
    errOut.Format("savebitmap: could not allocate %lu bytes for the image.",
        (unsigned long)imageBytes);
    return false;
  }

  // GetDIBits can rewrite the header it is handed; re-read the size it
  // actually produced rather than trusting the value computed above, which
  // is the buffer-overrun the originals were exposed to.
  if (!GetDIBits(hdc, hbmp, 0, (UINT)cy, bits, &bi, DIB_RGB_COLORS)) {
    DWORD e = GetLastError();
    GlobalFree((HGLOBAL)bits);
    errOut.Format("savebitmap: GetDIBits failed for a %ldx%ld bitmap (error %lu).",
        (long)cx, (long)cy, (unsigned long)e);
    return false;
  }
  DWORD payload = bi.bmiHeader.biSizeImage;
  if (payload == 0 || payload > imageBytes)
    payload = imageBytes;

  HANDLE hf = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
  if (hf == INVALID_HANDLE_VALUE) {
    DWORD e = GetLastError();
    GlobalFree((HGLOBAL)bits);
    // Naming the path matters: pyfemm's *_savebitmap wrappers omit the
    // fixpath() call their *_saveas siblings make, so a Windows path
    // arrives with its backslashes already eaten as Lua escapes and this
    // is the first place that becomes visible.
    errOut.Format("savebitmap: could not create \"%s\" (error %lu). "
                  "If this path looks mangled, pass forward slashes.",
        (LPCTSTR)CString(path), (unsigned long)e);
    return false;
  }

  BITMAPFILEHEADER hdr;
  ZeroMemory(&hdr, sizeof(hdr));
  hdr.bfType = 0x4d42; // "BM"
  hdr.bfOffBits = (DWORD)(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER));
  hdr.bfSize = hdr.bfOffBits + payload;

  DWORD written = 0;
  bool ok = true;
  CString stage;

  if (ok && (!WriteFile(hf, &hdr, sizeof(hdr), &written, NULL) || written != sizeof(hdr))) {
    ok = false; stage = "file header";
  }
  if (ok && (!WriteFile(hf, &bi.bmiHeader, sizeof(BITMAPINFOHEADER), &written, NULL)
             || written != sizeof(BITMAPINFOHEADER))) {
    ok = false; stage = "info header";
  }
  if (ok && (!WriteFile(hf, bits, payload, &written, NULL) || written != payload)) {
    ok = false; stage = "pixel data";
  }

  DWORD writeErr = ok ? 0 : GetLastError();
  CloseHandle(hf);
  GlobalFree((HGLOBAL)bits);

  if (!ok) {
    errOut.Format("savebitmap: failed writing the %s of \"%s\" (error %lu).",
        (LPCTSTR)stage, (LPCTSTR)CString(path), (unsigned long)writeErr);
    return false;
  }
  return true;
}
