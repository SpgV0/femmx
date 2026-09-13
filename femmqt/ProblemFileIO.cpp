#define _USE_MATH_DEFINES

#include "ProblemFileIO.h"

#include "FemmProblem.h"
#include "FemmTextFormat.h"
#include "FemmProblemEdit.h"
#include "ProblemKind.h"
#include "PropertyCodec.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <cmath>

using FemmTextFormat::g17;
using FemmTextFormat::splitFields;
using FemmTextFormat::splitTagValue;
using FemmTextFormat::unquote;

namespace {

// Magnetics is the odd one out for geometry: no conductor column, and a
// much wider block-label row. Asking the question this way round, once,
// keeps the difference in one place instead of four.
bool usesConductors(FemmProblemKind kind)
{
  return kind != FemmProblemKind::Magnetics;
}

// --- reading ---------------------------------------------------------------

void readNodes(FemmProblem& p, int n, const PropertyCodec::LineReader& next)
{
  QString line;
  const bool conductors = usesConductors(p.kind);
  for (int i = 0; i < n && next(line); i++) {
    const QVector<QString> f = splitFields(line);
    if (f.size() < 4)
      continue;
    FemmNode node;
    node.x = f[0].toDouble();
    node.y = f[1].toDouble();
    node.pointPropIndex = f[2].toInt();
    node.inGroup = f[3].toInt();
    if (conductors && f.size() >= 5)
      node.conductorIndex = f[4].toInt();
    p.nodes.push_back(node);
  }
}

void readSegments(FemmProblem& p, int n, const PropertyCodec::LineReader& next)
{
  QString line;
  const bool conductors = usesConductors(p.kind);
  for (int i = 0; i < n && next(line); i++) {
    const QVector<QString> f = splitFields(line);
    if (f.size() < 5)
      continue;
    FemmSegment seg;
    seg.n0 = f[0].toInt();
    seg.n1 = f[1].toInt();
    seg.maxSideLength = f[2].toDouble();
    seg.boundaryMarker = f[3].toInt();
    seg.hidden = f[4].toInt() != 0;
    seg.inGroup = (f.size() >= 6) ? f[5].toInt() : 0;
    if (conductors && f.size() >= 7)
      seg.conductorIndex = f[6].toInt();
    p.segments.push_back(seg);
  }
}

void readArcs(FemmProblem& p, int n, const PropertyCodec::LineReader& next)
{
  QString line;
  const bool conductors = usesConductors(p.kind);
  for (int i = 0; i < n && next(line); i++) {
    const QVector<QString> f = splitFields(line);
    if (f.size() < 7)
      continue;
    FemmArcSegment arc;
    arc.n0 = f[0].toInt();
    arc.n1 = f[1].toInt();
    arc.arcLength = f[2].toDouble();
    arc.maxSideLength = f[3].toDouble();
    arc.boundaryMarker = f[4].toInt();
    arc.hidden = f[5].toInt() != 0;
    arc.inGroup = f[6].toInt();
    // The conductor column sits BETWEEN inGroup and mySideLength in the
    // three formats that have it, so mySideLength moves along by one.
    if (conductors) {
      if (f.size() >= 8)
        arc.conductorIndex = f[7].toInt();
      arc.mySideLength = (f.size() >= 9) ? f[8].toDouble() : arc.maxSideLength;
    } else {
      arc.mySideLength = (f.size() >= 8) ? f[7].toDouble() : arc.maxSideLength;
    }
    p.arcSegments.push_back(arc);
  }
}

void readHoles(FemmProblem& p, int n, const PropertyCodec::LineReader& next)
{
  QString line;
  for (int i = 0; i < n && next(line); i++) {
    const QVector<QString> f = splitFields(line);
    if (f.size() < 3)
      continue;
    FemmBlockLabel hole;
    hole.x = f[0].toDouble();
    hole.y = f[1].toDouble();
    hole.blockTypeIndex = -1; // a hole IS a block label with no material
    hole.inGroup = f[2].toInt();
    p.blockLabels.push_back(hole);
  }
}

void readBlockLabels(FemmProblem& p, int n, const PropertyCodec::LineReader& next)
{
  QString line;
  for (int i = 0; i < n && next(line); i++) {
    const QVector<QString> f = splitFields(line);
    if (f.size() < 5)
      continue;
    FemmBlockLabel lbl;
    lbl.x = f[0].toDouble();
    lbl.y = f[1].toDouble();
    lbl.blockTypeIndex = f[2].toInt();
    // Stored on disk as the side length of a square of the same area.
    const double sideLen = f[3].toDouble();
    lbl.maxArea = (sideLen > 0) ? (M_PI * sideLen * sideLen / 4.0) : 0.0;

    if (!usesConductors(p.kind)) {
      // .fem: circuit, MagDir, group, turns, flags, optional function.
      if (f.size() < 8)
        return;
      lbl.circuitIndex = f[4].toInt();
      lbl.magDir = f[5].toDouble();
      lbl.inGroup = f[6].toInt();
      lbl.turns = f[7].toInt();
      if (f.size() >= 9) {
        const int flags = f[8].toInt();
        lbl.isExternal = (flags & 1) != 0;
        lbl.isDefault = (flags & 2) != 0;
      }
      // A trailing quoted MagDirFctn may itself contain spaces, so it is
      // taken from the raw line rather than the split tokens.
      const int q0 = line.indexOf('"');
      if (q0 >= 0) {
        const int q1 = line.indexOf('"', q0 + 1);
        if (q1 > q0)
          lbl.magDirFctn = line.mid(q0 + 1, q1 - q0 - 1);
      }
    } else {
      // .fee/.feh/.fec: group and flags only. No circuit, no MagDir, no
      // turns -- those are magnetics ideas.
      lbl.inGroup = f[4].toInt();
      if (f.size() >= 6) {
        const int flags = f[5].toInt();
        lbl.isExternal = (flags & 1) != 0;
        lbl.isDefault = (flags & 2) != 0;
      }
    }
    p.blockLabels.push_back(lbl);
  }
}

// --- writing ---------------------------------------------------------------

void writeGeometry(const FemmProblem& p, QTextStream& out)
{
  const bool conductors = usesConductors(p.kind);

  out << "[NumPoints] = " << p.nodes.size() << "\n";
  for (const FemmNode& n : p.nodes) {
    out << g17(n.x) << "\t" << g17(n.y) << "\t" << n.pointPropIndex << "\t" << n.inGroup;
    if (conductors)
      out << "\t" << n.conductorIndex;
    out << "\n";
  }

  out << "[NumSegments] = " << p.segments.size() << "\n";
  for (const FemmSegment& s : p.segments) {
    out << s.n0 << "\t" << s.n1 << "\t";
    if (s.maxSideLength < 0)
      out << "-1\t";
    else
      out << g17(s.maxSideLength) << "\t";
    out << s.boundaryMarker << "\t" << (s.hidden ? 1 : 0) << "\t" << s.inGroup;
    if (conductors)
      out << "\t" << s.conductorIndex;
    out << "\n";
  }

  out << "[NumArcSegments] = " << p.arcSegments.size() << "\n";
  for (const FemmArcSegment& a : p.arcSegments) {
    out << a.n0 << "\t" << a.n1 << "\t" << g17(a.arcLength) << "\t" << g17(a.maxSideLength)
        << "\t" << a.boundaryMarker << "\t" << (a.hidden ? 1 : 0) << "\t" << a.inGroup;
    if (conductors)
      out << "\t" << a.conductorIndex;
    out << "\t" << g17(a.mySideLength) << "\n";
  }

  int holeCount = 0;
  for (const FemmBlockLabel& b : p.blockLabels)
    if (b.blockTypeIndex < 0)
      holeCount++;
  out << "[NumHoles] = " << holeCount << "\n";
  for (const FemmBlockLabel& b : p.blockLabels)
    if (b.blockTypeIndex < 0)
      out << g17(b.x) << "\t" << g17(b.y) << "\t" << b.inGroup << "\n";

  out << "[NumBlockLabels] = " << (p.blockLabels.size() - holeCount) << "\n";
  for (const FemmBlockLabel& b : p.blockLabels) {
    if (b.blockTypeIndex < 0)
      continue;
    out << g17(b.x) << "\t" << g17(b.y) << "\t" << b.blockTypeIndex << "\t";
    if (b.maxArea > 0)
      out << g17(std::sqrt(4.0 * b.maxArea / M_PI)) << "\t";
    else
      out << "-1\t";
    const int flags = (b.isExternal ? 1 : 0) + (b.isDefault ? 2 : 0);
    if (!conductors) {
      out << b.circuitIndex << "\t" << g17(b.magDir) << "\t" << b.inGroup << "\t"
          << b.turns << "\t" << flags;
      if (!b.magDirFctn.isEmpty())
        out << "\t\"" << b.magDirFctn << "\"";
    } else {
      out << b.inGroup << "\t" << flags;
    }
    out << "\n";
  }
}

} // namespace

