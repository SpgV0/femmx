#pragma once

#include <QString>

struct FemmProblem;

// .femx: a pure-binary cache of a .fem file, mirroring .ansx's design
// (AnsxFileIO.h) but for editable geometry instead of a solved mesh --
// same magic+version header, same fixed-size-record layout, same
// size/mtime staleness check, same "regenerable performance cache, not a
// second source of truth" role. .fem stays the canonical, portable,
// interchange format (femmx.exe/fkn.exe/triangle.exe only ever read/write
// .fem); .femx exists purely so femmqt.exe's own re-opens of a file it
// already saved skip re-parsing text.
//
// Unlike .ansx (whose source data -- millions of mesh nodes/elements --
// is genuinely large), a typical .fem is small (thousands of entities at
// most), so the win here is more modest and more about consistency with
// the .ansx approach than fixing an observed bottleneck -- still
// implemented the same way since a fast binary path costs little once
// the pattern already exists.
//
// Fixed-size string fields (name/comment/prevSoln/MagDirFctn) rather than
// a variable-length string pool, for simplicity -- truncates anything
// longer than the field, which is expected to be exceedingly rare for
// these particular strings (material/boundary names, short comments).
// BH curve points per material are similarly capped (see kMaxBhPoints);
// a material with more points than that falls back to being written with
// only the first kMaxBhPoints when cached to .femx -- also expected rare
// in practice, and callers can always fall back to re-reading the source
// .fem (which has no such cap) if this ever matters.
namespace FemxFileIO {

bool isUpToDate(const QString& femxPath, const QString& femPath);

bool writeFemx(const QString& femxPath, const QString& sourceFemPath,
    const FemmProblem& problem, QString& errorMessage);

bool readFemx(const QString& femxPath, FemmProblem& problem, QString& errorMessage);

}
