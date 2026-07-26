#pragma once

// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21: per
// user request ("Add support for .ansx and .femx in the old gui as
// well") -- reads/writes the exact same binary .ansx cache format
// femmqt/AnsxFileIO.cpp already defines (magic "FEMMANSX", version 3,
// identical node/element record layouts), so a .ansx written by either
// GUI is usable by the other. See femmqt/AnsxFileIO.h for the full
// design rationale.
//
// Scope note: only meshnode/meshelem are cached -- the part of a solved
// .ans that actually scales with mesh size and dominates load time for a
// large model (see FemmviewDoc.cpp's own "this loop dominates .ans load
// time" comment on the text parser it's paired with). Everything else
// -- geometry/properties, the per-block-label circuit correction section
// (Case/dVolts/J, bounded by block-label count, not mesh size), and the
// air gap element section (a comparatively small, advanced-feature list)
// -- is always still read from the source .ans text regardless of
// whether the mesh fast path is used, cheap even for a huge file, and
// matching femmqt's own established "not a serialization of the full
// problem" scope for this format.
//
// Written in plain C++ (stdio-based file I/O), matching FemmviewDoc.cpp's
// own existing fopen/fread/fwrite style.

class CFemmviewDoc;

namespace AnsxFileIO {

bool isUpToDate(const char* ansxPath, const char* ansPath);

// Populates doc.meshnode/meshelem, including B1/B2/ctr (derived by
// FemmviewDoc.cpp's own post-parse pass on the non-cached path -- see
// that pass's comment in OnOpenDocument for why writeAnsx must run
// AFTER it, not before) and blk (resolved from doc.blocklist, which
// must already be populated -- i.e. this must be called after the
// geometry/property text sections, not before). rsqr/magdir are NOT
// included (not part of this cache's scope -- see the header comment
// above) -- OnOpenDocument's existing post-parse passes for those still
// run unconditionally, whether or not this fast path was used.
bool readAnsx(const char* ansxPath, CFemmviewDoc& doc);

// Call once doc.meshnode/meshelem/blocklist are fully populated and
// post-processed (i.e. at the same point OnOpenDocument itself would be
// ready to return TRUE) so B1/B2/ctr/rsqr are already correct. Also
// resolves and stores each element's material properties (muX/muY/
// sigma/Jsrc, via doc.blockproplist) and total current density (via
// doc.GetJA(), the same function the post-processor's own J/H plots
// call) -- matching femmqt's own AnsxElementRecord fields exactly, so a
// classic-written .ansx still drives femmqt's |H|/|Js+Je| density plots
// correctly. Best-effort: a write failure isn't fatal to the open/solve
// that triggered it.
bool writeAnsx(const char* ansxPath, const char* ansPath, CFemmviewDoc& doc);

}