// ---------------------------------------------------------------------------

bool ProblemFileIO::readAs(const QString& path, FemmProblemKind kind,
    FemmProblem& problem, QString& errorMessage)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    errorMessage = QStringLiteral("Could not open \"%1\" for reading.").arg(path);
    return false;
  }

  problem = FemmProblem();
  problem.kind = kind;

  QTextStream in(&file);
  QString line;

  const PropertyCodec::LineReader next = [&in](QString& out) -> bool {
    if (in.atEnd())
      return false;
    out = in.readLine();
    return true;
  };

  while (next(line)) {
    if (line.trimmed().isEmpty())
      continue;
    QString tag, value;
    if (!splitTagValue(line, tag, value))
      continue;

    // The header scalars common to all four formats.
    if (tag == "Format") {
      // Ignored: this reader targets the format version this repo's
      // writers produce, with no older-version shims.
    } else if (tag == "Precision") {
      problem.precision = value.toDouble();
    } else if (tag == "MinAngle") {
      problem.minAngle = value.toDouble();
    } else if (tag == "DoSmartMesh") {
      problem.smartMesh = value.toInt() != 0;
    } else if (tag == "Depth") {
      problem.depth = value.toDouble();
    } else if (tag == "LengthUnits") {
      // Spellings and fallback exactly as FemmFileIO has always read
      // them: anything unrecognised is inches, which is what the format
      // itself defaults to.
      const QString u = value.trimmed().toLower();
      if (u == "millimeters") problem.lengthUnits = FemmLengthUnits::Millimeters;
      else if (u == "centimeters") problem.lengthUnits = FemmLengthUnits::Centimeters;
      else if (u == "meters") problem.lengthUnits = FemmLengthUnits::Meters;
      else if (u == "mils") problem.lengthUnits = FemmLengthUnits::Mils;
      else if (u == "microns") problem.lengthUnits = FemmLengthUnits::Microns;
      else problem.lengthUnits = FemmLengthUnits::Inches;
    } else if (tag == "ProblemType") {
      problem.problemType = (value.trimmed().toLower() == "axisymmetric")
          ? FemmCoordinateType::Axisymmetric
          : FemmCoordinateType::Planar;
    } else if (tag == "extZo") {
      problem.extZo = value.toDouble();
    } else if (tag == "extRo") {
      problem.extRo = value.toDouble();
    } else if (tag == "extRi") {
      problem.extRi = value.toDouble();
    } else if (tag == "Coordinates") {
      problem.coordsPolar = value.trimmed().toLower().startsWith("polar");
    } else if (tag == "GPUAccel") {
      problem.gpuAccel = value.toInt();
    } else if (tag == "Comment") {
      problem.comment = unquote(value);
    } else if (tag == "NumPoints") {
      readNodes(problem, value.toInt(), next);
    } else if (tag == "NumSegments") {
      readSegments(problem, value.toInt(), next);
    } else if (tag == "NumArcSegments") {
      readArcs(problem, value.toInt(), next);
    } else if (tag == "NumHoles") {
      readHoles(problem, value.toInt(), next);
    } else if (tag == "NumBlockLabels") {
      readBlockLabels(problem, value.toInt(), next);
    } else if (!PropertyCodec::readSection(problem, tag, value.toInt(), next)) {
      // Not geometry, not a shared scalar and not a property section for
      // this kind -- try it as one of the kind's own header scalars, and
      // ignore it if it is not that either (a future format addition).
      PropertyCodec::readKindScalar(problem, tag, value);
    }
  }

  return true;
}

