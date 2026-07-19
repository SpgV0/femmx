#include "FemxFileIO.h"

#include "FemmProblem.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace {

constexpr int kNameLen = 64;
constexpr int kPathLen = 260;
constexpr int kCommentLen = 512;
constexpr int kMagDirFctnLen = 256;
constexpr int kMaxBhPoints = 64;

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
  int32_t boundaryMarker, inGroup;
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

void setFixed(char* dst, int size, const QString& s)
{
  std::memset(dst, 0, size);
  QByteArray utf8 = s.toUtf8();
  int n = std::min((int)utf8.size(), size - 1);
  if (n > 0)
    std::memcpy(dst, utf8.constData(), n);
}

QString getFixed(const char* src, int size)
{
  // src isn't guaranteed null-terminated if a write ever filled the whole
  // buffer (setFixed above always leaves at least one trailing zero, but
  // guard here too in case of a corrupt/foreign file).
  int len = 0;
  while (len < size && src[len] != 0)
    len++;
  return QString::fromUtf8(src, len);
}

template <typename T>
bool writeArray(QFile& file, const T* data, size_t count, QString& errorMessage, const QString& what)
{
  if (count == 0)
    return true;
  qint64 bytes = (qint64)count * sizeof(T);
  if (file.write(reinterpret_cast<const char*>(data), bytes) != bytes) {
    errorMessage = QStringLiteral("Failed writing %1.").arg(what);
    return false;
  }
  return true;
}

template <typename T>
bool readArray(QFile& file, QVector<T>& out, uint64_t count, QString& errorMessage, const QString& what)
{
  out.resize((int)count);
  if (count == 0)
    return true;
  qint64 bytes = (qint64)count * sizeof(T);
  if (file.read(reinterpret_cast<char*>(out.data()), bytes) != bytes) {
    errorMessage = QStringLiteral("\"%1\" is truncated.").arg(what);
    return false;
  }
  return true;
}

bool readHeader(QFile& file, FemxHeader& header)
{
  if (file.read(reinterpret_cast<char*>(&header), sizeof(header)) != (qint64)sizeof(header))
    return false;
  if (std::memcmp(header.magic, "FEMMFEMX", 8) != 0)
    return false;
  if (header.version != 1 || header.headerSize < sizeof(FemxHeader))
    return false;
  return true;
}

} // namespace

bool FemxFileIO::isUpToDate(const QString& femxPath, const QString& femPath)
{
  QFile file(femxPath);
  if (!file.open(QIODevice::ReadOnly))
    return false;
  FemxHeader header;
  if (!readHeader(file, header))
    return false;

  QFileInfo femInfo(femPath);
  if (!femInfo.exists())
    return false;
  return (uint64_t)femInfo.size() == header.sourceFemSize && (uint64_t)femInfo.lastModified().toSecsSinceEpoch() == header.sourceFemMtimeSecs;
}

