#include "stdafx.h"
#include "problem.h"
#include "femm.h"
#include "xyplot.h"
#include "AnsxFileIO.h"
#include "FemmviewDoc.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

// Struct layouts are byte-for-byte copies of femmqt/AnsxFileIO.cpp's --
// see FemxFileIO.cpp's identical top-of-file note for why this is an
// independent copy rather than a shared header.

namespace {

constexpr uint32_t kAnsxVersion = 3;

#pragma pack(push, 1)
struct AnsxHeader {
  char magic[8]; // "FEMMANSX"
  uint32_t version;
  uint32_t headerSize;
  uint32_t coordSystem; // 0 = planar, 1 = axisymmetric
  uint32_t lengthUnits;
  double frequency;
  double bMagMin, bMagMax;
  uint64_t sourceSize;
  uint64_t sourceMtimeSecs;
  uint64_t nodeCount;
  uint64_t elementCount;
};

struct AnsxNodeRecord {
  double x, y, Are, Aim;
};
struct AnsxElementRecord {
  int64_t p0, p1, p2, lbl;
  double B1re, B1im, B2re, B2im;
  double ctrX, ctrY;
  double muX, muY;
  double sigma;
  double jSrcRe, jSrcIm;
  double jRe, jIm;
};
#pragma pack(pop)
static_assert(sizeof(AnsxHeader) == 80, "AnsxHeader must stay a fixed, packed layout");
static_assert(sizeof(AnsxNodeRecord) == 32, "AnsxNodeRecord must stay a fixed, packed layout");
static_assert(sizeof(AnsxElementRecord) == 136, "AnsxElementRecord must stay a fixed, packed layout");

bool readHeader(FILE* fp, AnsxHeader& header)
{
  if (fread(&header, 1, sizeof(header), fp) != sizeof(header))
    return false;
  if (memcmp(header.magic, "FEMMANSX", 8) != 0)
    return false;
  if (header.version != kAnsxVersion || header.headerSize < sizeof(AnsxHeader))
    return false;
  return true;
}

bool statFile(const char* path, uint64_t& sizeOut, uint64_t& mtimeOut)
{
  struct __stat64 st;
  if (_stat64(path, &st) != 0)
    return false;
  sizeOut = (uint64_t)st.st_size;
  mtimeOut = (uint64_t)st.st_mtime;
  return true;
}

} // namespace

bool AnsxFileIO::isUpToDate(const char* ansxPath, const char* ansPath)
{
  FILE* fp = fopen(ansxPath, "rb");
  if (!fp)
    return false;
  AnsxHeader header;
  bool ok = readHeader(fp, header);
  fclose(fp);
  if (!ok)
    return false;

  uint64_t ansSize, ansMtime;
  if (!statFile(ansPath, ansSize, ansMtime))
    return false;
  return ansSize == header.sourceSize && ansMtime == header.sourceMtimeSecs;
}

bool AnsxFileIO::readAnsx(const char* ansxPath, CFemmviewDoc& doc)
{
  FILE* fp = fopen(ansxPath, "rb");
  if (!fp)
    return false;

  AnsxHeader header;
  if (!readHeader(fp, header)) {
    fclose(fp);
    return false;
  }

  doc.meshnode.SetSize((int)header.nodeCount);
  for (uint64_t i = 0; i < header.nodeCount; i++) {
    AnsxNodeRecord rec;
    if (fread(&rec, 1, sizeof(rec), fp) != sizeof(rec)) {
      fclose(fp);
      return false;
    }
    CMeshNode n;
    n.x = rec.x;
    n.y = rec.y;
    n.A = CComplex(rec.Are, rec.Aim);
    doc.meshnode.SetAt((int)i, n);
  }

  doc.meshelem.SetSize((int)header.elementCount);
  for (uint64_t i = 0; i < header.elementCount; i++) {
    AnsxElementRecord rec;
    if (fread(&rec, 1, sizeof(rec), fp) != sizeof(rec)) {
      fclose(fp);
      return false;
    }
    CElement e;
    e.p[0] = (int)rec.p0;
    e.p[1] = (int)rec.p1;
    e.p[2] = (int)rec.p2;
    e.lbl = (int)rec.lbl;
    e.blk = (rec.lbl >= 0 && rec.lbl < doc.blocklist.GetSize()) ? doc.blocklist[(int)rec.lbl].BlockType : 0;
    e.B1 = CComplex(rec.B1re, rec.B1im);
    e.B2 = CComplex(rec.B2re, rec.B2im);
    e.ctr = CComplex(rec.ctrX, rec.ctrY);
    doc.meshelem.SetAt((int)i, e);
  }

  fclose(fp);
  return true;
}

