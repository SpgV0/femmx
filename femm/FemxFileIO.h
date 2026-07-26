#pragma once

// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21: per
// user request ("Add support for .ansx and .femx in the old gui as well")
// -- reads/writes the exact same binary .femx cache format femmqt/
// FemxFileIO.cpp already defines (magic "FEMMFEMX", kFemxVersion=2,
// identical struct layouts), so a .femx written by either GUI is usable
// by the other. femmqt's own header (femmqt/FemxFileIO.h) has the full
// design rationale; the short version: .femx is a pure performance cache
// for .fem's geometry/property data (never a second source of truth --
// regenerated from .fem whenever its recorded source size/mtime don't
// match), so this class GUI-side port only needs a freshness check plus
// read/write, no format evolution of its own.
//
// Written in plain C++ (stdio-based file I/O) rather than MFC's CFile,
// matching CFemmeDoc::OnOpenDocument/OnSaveDocument's own existing
// fopen/fread/fwrite style in FemmeDoc.cpp -- no reason to introduce a
// second file-I/O idiom into this codebase for one new feature.

class CFemmeDoc;

namespace FemxFileIO {

// True if "<femxPath>" exists, has the current magic/version, and its
// recorded source .fem size/mtime still match femPath on disk.
bool isUpToDate(const char* femxPath, const char* femPath);

// Reads femxPath directly into doc's geometry/property arrays (nodelist,
// linelist, arclist, blocklist, blockproplist, lineproplist,
// nodeproplist, circproplist) plus its problem-level fields (Frequency,
// Precision, ..., GPUAccel). Clears/overwrites whatever was already in
// those arrays -- same contract as OnOpenDocument's OldOnOpenDocument
// fallback.
bool readFemx(const char* femxPath, CFemmeDoc& doc);

// Writes femxPath from doc's current in-memory state, recording femPath's
// current size/mtime for the next isUpToDate() check. Best-effort: a
// write failure (e.g. read-only directory) isn't fatal to the caller, it
// just means no speedup next time -- matches femmqt's own writeFemx
// callers, which never treat this as a hard error either.
bool writeFemx(const char* femxPath, const char* femPath, CFemmeDoc& doc);

} // namespace FemxFileIO
