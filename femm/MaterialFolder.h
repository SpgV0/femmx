// MaterialFolder.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12
// (issue #34).
//
// The shipped material libraries are folder trees, and two entries in
// them share a name with a DIFFERENT material:
//
//   matlib.dat   "Supermalloy"  Nickel Alloys                   mu_r 529095
//   matlib.dat   "Supermalloy"  Metals Handbook DC Magnetization mu_r 1
//   heatlib.dat  "Ammonia"      Saturated Liquids               k 0.546
//   heatlib.dat  "Ammonia"      Gases at 1 atm                  k 0.0153
//
// *_getmaterial matched on name alone, first in file order, so one of
// each pair was unreachable from scripting and a script asking for the
// name silently got the other. The GUI's browser disambiguates by
// folder; scripting could not. These helpers let the four
// lua_getmaterial implementations track the folder they are currently
// inside while parsing, so an optional folder argument can select
// between duplicates.
//
// A rename was the alternative and was deliberately not taken: users'
// existing models reference these by name, so renaming one would make
// some of those models silently resolve to the other material -- which
// is the very failure being fixed.

#pragma once

#include <afx.h>

// The library format nests <BeginFolder> / <FolderName> = "..." /
// <EndFolder>. Call these from the parse loop, before the per-property
// cases, and the stack tracks where in the tree the parser is.
//
// Returns true if the line was a folder directive (and so needs no
// further interpretation by the caller).
inline bool TrackMaterialFolder(const char* q, const char* s,
    CStringArray& stack)
{
  if (_strnicmp(q, "<beginfolder>", 13) == 0) {
    // Pushed empty; the <FolderName> line that follows names it. A
    // folder without a name still has to occupy a level, or every
    // <EndFolder> after it would pop the wrong one.
    stack.Add(CString());
    return true;
  }
  if (_strnicmp(q, "<endfolder>", 11) == 0) {
    if (stack.GetSize() > 0)
      stack.RemoveAt(stack.GetSize() - 1);
    return true;
  }
  if (_strnicmp(q, "<foldername>", 12) == 0) {
    CString line(s);
    int eq = line.Find('=');
    if (eq >= 0) {
      CString name = line.Mid(eq + 1);
      name.Trim();
      name.Trim("\"");
      name.Trim();
      if (stack.GetSize() > 0)
        stack.SetAt(stack.GetSize() - 1, name);
      else
        stack.Add(name);
    }
    return true;
  }
  return false;
}

// True if `folder` names any level of the current path, compared
// case-insensitively. Matching any level rather than only the innermost
// is deliberate: "Metals Handbook DC Magnetization Curves" is what a user
// reads in the browser, and requiring them to spell out every ancestor to
// disambiguate two entries would be a worse interface than the problem.
inline bool InFolder(const CStringArray& stack, const CString& folder)
{
  if (folder.IsEmpty())
    return true;
  for (int i = 0; i < stack.GetSize(); i++) {
    if (stack.GetAt(i).CompareNoCase(folder) == 0)
      return true;
  }
  return false;
}