bool AnsxFileIO::writeAnsx(const char* ansxPath, const char* ansPath, CFemmviewDoc& doc)
{
  uint64_t ansSize, ansMtime;
  if (!statFile(ansPath, ansSize, ansMtime))
    return false;

  FILE* fp = fopen(ansxPath, "wb");
  if (!fp)
    return false;

  int n = doc.meshelem.GetSize();

  // |B| = sqrt(|B1|^2+|B2|^2) -- B1/B2 are the two Cartesian/polar
  // components, not independent field magnitudes -- matching femmqt's
  // own bMagMin/bMagMax definition (MeshSolutionItem's constructor).
  double bMagMin = 0, bMagMax = 0;
  for (int i = 0; i < n; i++) {
    const CComplex& b1 = doc.meshelem[i].B1;
    const CComplex& b2 = doc.meshelem[i].B2;
    double b = sqrt(b1.re * b1.re + b1.im * b1.im + b2.re * b2.re + b2.im * b2.im);
    if (i == 0) {
      bMagMin = bMagMax = b;
    } else {
      if (b < bMagMin)
        bMagMin = b;
      if (b > bMagMax)
        bMagMax = b;
    }
  }

  AnsxHeader header;
  memset(&header, 0, sizeof(header));
  memcpy(header.magic, "FEMMANSX", 8);
  header.version = kAnsxVersion;
  header.headerSize = sizeof(AnsxHeader);
  header.coordSystem = (uint32_t)doc.ProblemType;
  header.lengthUnits = (uint32_t)doc.LengthUnits;
  header.frequency = doc.Frequency;
  header.bMagMin = bMagMin;
  header.bMagMax = bMagMax;
  header.sourceSize = ansSize;
  header.sourceMtimeSecs = ansMtime;
  header.nodeCount = (uint64_t)doc.meshnode.GetSize();
  header.elementCount = (uint64_t)doc.meshelem.GetSize();

  if (fwrite(&header, 1, sizeof(header), fp) != sizeof(header)) {
    fclose(fp);
    return false;
  }

  for (int i = 0; i < doc.meshnode.GetSize(); i++) {
    const CMeshNode& mn = doc.meshnode[i];
    AnsxNodeRecord rec;
    rec.x = mn.x;
    rec.y = mn.y;
    rec.Are = mn.A.re;
    rec.Aim = mn.A.im;
    if (fwrite(&rec, 1, sizeof(rec), fp) != sizeof(rec)) {
      fclose(fp);
      return false;
    }
  }

  CComplex J[3], A[3];
  for (int i = 0; i < n; i++) {
    const CElement& e = doc.meshelem[i];
    AnsxElementRecord rec;
    rec.p0 = e.p[0];
    rec.p1 = e.p[1];
    rec.p2 = e.p[2];
    rec.lbl = e.lbl;
    rec.B1re = e.B1.re;
    rec.B1im = e.B1.im;
    rec.B2re = e.B2.re;
    rec.B2im = e.B2.im;
    rec.ctrX = e.ctr.re;
    rec.ctrY = e.ctr.im;
    if (e.blk >= 0 && e.blk < doc.blockproplist.GetSize()) {
      const CMaterialProp& mp = doc.blockproplist[e.blk];
      rec.muX = mp.mu_x;
      rec.muY = mp.mu_y;
      rec.sigma = mp.Cduct;
      // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
      // the view-side femmviewdata::CMaterialProp (problem.h) stores this
      // as separate Jr/Ji doubles, unlike the draw-side femmedata::
      // CMaterialProp (NOSEBL.H, see FemxFileIO.cpp) which bundles them
      // into a single CComplex Jsrc -- two independently-defined classes
      // with the same name but different member layouts (draw vs. view
      // side, per this file's own header note).
      rec.jSrcRe = mp.Jr;
      rec.jSrcIm = mp.Ji;
    } else {
      rec.muX = rec.muY = 1;
      rec.sigma = rec.jSrcRe = rec.jSrcIm = 0;
    }
    // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
    // GetJA's own comment claims it "returns current density... in
    // units of MA/m^2", but its actual return statement is
    // "return (Javg * 1.e06)" -- the real return value is in A/m^2,
    // matching J[]/A[] (also *1.e06'd just above that return). femmqt's
    // jRe/jIm (and jSrcRe/jSrcIm just above, straight from material
    // Jsrc, which IS in MA/m^2) are in MA/m^2 -- confirmed by
    // femm.rc/SolutionView.cpp's shared "|Js+Je|, MA/m^2" legend label
    // -- so this needs the inverse /1.e06 to land in the same units as
    // every other current-density field in this record, not 1e6x too
    // large.
    CComplex Jtot = doc.GetJA(i, J, A);
    rec.jRe = Jtot.re / 1.0e6;
    rec.jIm = Jtot.im / 1.0e6;
    if (fwrite(&rec, 1, sizeof(rec), fp) != sizeof(rec)) {
      fclose(fp);
      return false;
    }
  }

  fclose(fp);
  return true;
}
