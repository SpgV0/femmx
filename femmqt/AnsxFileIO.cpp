#include "AnsxFileIO.h"

#include "MeshSolution.h"

#include <QFile>
#include <QFileInfo>

#include <cstdint>
#include <cstring>

namespace {

// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21: was 1,
// then 2 -- 2 added AnsxElementRecord's 5 material fields (muX/muY/sigma/
// jSrcRe/jSrcIm); 3 adds jRe/jIm (total current density), which
// MeshSolutionElement gained in a separate edit right after and this
// record initially missed -- a real bug caught live (the |Js+Je| density
// plot rendered as uniformly zero because every cached load silently
// left e.jRe/e.jIm at their 0 default). No attempt at backward
// compatibility: an old file simply fails this version check, which
// isUpToDate() surfaces as "not up to date" -- openAnsFile's existing
// fallback then regenerates it from the source .ans, exactly like any
// other stale-cache case. Fine for a pure performance cache with no
// independent data of its own to lose.
//
// Modified by Claude (Anthropic), noreply@anthropic.com: 3 -> 4 adds
// isExternal (see MeshSolutionElement's own comment) -- caught the exact
// same way jRe/jIm was: a stale .ansx cache from before this field
// existed silently left every element's isExternal at its 0/false
// default, so the density-plot auto-range bug this fixes only reproduced
// on a first/forced-fresh .ans load, never on a cached re-open, until the
// version bump here made every pre-existing cache correctly stale.
// 4 -> 5 adds rsqr (same MeshSolutionElement comment), needed for the
// size-weighted auto-range heuristic alongside isExternal.
//
// Modified by Claude (Anthropic), noreply@anthropic.com: 5 -> 6 adds
// bhMaterialIndex (AnsxElementRecord) plus a new variable-length section
// after the element records for MeshSolution::nonlinearMaterials itself
// (see BHCurve.h) -- same "stale cache silently keeps old/wrong data"
// risk as every version bump above: without it, a .ansx cached before
// this fix existed would keep serving the WRONG (linear-placeholder)
// H field for nonlinear materials indefinitely, since bhMaterialIndex
// would silently read back as 0 (from zero-filled old records) instead
// of -1, and readAnsx wouldn't know to interpret it as "no nonlinear
// material" -- confirmed as the actual reason a first attempt at this
// fix appeared not to work at all when tested live (a stale .ansx from
// before bhMaterialIndex existed, not a logic bug in the fix itself).
constexpr uint32_t kAnsxVersion = 6;

#pragma pack(push, 1)
struct AnsxHeader {
  char magic[8]; // "FEMMANSX"
  uint32_t version;
  uint32_t headerSize; // sizeof(AnsxHeader), lets a future version skip unknown trailing fields
  uint32_t coordSystem; // 0 = planar, 1 = axisymmetric
  uint32_t lengthUnits;
  double frequency;
  double bMagMin, bMagMax;
  uint64_t sourceSize; // source .ans's size, for the staleness check
  uint64_t sourceMtimeSecs; // source .ans's mtime, for the staleness check
  uint64_t nodeCount;
  uint64_t elementCount;
  // Modified by Claude (Anthropic), noreply@anthropic.com: number of
  // MeshSolution::nonlinearMaterials entries in the variable-length
  // section written after the (fixed-size) element records -- see
  // BHCurve.h and kAnsxVersion's own comment.
  uint64_t nonlinearMaterialCount;
};
#pragma pack(pop)
static_assert(sizeof(AnsxHeader) == 88, "AnsxHeader must stay a fixed, packed layout");

// Node/element record layouts, kept deliberately separate from
// MeshSolutionNode/MeshSolutionElement (MeshSolution.h) even though
// they're currently field-for-field identical -- this is the actual
// file-format contract, and MeshSolution is free to gain in-memory-only
// fields later without silently changing what's on disk. int64_t (not
// int32_t) for the element's node/label indices purely to keep every
// field 8-byte aligned with no padding, not because indices need the
// extra range.
#pragma pack(push, 1)
struct AnsxNodeRecord {
  double x, y, Are, Aim;
};
struct AnsxElementRecord {
  int64_t p0, p1, p2, lbl;
  double B1re, B1im, B2re, B2im;
  double ctrX, ctrY;
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
  // resolved material properties (see MeshSolutionElement's own comment)
  // -- precomputed once by AnsFileIO::readAns, cached here the same way
  // B1/B2 already are, rather than re-resolved via the .fem-format header
  // on every load (which would mean re-reading the header from the
  // source .ans even on the "fast" .ansx path, defeating much of the
  // point of this cache for a huge source file).
  double muX, muY;
  double sigma;
  double jSrcRe, jSrcIm;
  // Total current density (source + eddy + solved circuit correction) --
  // added in the same version-2 bump as the fields above but easy to
  // miss since MeshSolutionElement gained it in a separate, later edit:
  // forgetting to also extend this record (and the write/read loops
  // below) would silently leave e.jRe/e.jIm at their 0 default on every
  // load that hits the .ansx cache instead of the raw .ans path -- which
  // is exactly what happened, caught via the |Js+Je| density plot
  // rendering as uniformly zero despite AnsFileIO::readAns computing
  // real values (confirmed by temporarily forcing a fresh .ans-path
  // read, which showed correct numbers) -- see MeshSolutionElement's own
  // jRe/jIm comment.
  double jRe, jIm;
  // Modified by Claude (Anthropic), noreply@anthropic.com: see
  // MeshSolutionElement::isExternal's own comment. int64_t (0/1), not
  // bool, for the same fixed-8-byte-field alignment reason as p0/p1/p2/lbl
  // above.
  int64_t isExternal;
  // Modified by Claude (Anthropic), noreply@anthropic.com: see
  // MeshSolutionElement::rsqr's own comment.
  double rsqr;
  // Modified by Claude (Anthropic), noreply@anthropic.com: see
  // MeshSolutionElement::bhMaterialIndex's own comment. Indexes the new
  // nonlinearMaterials section written after all element records (below)
  // -- -1 (not 0) means "no nonlinear material", so this deliberately
  // does NOT default to a valid-looking index if ever read from a
  // zero-filled/stale buffer.
  int64_t bhMaterialIndex;
};
#pragma pack(pop)
static_assert(sizeof(AnsxNodeRecord) == 32, "AnsxNodeRecord must stay a fixed, packed layout");
static_assert(sizeof(AnsxElementRecord) == 160, "AnsxElementRecord must stay a fixed, packed layout");

bool readHeader(QFile& file, AnsxHeader& header)
{
  if (file.read(reinterpret_cast<char*>(&header), sizeof(header)) != (qint64)sizeof(header))
    return false;
  if (std::memcmp(header.magic, "FEMMANSX", 8) != 0)
    return false;
  if (header.version != kAnsxVersion || header.headerSize < sizeof(AnsxHeader))
    return false;
  return true;
}

} // namespace

