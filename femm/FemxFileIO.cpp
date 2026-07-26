#include "stdafx.h"
#include "femm.h"
#include "FemxFileIO.h"
#include "FemmeDoc.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

// See FemxFileIO.h for the overall design note. Struct layouts below are
// byte-for-byte copies of femmqt/FemxFileIO.cpp's -- kept as an
// independent copy (not a shared header) since one side is Qt types and
// the other is plain C++/MFC, matching this project's established
// convention of independent format readers per GUI (see e.g.
// FemmeDoc.cpp's own .fem parser vs femmqt/FemmFileIO.cpp's).

namespace {

constexpr int kNameLen = 64;
constexpr int kPathLen = 260;
constexpr int kCommentLen = 512;
constexpr int kMagDirFctnLen = 256;
constexpr int kMaxBhPoints = 256;
constexpr uint32_t kFemxVersion = 2;

#pragma pack(push, 1)
struct FemxHeader {
  char magic[8]; // "FEMMFEMX"
  uint32_t version;
  uint32_t headerSize;
  uint32_t problemType;
  uint32_t lengthUnits;
  uint8_t coordsPolar;
  uint8_t smartMesh;
  uint8_t reserved[6];
  double frequency, precision, minAngle, depth;
  double extZo, extRo, extRi;
  int32_t acSolver, gpuAccel, prevType;
  char prevSoln[kPathLen];
  char comment[kCommentLen];
  uint64_t sourceFemSize;
  uint64_t sourceFemMtimeSecs;
  uint64_t pointPropCount, boundaryPropCount, materialPropCount, circuitPropCount;
  uint64_t nodeCount, segmentCount, arcCount, blockLabelCount;
};

struct FemxPointPropRecord {
  char name[kNameLen];
  double Jr, Ji, Ar, Ai;
};

struct FemxBoundaryPropRecord {
  char name[kNameLen];
  int32_t bdryFormat;
  double A0, A1, A2, phi;
  double c0re, c0im, c1re, c1im;
  double muSsd, sigmaSsd;
  double innerAngle, outerAngle;
};

struct FemxMaterialPropRecord {
  char name[kNameLen];
  double muX, muY, Hc, HcAngle, JsrcRe, JsrcIm, sigma, dLam, phiH, phiHx, phiHy;
  int32_t lamType, nStrands;
  double lamFill, wireD;
  int32_t bhPointCount; // clamped to kMaxBhPoints on write
  double bhData[kMaxBhPoints][2];
};

struct FemxCircuitPropRecord {
  char name[kNameLen];
  double ampsRe, ampsIm;
  int32_t circType;
};

struct FemxNodeRecord {
  double x, y;
  int32_t pointPropIndex, inGroup;
};

struct FemxSegmentRecord {
  int32_t n0, n1;
  double maxSideLength;
  int32_t boundaryMarker, hidden, inGroup;
};

struct FemxArcRecord {
  int32_t n0, n1;
  double arcLength, maxSideLength;
  int32_t boundaryMarker, hidden, inGroup;
  double mySideLength;
};

struct FemxBlockLabelRecord {
  double x, y;
  int32_t blockTypeIndex;
  double maxArea;
  int32_t circuitIndex;
  double magDir;
  char magDirFctn[kMagDirFctnLen];
  int32_t inGroup, turns, isExternal, isDefault;
};
#pragma pack(pop)

void setFixed(char* dst, int size, const CString& s)
{
  memset(dst, 0, size);
  int n = s.GetLength();
  if (n > size - 1)
    n = size - 1;
  if (n > 0)
    memcpy(dst, (const char*)s, n);
}

CString getFixed(const char* src, int size)
{
  int len = 0;
  while (len < size && src[len] != 0)
    len++;
  return CString(src, len);
}

bool readHeader(FILE* fp, FemxHeader& header)
{
  if (fread(&header, 1, sizeof(header), fp) != sizeof(header))
    return false;
  if (memcmp(header.magic, "FEMMFEMX", 8) != 0)
    return false;
  if (header.version != kFemxVersion || header.headerSize < sizeof(FemxHeader))
    return false;
  return true;
}

// Ground truth for the freshness check is the SOURCE .fem's own size and
// mtime -- matches femmqt's AnsxFileIO/FemxFileIO::isUpToDate exactly, so
// a cache written by one GUI is correctly judged fresh/stale by the
// other, not just by itself.
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

bool FemxFileIO::isUpToDate(const char* femxPath, const char* femPath)
{
  FILE* fp = fopen(femxPath, "rb");
  if (!fp)
    return false;
  FemxHeader header;
  bool ok = readHeader(fp, header);
  fclose(fp);
  if (!ok)
    return false;

  uint64_t femSize, femMtime;
  if (!statFile(femPath, femSize, femMtime))
    return false;
  return femSize == header.sourceFemSize && femMtime == header.sourceFemMtimeSecs;
}

bool FemxFileIO::readFemx(const char* femxPath, CFemmeDoc& doc)
{
  FILE* fp = fopen(femxPath, "rb");
  if (!fp)
    return false;

  FemxHeader header;
  if (!readHeader(fp, header)) {
    fclose(fp);
    return false;
  }

  doc.nodelist.RemoveAll();
  doc.linelist.RemoveAll();
  doc.arclist.RemoveAll();
  doc.blocklist.RemoveAll();
  doc.nodeproplist.RemoveAll();
  doc.lineproplist.RemoveAll();
  doc.blockproplist.RemoveAll();
  doc.circproplist.RemoveAll();

  doc.ProblemType = (BOOL)header.problemType;
  doc.LengthUnits = (int)header.lengthUnits;
  doc.Coords = (BOOL)header.coordsPolar;
  doc.SmartMesh = (int)header.smartMesh;
  doc.Frequency = header.frequency;
  doc.Precision = header.precision;
  doc.MinAngle = header.minAngle;
  doc.Depth = header.depth;
  doc.extZo = header.extZo;
  doc.extRo = header.extRo;
  doc.extRi = header.extRi;
  doc.ACSolver = header.acSolver;
  doc.GPUAccel = header.gpuAccel;
  doc.PrevType = header.prevType;
  doc.PrevSoln = getFixed(header.prevSoln, kPathLen);
  doc.ProblemNote = getFixed(header.comment, kCommentLen);

  for (uint64_t i = 0; i < header.pointPropCount; i++) {
    FemxPointPropRecord rec;
    if (fread(&rec, 1, sizeof(rec), fp) != sizeof(rec)) {
      fclose(fp);
      return false;
    }
    CPointProp pp;
    pp.PointName = getFixed(rec.name, kNameLen);
    pp.Jp = CComplex(rec.Jr, rec.Ji);
    pp.Ap = CComplex(rec.Ar, rec.Ai);
    doc.nodeproplist.Add(pp);
  }

  for (uint64_t i = 0; i < header.boundaryPropCount; i++) {
    FemxBoundaryPropRecord rec;
    if (fread(&rec, 1, sizeof(rec), fp) != sizeof(rec)) {
      fclose(fp);
      return false;
    }
    CBoundaryProp bp;
    bp.BdryName = getFixed(rec.name, kNameLen);
    bp.BdryFormat = rec.bdryFormat;
    bp.A0 = rec.A0;
    bp.A1 = rec.A1;
    bp.A2 = rec.A2;
    bp.phi = rec.phi;
    bp.c0 = CComplex(rec.c0re, rec.c0im);
    bp.c1 = CComplex(rec.c1re, rec.c1im);
    bp.Mu = rec.muSsd;
    bp.Sig = rec.sigmaSsd;
    bp.InnerAngle = rec.innerAngle;
    bp.OuterAngle = rec.outerAngle;
    doc.lineproplist.Add(bp);
  }

  for (uint64_t i = 0; i < header.materialPropCount; i++) {
    FemxMaterialPropRecord rec;
    if (fread(&rec, 1, sizeof(rec), fp) != sizeof(rec)) {
      fclose(fp);
      return false;
    }
    CMaterialProp mp;
    mp.BlockName = getFixed(rec.name, kNameLen);
    mp.mu_x = rec.muX;
    mp.mu_y = rec.muY;
    mp.H_c = rec.Hc;
    mp.Theta_m = rec.HcAngle;
    mp.Jsrc = CComplex(rec.JsrcRe, rec.JsrcIm);
    mp.Cduct = rec.sigma;
    mp.Lam_d = rec.dLam;
    mp.Theta_hn = rec.phiH;
    mp.Theta_hx = rec.phiHx;
    mp.Theta_hy = rec.phiHy;
    mp.LamType = rec.lamType;
    mp.NStrands = rec.nStrands;
    mp.LamFill = rec.lamFill;
    mp.WireD = rec.wireD;
    mp.BHpoints = rec.bhPointCount;
    if (rec.bhPointCount > 0) {
      // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
      // must be calloc, not new[] -- CMaterialProp::~CMaterialProp()
      // (NOSEBL.CPP) frees this with plain free(), matching every
      // existing BHdata allocation site in FemmeDoc.cpp (e.g. its own
      // .fem text parser); new[]/free() is undefined behavior.
      mp.BHdata = (CComplex*)calloc(rec.bhPointCount, sizeof(CComplex));
      for (int32_t j = 0; j < rec.bhPointCount && j < kMaxBhPoints; j++)
        mp.BHdata[j] = CComplex(rec.bhData[j][0], rec.bhData[j][1]);
    }
    doc.blockproplist.Add(mp);
    // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
    // CMaterialProp has no user-declared copy constructor, so
    // CArray::Add() above made a plain memberwise (shallow) copy of mp
    // -- blockproplist's stored copy now points at the SAME BHdata
    // block mp does. mp is local to this loop body and destructs at
    // the end of every iteration, and ~CMaterialProp() unconditionally
    // frees BHdata -- without clearing mp's own pointer first, that
    // free() would leave the copy just added to blockproplist pointing
    // at freed memory on every iteration with BH data. Clearing mp's
    // copy (not blockproplist's) hands ownership to the array entry
    // cleanly; BHpoints=0 keeps mp's own destructor from attempting a
    // second free of a pointer it no longer owns.
    mp.BHdata = NULL;
    mp.BHpoints = 0;
  }

  for (uint64_t i = 0; i < header.circuitPropCount; i++) {
    FemxCircuitPropRecord rec;
    if (fread(&rec, 1, sizeof(rec), fp) != sizeof(rec)) {
      fclose(fp);
      return false;
    }
    CCircuit cp;
    cp.CircName = getFixed(rec.name, kNameLen);
    cp.Amps = CComplex(rec.ampsRe, rec.ampsIm);
    cp.CircType = rec.circType;
    doc.circproplist.Add(cp);
  }

  for (uint64_t i = 0; i < header.nodeCount; i++) {
    FemxNodeRecord rec;
    if (fread(&rec, 1, sizeof(rec), fp) != sizeof(rec)) {
      fclose(fp);
      return false;
    }
    CNode n;
    n.x = rec.x;
    n.y = rec.y;
    n.BoundaryMarker = (rec.pointPropIndex >= 1 && rec.pointPropIndex <= doc.nodeproplist.GetSize())
        ? doc.nodeproplist[rec.pointPropIndex - 1].PointName
        : CString();
    n.InGroup = rec.inGroup;
    doc.nodelist.Add(n);
  }

  for (uint64_t i = 0; i < header.segmentCount; i++) {
    FemxSegmentRecord rec;
    if (fread(&rec, 1, sizeof(rec), fp) != sizeof(rec)) {
      fclose(fp);
      return false;
    }
    CSegment s;
    s.n0 = rec.n0;
    s.n1 = rec.n1;
    s.MaxSideLength = rec.maxSideLength;
    s.BoundaryMarker = (rec.boundaryMarker >= 1 && rec.boundaryMarker <= doc.lineproplist.GetSize())
        ? doc.lineproplist[rec.boundaryMarker - 1].BdryName
        : CString();
    s.Hidden = rec.hidden;
    s.InGroup = rec.inGroup;
    doc.linelist.Add(s);
  }

  for (uint64_t i = 0; i < header.arcCount; i++) {
    FemxArcRecord rec;
    if (fread(&rec, 1, sizeof(rec), fp) != sizeof(rec)) {
      fclose(fp);
      return false;
    }
    CArcSegment a;
    a.n0 = rec.n0;
    a.n1 = rec.n1;
    a.ArcLength = rec.arcLength;
    a.MaxSideLength = rec.maxSideLength;
    a.BoundaryMarker = (rec.boundaryMarker >= 1 && rec.boundaryMarker <= doc.lineproplist.GetSize())
        ? doc.lineproplist[rec.boundaryMarker - 1].BdryName
        : CString();
    a.Hidden = rec.hidden;
    a.InGroup = rec.inGroup;
    a.mySideLength = rec.mySideLength;
    doc.arclist.Add(a);
  }

  for (uint64_t i = 0; i < header.blockLabelCount; i++) {
    FemxBlockLabelRecord rec;
    if (fread(&rec, 1, sizeof(rec), fp) != sizeof(rec)) {
      fclose(fp);
      return false;
    }
    CBlockLabel b;
    b.x = rec.x;
    b.y = rec.y;
    if (rec.blockTypeIndex < 0)
      b.BlockType = "<No Mesh>";
    else if (rec.blockTypeIndex == 0 || rec.blockTypeIndex > doc.blockproplist.GetSize())
      b.BlockType = "<None>";
    else
      b.BlockType = doc.blockproplist[rec.blockTypeIndex - 1].BlockName;
    // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-07-21:
    // MaxArea round-trips through the .fem text format as a diameter
    // (sqrt(4*MaxArea/PI), see OnSaveDocument/OnOpenDocument's own
    // inverse), and femmqt's FemmBlockLabel::maxArea already stores the
    // AREA, matching this field's own name here -- so no conversion
    // needed, just copy straight through, unlike the text format.
    b.MaxArea = rec.maxArea;
    b.InCircuit = (rec.circuitIndex >= 1 && rec.circuitIndex <= doc.circproplist.GetSize())
        ? doc.circproplist[rec.circuitIndex - 1].CircName
        : CString();
    b.MagDir = rec.magDir;
    b.MagDirFctn = getFixed(rec.magDirFctn, kMagDirFctnLen);
    b.InGroup = rec.inGroup;
    b.Turns = rec.turns;
    b.IsExternal = rec.isExternal ? TRUE : FALSE;
    b.IsDefault = rec.isDefault ? TRUE : FALSE;
    doc.blocklist.Add(b);
  }

  fclose(fp);
  return true;
}

bool FemxFileIO::writeFemx(const char* femxPath, const char* femPath, CFemmeDoc& doc)
{
  uint64_t femSize, femMtime;
  if (!statFile(femPath, femSize, femMtime))
    return false;

  FILE* fp = fopen(femxPath, "wb");
  if (!fp)
    return false;

  FemxHeader header;
  memset(&header, 0, sizeof(header));
  memcpy(header.magic, "FEMMFEMX", 8);
  header.version = kFemxVersion;
  header.headerSize = sizeof(FemxHeader);
  header.problemType = (uint32_t)doc.ProblemType;
  header.lengthUnits = (uint32_t)doc.LengthUnits;
  header.coordsPolar = doc.Coords ? 1 : 0;
  header.smartMesh = doc.SmartMesh ? 1 : 0;
  header.frequency = doc.Frequency;
  header.precision = doc.Precision;
  header.minAngle = doc.MinAngle;
  header.depth = doc.Depth;
  header.extZo = doc.extZo;
  header.extRo = doc.extRo;
  header.extRi = doc.extRi;
  header.acSolver = doc.ACSolver;
  header.gpuAccel = doc.GPUAccel;
  header.prevType = doc.PrevType;
  setFixed(header.prevSoln, kPathLen, doc.PrevSoln);
  setFixed(header.comment, kCommentLen, doc.ProblemNote);
  header.sourceFemSize = femSize;
  header.sourceFemMtimeSecs = femMtime;
  header.pointPropCount = (uint64_t)doc.nodeproplist.GetSize();
  header.boundaryPropCount = (uint64_t)doc.lineproplist.GetSize();
  header.materialPropCount = (uint64_t)doc.blockproplist.GetSize();
  header.circuitPropCount = (uint64_t)doc.circproplist.GetSize();
  header.nodeCount = (uint64_t)doc.nodelist.GetSize();
  header.segmentCount = (uint64_t)doc.linelist.GetSize();
  header.arcCount = (uint64_t)doc.arclist.GetSize();
  header.blockLabelCount = (uint64_t)doc.blocklist.GetSize();

  if (fwrite(&header, 1, sizeof(header), fp) != sizeof(header)) {
    fclose(fp);
    return false;
  }

  for (int i = 0; i < doc.nodeproplist.GetSize(); i++) {
    const CPointProp& pp = doc.nodeproplist[i];
    FemxPointPropRecord rec;
    memset(&rec, 0, sizeof(rec));
    setFixed(rec.name, kNameLen, pp.PointName);
    rec.Jr = pp.Jp.re;
    rec.Ji = pp.Jp.im;
    rec.Ar = pp.Ap.re;
    rec.Ai = pp.Ap.im;
    if (fwrite(&rec, 1, sizeof(rec), fp) != sizeof(rec)) {
      fclose(fp);
      return false;
    }
  }

  for (int i = 0; i < doc.lineproplist.GetSize(); i++) {
    const CBoundaryProp& bp = doc.lineproplist[i];
    FemxBoundaryPropRecord rec;
    memset(&rec, 0, sizeof(rec));
    setFixed(rec.name, kNameLen, bp.BdryName);
    rec.bdryFormat = bp.BdryFormat;
    rec.A0 = bp.A0;
    rec.A1 = bp.A1;
    rec.A2 = bp.A2;
    rec.phi = bp.phi;
    rec.c0re = bp.c0.re;
    rec.c0im = bp.c0.im;
    rec.c1re = bp.c1.re;
    rec.c1im = bp.c1.im;
    rec.muSsd = bp.Mu;
    rec.sigmaSsd = bp.Sig;
    rec.innerAngle = bp.InnerAngle;
    rec.outerAngle = bp.OuterAngle;
    if (fwrite(&rec, 1, sizeof(rec), fp) != sizeof(rec)) {
      fclose(fp);
      return false;
    }
  }

  for (int i = 0; i < doc.blockproplist.GetSize(); i++) {
    const CMaterialProp& mp = doc.blockproplist[i];
    FemxMaterialPropRecord rec;
    memset(&rec, 0, sizeof(rec));
    setFixed(rec.name, kNameLen, mp.BlockName);
    rec.muX = mp.mu_x;
    rec.muY = mp.mu_y;
    rec.Hc = mp.H_c;
    rec.HcAngle = mp.Theta_m;
    rec.JsrcRe = mp.Jsrc.re;
    rec.JsrcIm = mp.Jsrc.im;
    rec.sigma = mp.Cduct;
    rec.dLam = mp.Lam_d;
    rec.phiH = mp.Theta_hn;
    rec.phiHx = mp.Theta_hx;
    rec.phiHy = mp.Theta_hy;
    rec.lamType = mp.LamType;
    rec.nStrands = mp.NStrands;
    rec.lamFill = mp.LamFill;
    rec.wireD = mp.WireD;
    rec.bhPointCount = (mp.BHpoints > kMaxBhPoints) ? kMaxBhPoints : mp.BHpoints;
    for (int32_t j = 0; j < rec.bhPointCount; j++) {
      rec.bhData[j][0] = mp.BHdata[j].re;
      rec.bhData[j][1] = mp.BHdata[j].im;
    }
    if (fwrite(&rec, 1, sizeof(rec), fp) != sizeof(rec)) {
      fclose(fp);
      return false;
    }
  }

  for (int i = 0; i < doc.circproplist.GetSize(); i++) {
    const CCircuit& cp = doc.circproplist[i];
    FemxCircuitPropRecord rec;
    memset(&rec, 0, sizeof(rec));
    setFixed(rec.name, kNameLen, cp.CircName);
    rec.ampsRe = cp.Amps.re;
    rec.ampsIm = cp.Amps.im;
    rec.circType = cp.CircType;
    if (fwrite(&rec, 1, sizeof(rec), fp) != sizeof(rec)) {
      fclose(fp);
      return false;
    }
  }

  for (int i = 0; i < doc.nodelist.GetSize(); i++) {
    const CNode& n = doc.nodelist[i];
    FemxNodeRecord rec;
    rec.x = n.x;
    rec.y = n.y;
    rec.pointPropIndex = 0;
    for (int j = 0; j < doc.nodeproplist.GetSize(); j++)
      if (doc.nodeproplist[j].PointName == n.BoundaryMarker)
        rec.pointPropIndex = j + 1;
    rec.inGroup = n.InGroup;
    if (fwrite(&rec, 1, sizeof(rec), fp) != sizeof(rec)) {
      fclose(fp);
      return false;
    }
  }

  for (int i = 0; i < doc.linelist.GetSize(); i++) {
    const CSegment& s = doc.linelist[i];
    FemxSegmentRecord rec;
    rec.n0 = s.n0;
    rec.n1 = s.n1;
    rec.maxSideLength = s.MaxSideLength;
    rec.boundaryMarker = 0;
    for (int j = 0; j < doc.lineproplist.GetSize(); j++)
      if (doc.lineproplist[j].BdryName == s.BoundaryMarker)
        rec.boundaryMarker = j + 1;
    rec.hidden = s.Hidden;
    rec.inGroup = s.InGroup;
    if (fwrite(&rec, 1, sizeof(rec), fp) != sizeof(rec)) {
      fclose(fp);
      return false;
    }
  }

  for (int i = 0; i < doc.arclist.GetSize(); i++) {
    const CArcSegment& a = doc.arclist[i];
    FemxArcRecord rec;
    rec.n0 = a.n0;
    rec.n1 = a.n1;
    rec.arcLength = a.ArcLength;
    rec.maxSideLength = a.MaxSideLength;
    rec.boundaryMarker = 0;
    for (int j = 0; j < doc.lineproplist.GetSize(); j++)
      if (doc.lineproplist[j].BdryName == a.BoundaryMarker)
        rec.boundaryMarker = j + 1;
    rec.hidden = a.Hidden;
    rec.inGroup = a.InGroup;
    rec.mySideLength = a.mySideLength;
    if (fwrite(&rec, 1, sizeof(rec), fp) != sizeof(rec)) {
      fclose(fp);
      return false;
    }
  }

  for (int i = 0; i < doc.blocklist.GetSize(); i++) {
    const CBlockLabel& b = doc.blocklist[i];
    FemxBlockLabelRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.x = b.x;
    rec.y = b.y;
    if (b.BlockType == "<No Mesh>") {
      rec.blockTypeIndex = -1;
    } else {
      rec.blockTypeIndex = 0;
      for (int j = 0; j < doc.blockproplist.GetSize(); j++)
        if (doc.blockproplist[j].BlockName == b.BlockType)
          rec.blockTypeIndex = j + 1;
    }
    rec.maxArea = b.MaxArea;
    rec.circuitIndex = 0;
    for (int j = 0; j < doc.circproplist.GetSize(); j++)
      if (doc.circproplist[j].CircName == b.InCircuit)
        rec.circuitIndex = j + 1;
    rec.magDir = b.MagDir;
    setFixed(rec.magDirFctn, kMagDirFctnLen, b.MagDirFctn);
    rec.inGroup = b.InGroup;
    rec.turns = b.Turns;
    rec.isExternal = b.IsExternal ? 1 : 0;
    rec.isDefault = b.IsDefault ? 1 : 0;
    if (fwrite(&rec, 1, sizeof(rec), fp) != sizeof(rec)) {
      fclose(fp);
      return false;
    }
  }

  fclose(fp);
  return true;
}