bool FemxFileIO::writeFemx(const QString& femxPath, const QString& sourceFemPath,
    const FemmProblem& p, QString& errorMessage)
{
  QFileInfo femInfo(sourceFemPath);
  if (!femInfo.exists()) {
    errorMessage = QStringLiteral("Source file \"%1\" does not exist.").arg(sourceFemPath);
    return false;
  }

  QFile file(femxPath);
  if (!file.open(QIODevice::WriteOnly)) {
    errorMessage = QStringLiteral("Could not write \"%1\".").arg(femxPath);
    return false;
  }

  FemxHeader header{};
  std::memcpy(header.magic, "FEMMFEMX", 8);
  header.version = 1;
  header.headerSize = sizeof(FemxHeader);
  header.problemType = (uint32_t)p.problemType;
  header.lengthUnits = (uint32_t)p.lengthUnits;
  header.coordsPolar = p.coordsPolar ? 1 : 0;
  header.smartMesh = p.smartMesh ? 1 : 0;
  header.frequency = p.frequency;
  header.precision = p.precision;
  header.minAngle = p.minAngle;
  header.depth = p.depth;
  header.extZo = p.extZo;
  header.extRo = p.extRo;
  header.extRi = p.extRi;
  header.acSolver = p.acSolver;
  header.gpuAccel = p.gpuAccel;
  header.prevType = p.prevType;
  setFixed(header.prevSoln, kPathLen, p.prevSoln);
  setFixed(header.comment, kCommentLen, p.comment);
  header.sourceFemSize = (uint64_t)femInfo.size();
  header.sourceFemMtimeSecs = (uint64_t)femInfo.lastModified().toSecsSinceEpoch();
  header.pointPropCount = (uint64_t)p.pointProps.size();
  header.boundaryPropCount = (uint64_t)p.boundaryProps.size();
  header.materialPropCount = (uint64_t)p.materialProps.size();
  header.circuitPropCount = (uint64_t)p.circuitProps.size();
  header.nodeCount = (uint64_t)p.nodes.size();
  header.segmentCount = (uint64_t)p.segments.size();
  header.arcCount = (uint64_t)p.arcSegments.size();
  header.blockLabelCount = (uint64_t)p.blockLabels.size();

  if (file.write(reinterpret_cast<const char*>(&header), sizeof(header)) != (qint64)sizeof(header)) {
    errorMessage = QStringLiteral("Failed writing \"%1\" header.").arg(femxPath);
    return false;
  }

  for (const FemmPointProp& pp : p.pointProps) {
    FemxPointPropRecord rec{};
    setFixed(rec.name, kNameLen, pp.name);
    rec.Jr = pp.Jr;
    rec.Ji = pp.Ji;
    rec.Ar = pp.Ar;
    rec.Ai = pp.Ai;
    if (!writeArray(file, &rec, 1, errorMessage, "point property data"))
      return false;
  }

  for (const FemmBoundaryProp& b : p.boundaryProps) {
    FemxBoundaryPropRecord rec{};
    setFixed(rec.name, kNameLen, b.name);
    rec.bdryFormat = b.bdryFormat;
    rec.A0 = b.A0;
    rec.A1 = b.A1;
    rec.A2 = b.A2;
    rec.phi = b.phi;
    rec.c0re = b.c0re;
    rec.c0im = b.c0im;
    rec.c1re = b.c1re;
    rec.c1im = b.c1im;
    rec.muSsd = b.muSsd;
    rec.sigmaSsd = b.sigmaSsd;
    rec.innerAngle = b.innerAngle;
    rec.outerAngle = b.outerAngle;
    if (!writeArray(file, &rec, 1, errorMessage, "boundary property data"))
      return false;
  }

  for (const FemmMaterialProp& m : p.materialProps) {
    FemxMaterialPropRecord rec{};
    setFixed(rec.name, kNameLen, m.name);
    rec.muX = m.muX;
    rec.muY = m.muY;
    rec.Hc = m.Hc;
    rec.HcAngle = m.HcAngle;
    rec.JsrcRe = m.JsrcRe;
    rec.JsrcIm = m.JsrcIm;
    rec.sigma = m.sigma;
    rec.dLam = m.dLam;
    rec.phiH = m.phiH;
    rec.phiHx = m.phiHx;
    rec.phiHy = m.phiHy;
    rec.lamType = m.lamType;
    rec.nStrands = m.nStrands;
    rec.lamFill = m.lamFill;
    rec.wireD = m.wireD;
    rec.bhPointCount = std::min((int)m.bhData.size(), kMaxBhPoints);
    for (int i = 0; i < rec.bhPointCount; i++) {
      rec.bhData[i][0] = m.bhData[i].first;
      rec.bhData[i][1] = m.bhData[i].second;
    }
    if (!writeArray(file, &rec, 1, errorMessage, "material property data"))
      return false;
  }

  for (const FemmCircuitProp& c : p.circuitProps) {
    FemxCircuitPropRecord rec{};
    setFixed(rec.name, kNameLen, c.name);
    rec.ampsRe = c.ampsRe;
    rec.ampsIm = c.ampsIm;
    rec.circType = c.circType;
    if (!writeArray(file, &rec, 1, errorMessage, "circuit property data"))
      return false;
  }

  if (!p.nodes.isEmpty()) {
    QVector<FemxNodeRecord> recs(p.nodes.size());
    for (int i = 0; i < p.nodes.size(); i++)
      recs[i] = { p.nodes[i].x, p.nodes[i].y, p.nodes[i].boundaryMarker, p.nodes[i].inGroup };
    if (!writeArray(file, recs.constData(), recs.size(), errorMessage, "node data"))
      return false;
  }

  if (!p.segments.isEmpty()) {
    QVector<FemxSegmentRecord> recs(p.segments.size());
    for (int i = 0; i < p.segments.size(); i++) {
      const FemmSegment& s = p.segments[i];
      recs[i] = { s.n0, s.n1, s.maxSideLength, s.boundaryMarker, s.hidden ? 1 : 0, s.inGroup };
    }
    if (!writeArray(file, recs.constData(), recs.size(), errorMessage, "segment data"))
      return false;
  }

  if (!p.arcSegments.isEmpty()) {
    QVector<FemxArcRecord> recs(p.arcSegments.size());
    for (int i = 0; i < p.arcSegments.size(); i++) {
      const FemmArcSegment& a = p.arcSegments[i];
      recs[i] = { a.n0, a.n1, a.arcLength, a.maxSideLength, a.boundaryMarker, a.hidden ? 1 : 0, a.inGroup, a.mySideLength };
    }
    if (!writeArray(file, recs.constData(), recs.size(), errorMessage, "arc data"))
      return false;
  }

  for (const FemmBlockLabel& b : p.blockLabels) {
    FemxBlockLabelRecord rec{};
    rec.x = b.x;
    rec.y = b.y;
    rec.blockTypeIndex = b.blockTypeIndex;
    rec.maxArea = b.maxArea;
    rec.circuitIndex = b.circuitIndex;
    rec.magDir = b.magDir;
    setFixed(rec.magDirFctn, kMagDirFctnLen, b.magDirFctn);
    rec.inGroup = b.inGroup;
    rec.turns = b.turns;
    rec.isExternal = b.isExternal ? 1 : 0;
    rec.isDefault = b.isDefault ? 1 : 0;
    if (!writeArray(file, &rec, 1, errorMessage, "block label data"))
      return false;
  }

  return true;
}