bool AnsxFileIO::isUpToDate(const QString& ansxPath, const QString& ansPath)
{
  QFile file(ansxPath);
  if (!file.open(QIODevice::ReadOnly))
    return false;
  AnsxHeader header;
  if (!readHeader(file, header))
    return false;

  QFileInfo ansInfo(ansPath);
  if (!ansInfo.exists())
    return false;
  return (uint64_t)ansInfo.size() == header.sourceSize && (uint64_t)ansInfo.lastModified().toSecsSinceEpoch() == header.sourceMtimeSecs;
}

bool AnsxFileIO::writeAnsx(const QString& ansxPath, const QString& sourceAnsPath,
    int coordSystem, int lengthUnits, double frequency,
    const MeshSolution& solution, QString& errorMessage)
{
  QFileInfo ansInfo(sourceAnsPath);
  if (!ansInfo.exists()) {
    errorMessage = QStringLiteral("Source file \"%1\" does not exist.").arg(sourceAnsPath);
    return false;
  }

  QFile file(ansxPath);
  if (!file.open(QIODevice::WriteOnly)) {
    errorMessage = QStringLiteral("Could not write \"%1\".").arg(ansxPath);
    return false;
  }

  AnsxHeader header{};
  std::memcpy(header.magic, "FEMMANSX", 8);
  header.version = kAnsxVersion;
  header.headerSize = sizeof(AnsxHeader);
  header.coordSystem = (uint32_t)coordSystem;
  header.lengthUnits = (uint32_t)lengthUnits;
  header.frequency = frequency;
  header.bMagMin = solution.bMagMin;
  header.bMagMax = solution.bMagMax;
  header.sourceSize = (uint64_t)ansInfo.size();
  header.sourceMtimeSecs = (uint64_t)ansInfo.lastModified().toSecsSinceEpoch();
  header.nodeCount = (uint64_t)solution.nodes.size();
  header.elementCount = (uint64_t)solution.elements.size();
  header.nonlinearMaterialCount = (uint64_t)solution.nonlinearMaterials.size();

  if (file.write(reinterpret_cast<const char*>(&header), sizeof(header)) != (qint64)sizeof(header)) {
    errorMessage = QStringLiteral("Failed writing \"%1\" header.").arg(ansxPath);
    return false;
  }

  // Bulk-write the whole node array in one call (QVector<MeshSolutionNode>
  // has the same {double x,y,Are,Aim;} layout as AnsxNodeRecord, so no
  // per-record copy/transform is needed here either).
  static_assert(sizeof(MeshSolutionNode) == sizeof(AnsxNodeRecord), "MeshSolutionNode/AnsxNodeRecord layouts must match for the bulk write below");
  if (!solution.nodes.isEmpty()) {
    qint64 bytes = (qint64)solution.nodes.size() * sizeof(AnsxNodeRecord);
    if (file.write(reinterpret_cast<const char*>(solution.nodes.constData()), bytes) != bytes) {
      errorMessage = QStringLiteral("Failed writing \"%1\" node data.").arg(ansxPath);
      return false;
    }
  }

  // Elements can't be bulk-copied this way -- MeshSolutionElement's
  // indices are int, not int64_t (see the AnsxElementRecord comment
  // above) -- so this writes one record at a time. Still just a single
  // fixed-size memcpy-shaped struct per element, no formatting/parsing.
  for (const MeshSolutionElement& e : solution.elements) {
    AnsxElementRecord rec;
    rec.p0 = e.p0;
    rec.p1 = e.p1;
    rec.p2 = e.p2;
    rec.lbl = e.lbl;
    rec.B1re = e.B1re;
    rec.B1im = e.B1im;
    rec.B2re = e.B2re;
    rec.B2im = e.B2im;
    rec.ctrX = e.ctrX;
    rec.ctrY = e.ctrY;
    rec.muX = e.muX;
    rec.muY = e.muY;
    rec.sigma = e.sigma;
    rec.jSrcRe = e.jSrcRe;
    rec.jSrcIm = e.jSrcIm;
    rec.jRe = e.jRe;
    rec.jIm = e.jIm;
    rec.isExternal = e.isExternal ? 1 : 0;
    rec.rsqr = e.rsqr;
    rec.bhMaterialIndex = e.bhMaterialIndex;
    if (file.write(reinterpret_cast<const char*>(&rec), sizeof(rec)) != (qint64)sizeof(rec)) {
      errorMessage = QStringLiteral("Failed writing \"%1\" element data.").arg(ansxPath);
      return false;
    }
  }

  // Modified by Claude (Anthropic), noreply@anthropic.com: variable-length
  // section, one BH curve per solution.nonlinearMaterials entry -- each a
  // point count followed by that many (b, h, slope) triples, all as raw
  // doubles (no per-value formatting/parsing, same bulk-I/O spirit as the
  // fixed-size sections above, just not fixed-size itself since different
  // materials can have different numbers of BH points).
  for (const BHCurve::Curve& curve : solution.nonlinearMaterials) {
    uint64_t n = (uint64_t)curve.b.size();
    if (file.write(reinterpret_cast<const char*>(&n), sizeof(n)) != (qint64)sizeof(n)) {
      errorMessage = QStringLiteral("Failed writing \"%1\" BH curve data.").arg(ansxPath);
      return false;
    }
    for (const QVector<double>* arr : { &curve.b, &curve.h, &curve.slope }) {
      qint64 bytes = (qint64)n * sizeof(double);
      if (arr->size() != (int)n || file.write(reinterpret_cast<const char*>(arr->constData()), bytes) != bytes) {
        errorMessage = QStringLiteral("Failed writing \"%1\" BH curve data.").arg(ansxPath);
        return false;
      }
    }
  }

  return true;
}