bool ProblemFileIO::read(const QString& path, FemmProblem& problem, QString& errorMessage)
{
  FemmProblemKind kind = FemmProblemKind::Magnetics;
  if (!ProblemKind::kindForPath(path, kind)) {
    errorMessage = QStringLiteral(
        "\"%1\" is not a FEMM model file. Expected .fem (magnetics), .fee "
        "(electrostatics), .feh (heat flow) or .fec (current flow).")
                       .arg(QFileInfo(path).fileName());
    return false;
  }
  return readAs(path, kind, problem, errorMessage);
}

namespace {
bool writeStripped(const QString& path, const FemmProblem& p, QString& errorMessage);
}

bool ProblemFileIO::write(const QString& path, const FemmProblem& p, QString& errorMessage)
{
  // Construction geometry (issue #31) never leaves femmqt, in ANY of the
  // four formats. This strip used to live in FemmFileIO::writeFem; when
  // that became a forward to this function it came with it, because this
  // is now the only writer and a centreline reaching a solver is a
  // material boundary that cuts the region it crosses in two.
  //
  // Caught by tst_construction when the forward landed without it, which
  // is the whole reason those tests check the bytes that leave rather
  // than the flag.
  if (FemmProblemEdit::hasConstruction(p)) {
    const FemmProblem forExport = FemmProblemEdit::withoutConstruction(p);
    return writeStripped(path, forExport, errorMessage);
  }
  return writeStripped(path, p, errorMessage);
}