bool FemxFileIO::readFemx(const QString& femxPath, FemmProblem& p, QString& errorMessage)
{
  QFile file(femxPath);
  if (!file.open(QIODevice::ReadOnly)) {
    errorMessage = QStringLiteral("Could not open \"%1\".").arg(femxPath);
    return false;
  }

  FemxHeader header;
  if (!readHeader(file, header)) {
    errorMessage = QStringLiteral("\"%1\" is not a valid .femx file.").arg(femxPath);
    return false;
  }

  p = FemmProblem();
  p.problemType = (FemmCoordinateType)header.problemType;
  p.lengthUnits = (FemmLengthUnits)header.lengthUnits;
  p.coordsPolar = header.coordsPolar != 0;
  p.smartMesh = header.smartMesh != 0;
  p.frequency = header.frequency;
  p.precision = header.precision;
  p.minAngle = header.minAngle;
  p.depth = header.depth;
  p.extZo = header.extZo;
  p.extRo = header.extRo;
  p.extRi = header.extRi;
  p.acSolver = header.acSolver;
  p.gpuAccel = header.gpuAccel;
  p.prevType = header.prevType;
  p.prevSoln = getFixed(header.prevSoln, kPathLen);
  p.comment = getFixed(header.comment, kCommentLen);

  QVector<FemxPointPropRecord> pointRecs;
  if (!readArray(file, pointRecs, header.pointPropCount, errorMessage, femxPath))
    return false;
  for (const auto& r : pointRecs) {
    FemmPointProp pp;
    pp.name = getFixed(r.name, kNameLen);
    pp.Jr = r.Jr;
    pp.Ji = r.Ji;
    pp.Ar = r.Ar;
    pp.Ai = r.Ai;
    p.pointProps.push_back(pp);
  }

  QVector<FemxBoundaryPropRecord> bdryRecs;
  if (!readArray(file, bdryRecs, header.boundaryPropCount, errorMessage, femxPath))
    return false;
  for (const auto& r : bdryRecs) {
    FemmBoundaryProp b;
    b.name = getFixed(r.name, kNameLen);
    b.bdryFormat = r.bdryFormat;
    b.A0 = r.A0;
    b.A1 = r.A1;
    b.A2 = r.A2;
    b.phi = r.phi;
    b.c0re = r.c0re;
    b.c0im = r.c0im;
    b.c1re = r.c1re;
    b.c1im = r.c1im;
    b.muSsd = r.muSsd;
    b.sigmaSsd = r.sigmaSsd;
    b.innerAngle = r.innerAngle;
    b.outerAngle = r.outerAngle;
    p.boundaryProps.push_back(b);
  }

  QVector<FemxMaterialPropRecord> matRecs;
  if (!readArray(file, matRecs, header.materialPropCount, errorMessage, femxPath))
    return false;
  for (const auto& r : matRecs) {
    FemmMaterialProp m;
    m.name = getFixed(r.name, kNameLen);
    m.muX = r.muX;
    m.muY = r.muY;
    m.Hc = r.Hc;
    m.HcAngle = r.HcAngle;
    m.JsrcRe = r.JsrcRe;
    m.JsrcIm = r.JsrcIm;
    m.sigma = r.sigma;
    m.dLam = r.dLam;
    m.phiH = r.phiH;
    m.phiHx = r.phiHx;
    m.phiHy = r.phiHy;
    m.lamType = r.lamType;
    m.nStrands = r.nStrands;
    m.lamFill = r.lamFill;
    m.wireD = r.wireD;
    for (int i = 0; i < r.bhPointCount && i < kMaxBhPoints; i++)
      m.bhData.push_back({ r.bhData[i][0], r.bhData[i][1] });
    p.materialProps.push_back(m);
  }

  QVector<FemxCircuitPropRecord> circRecs;
  if (!readArray(file, circRecs, header.circuitPropCount, errorMessage, femxPath))
    return false;
  for (const auto& r : circRecs) {
    FemmCircuitProp c;
    c.name = getFixed(r.name, kNameLen);
    c.ampsRe = r.ampsRe;
    c.ampsIm = r.ampsIm;
    c.circType = r.circType;
    p.circuitProps.push_back(c);
  }

  QVector<FemxNodeRecord> nodeRecs;
  if (!readArray(file, nodeRecs, header.nodeCount, errorMessage, femxPath))
    return false;
  p.nodes.resize(nodeRecs.size());
  for (int i = 0; i < nodeRecs.size(); i++)
    p.nodes[i] = { nodeRecs[i].x, nodeRecs[i].y, nodeRecs[i].boundaryMarker, nodeRecs[i].inGroup, false };

  QVector<FemxSegmentRecord> segRecs;
  if (!readArray(file, segRecs, header.segmentCount, errorMessage, femxPath))
    return false;
  p.segments.resize(segRecs.size());
  for (int i = 0; i < segRecs.size(); i++)
    p.segments[i] = { segRecs[i].n0, segRecs[i].n1, segRecs[i].maxSideLength, segRecs[i].boundaryMarker, segRecs[i].hidden != 0, segRecs[i].inGroup, false };

  QVector<FemxArcRecord> arcRecs;
  if (!readArray(file, arcRecs, header.arcCount, errorMessage, femxPath))
    return false;
  p.arcSegments.resize(arcRecs.size());
  for (int i = 0; i < arcRecs.size(); i++)
    p.arcSegments[i] = { arcRecs[i].n0, arcRecs[i].n1, arcRecs[i].arcLength, arcRecs[i].maxSideLength, arcRecs[i].boundaryMarker, arcRecs[i].hidden != 0, arcRecs[i].inGroup, arcRecs[i].mySideLength, false };

  QVector<FemxBlockLabelRecord> blockRecs;
  if (!readArray(file, blockRecs, header.blockLabelCount, errorMessage, femxPath))
    return false;
  for (const auto& r : blockRecs) {
    FemmBlockLabel b;
    b.x = r.x;
    b.y = r.y;
    b.blockTypeIndex = r.blockTypeIndex;
    b.maxArea = r.maxArea;
    b.circuitIndex = r.circuitIndex;
    b.magDir = r.magDir;
    b.magDirFctn = getFixed(r.magDirFctn, kMagDirFctnLen);
    b.inGroup = r.inGroup;
    b.turns = r.turns;
    b.isExternal = r.isExternal != 0;
    b.isDefault = r.isDefault != 0;
    p.blockLabels.push_back(b);
  }

  return true;
}