bool AnsxFileIO::readAnsx(const QString& ansxPath, MeshSolution& solution, QString& errorMessage, int* coordSystemOut, double* frequencyOut)
{
  QFile file(ansxPath);
  if (!file.open(QIODevice::ReadOnly)) {
    errorMessage = QStringLiteral("Could not open \"%1\".").arg(ansxPath);
    return false;
  }

  AnsxHeader header;
  if (!readHeader(file, header)) {
    errorMessage = QStringLiteral("\"%1\" is not a valid .ansx file.").arg(ansxPath);
    return false;
  }
  if (coordSystemOut)
    *coordSystemOut = (int)header.coordSystem;
  if (frequencyOut)
    *frequencyOut = header.frequency;

  solution = MeshSolution();
  solution.bMagMin = header.bMagMin;
  solution.bMagMax = header.bMagMax;

  // Bulk read straight into the node array -- this is the whole point of
  // the format: no per-line parsing, no per-record transform, just a
  // single fread()-equivalent call.
  static_assert(sizeof(MeshSolutionNode) == sizeof(AnsxNodeRecord), "MeshSolutionNode/AnsxNodeRecord layouts must match for the bulk read below");
  solution.nodes.resize((int)header.nodeCount);
  if (header.nodeCount > 0) {
    qint64 bytes = (qint64)header.nodeCount * sizeof(AnsxNodeRecord);
    if (file.read(reinterpret_cast<char*>(solution.nodes.data()), bytes) != bytes) {
      errorMessage = QStringLiteral("\"%1\" is truncated (node data).").arg(ansxPath);
      return false;
    }
  }

  solution.elements.resize((int)header.elementCount);
  QByteArray elementBuf = file.read((qint64)header.elementCount * sizeof(AnsxElementRecord));
  if ((uint64_t)elementBuf.size() != header.elementCount * sizeof(AnsxElementRecord)) {
    errorMessage = QStringLiteral("\"%1\" is truncated (element data).").arg(ansxPath);
    return false;
  }
  const AnsxElementRecord* recs = reinterpret_cast<const AnsxElementRecord*>(elementBuf.constData());
  for (uint64_t i = 0; i < header.elementCount; i++) {
    MeshSolutionElement& e = solution.elements[(int)i];
    e.p0 = (int)recs[i].p0;
    e.p1 = (int)recs[i].p1;
    e.p2 = (int)recs[i].p2;
    e.lbl = (int)recs[i].lbl;
    e.B1re = recs[i].B1re;
    e.B1im = recs[i].B1im;
    e.B2re = recs[i].B2re;
    e.B2im = recs[i].B2im;
    e.ctrX = recs[i].ctrX;
    e.ctrY = recs[i].ctrY;
    e.muX = recs[i].muX;
    e.muY = recs[i].muY;
    e.sigma = recs[i].sigma;
    e.jSrcRe = recs[i].jSrcRe;
    e.jSrcIm = recs[i].jSrcIm;
    e.jRe = recs[i].jRe;
    e.jIm = recs[i].jIm;
    e.isExternal = recs[i].isExternal != 0;
    e.rsqr = recs[i].rsqr;
    e.bhMaterialIndex = (int)recs[i].bhMaterialIndex;
  }

  // Modified by Claude (Anthropic), noreply@anthropic.com: read back the
  // variable-length BH curve section written above -- see writeAnsx's
  // matching comment.
  solution.nonlinearMaterials.resize((int)header.nonlinearMaterialCount);
  for (uint64_t i = 0; i < header.nonlinearMaterialCount; i++) {
    uint64_t n = 0;
    if (file.read(reinterpret_cast<char*>(&n), sizeof(n)) != (qint64)sizeof(n)) {
      errorMessage = QStringLiteral("\"%1\" is truncated (BH curve data).").arg(ansxPath);
      return false;
    }
    BHCurve::Curve& curve = solution.nonlinearMaterials[(int)i];
    for (QVector<double>* arr : { &curve.b, &curve.h, &curve.slope }) {
      arr->resize((int)n);
      qint64 bytes = (qint64)n * sizeof(double);
      if (n > 0 && file.read(reinterpret_cast<char*>(arr->data()), bytes) != bytes) {
        errorMessage = QStringLiteral("\"%1\" is truncated (BH curve data).").arg(ansxPath);
        return false;
      }
    }
  }

  return true;
}