namespace {
bool writeStripped(const QString& path, const FemmProblem& p, QString& errorMessage)
{
  FemmProblemKind pathKind = FemmProblemKind::Magnetics;
  if (ProblemKind::kindForPath(path, pathKind) && pathKind != p.kind) {
    // Writing one format under another's extension produces a file that
    // opens as whatever its name says and loses every property in it.
    errorMessage = QStringLiteral(
        "This is a %1 problem, so it cannot be saved as \"%2\" -- that "
        "extension means %3. Save it as .%4 instead.")
                       .arg(ProblemKind::displayName(p.kind),
                           QFileInfo(path).fileName(),
                           ProblemKind::displayName(pathKind),
                           ProblemKind::extension(p.kind));
    return false;
  }

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    errorMessage = QStringLiteral("Could not open \"%1\" for writing.").arg(path);
    return false;
  }
  QTextStream out(&file);

  out << "[Format] = 4.0\n";
  out << "[Precision] = " << g17(p.precision) << "\n";
  PropertyCodec::writeKindScalars(p, out);
  out << "[MinAngle] = " << g17(p.minAngle) << "\n";
  out << "[DoSmartMesh] = " << (p.smartMesh ? 1 : 0) << "\n";
  out << "[Depth] = " << g17(p.depth) << "\n";
  out << "[LengthUnits] = ";
  switch (p.lengthUnits) {
  case FemmLengthUnits::Millimeters: out << "millimeters\n"; break;
  case FemmLengthUnits::Centimeters: out << "centimeters\n"; break;
  case FemmLengthUnits::Meters: out << "meters\n"; break;
  case FemmLengthUnits::Mils: out << "mils\n"; break;
  case FemmLengthUnits::Microns: out << "microns\n"; break;
  default: out << "inches\n"; break;
  }
  out << "[ProblemType] = "
      << (p.problemType == FemmCoordinateType::Axisymmetric ? "axisymmetric" : "planar") << "\n";
  out << "[Coordinates] = " << (p.coordsPolar ? "polar" : "cartesian") << "\n";
  out << "[extZo] = " << g17(p.extZo) << "\n";
  out << "[extRo] = " << g17(p.extRo) << "\n";
  out << "[extRi] = " << g17(p.extRi) << "\n";
  out << "[GPUAccel] = " << p.gpuAccel << "\n";
  out << "[Comment] = \"" << p.comment << "\"\n";

  PropertyCodec::writeSections(p, out);
  writeGeometry(p, out);
  return true;
}
} // namespace
