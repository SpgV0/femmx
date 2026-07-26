#define _USE_MATH_DEFINES

#include "HeatFileIO.h"

#include "FemmProblem.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include <cmath>

namespace {

// Duplicated from FemmFileIO.cpp -- see FemmProblem.h's header comment on
// why each file-format module is self-contained rather than sharing a
// parsing utility header (matches this codebase's existing convention,
// see also MeshBuilder.cpp/MaterialLibraryIO.cpp's own copies).
QString g17(double v)
{
  return QString::number(v, 'g', 17);
}

QString unquote(const QString& s)
{
  QString t = s.trimmed();
  if (t.length() >= 2 && t.startsWith('"') && t.endsWith('"'))
    return t.mid(1, t.length() - 2);
  return t;
}

bool splitTagValue(const QString& line, QString& tag, QString& value)
{
  int eq = line.indexOf('=');
  if (eq < 0)
    return false;
  tag = line.left(eq).trimmed();
  if (tag.length() >= 2 && ((tag.front() == '[' && tag.back() == ']') || (tag.front() == '<' && tag.back() == '>')))
    tag = tag.mid(1, tag.length() - 2);
  value = line.mid(eq + 1).trimmed();
  return true;
}

QVector<QString> splitFields(const QString& line)
{
  static const QRegularExpression ws("\\s+");
  QVector<QString> out;
  for (const QString& tok : line.trimmed().split(ws, Qt::SkipEmptyParts))
    out.push_back(tok);
  return out;
}

} // namespace

bool HeatFileIO::readFeh(const QString& path, FemmProblem& problem, QString& errorMessage)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    errorMessage = QStringLiteral("Could not open \"%1\" for reading.").arg(path);
    return false;
  }

  problem = FemmProblem();

  QTextStream in(&file);
  QString line;
  QString tag, value;

  auto nextLine = [&]() -> bool {
    if (in.atEnd())
      return false;
    line = in.readLine();
    return true;
  };

  while (nextLine()) {
    if (line.trimmed().isEmpty())
      continue;
    if (!splitTagValue(line, tag, value))
      continue;

    if (tag == "Format" || tag == "dT") {
      // Format: this reader targets the one .feh format version
      // HDRAWDOC.CPP's writer produces (1). dT: transient heat-flow time
      // step -- femmqt doesn't support transient analysis, steady-state
      // only, matching hsolv.exe's CUDA-ported solve path.
    } else if (tag == "Precision") {
      problem.precision = value.toDouble();
    } else if (tag == "GPUAccel") {
      problem.gpuAccel = value.toInt();
    } else if (tag == "MinAngle") {
      problem.minAngle = value.toDouble();
    } else if (tag == "DoSmartMesh") {
      problem.smartMesh = value.toInt() != 0;
    } else if (tag == "Depth") {
      problem.depth = value.toDouble();
    } else if (tag == "LengthUnits") {
      const QString v = value.trimmed().toLower();
      if (v == "millimeters")
        problem.lengthUnits = FemmLengthUnits::Millimeters;
      else if (v == "centimeters")
        problem.lengthUnits = FemmLengthUnits::Centimeters;
      else if (v == "meters")
        problem.lengthUnits = FemmLengthUnits::Meters;
      else if (v == "mils")
        problem.lengthUnits = FemmLengthUnits::Mils;
      else if (v == "microns")
        problem.lengthUnits = FemmLengthUnits::Microns;
      else
        problem.lengthUnits = FemmLengthUnits::Inches;
    } else if (tag == "ProblemType") {
      problem.problemType = (value.trimmed().toLower() == "axisymmetric") ? FemmCoordinateType::Axisymmetric : FemmCoordinateType::Planar;
    } else if (tag == "extZo") {
      problem.extZo = value.toDouble();
    } else if (tag == "extRo") {
      problem.extRo = value.toDouble();
    } else if (tag == "extRi") {
      problem.extRi = value.toDouble();
    } else if (tag == "Coordinates") {
      problem.coordsPolar = (value.trimmed().toLower() == "polar");
    } else if (tag == "PrevSoln") {
      problem.prevSoln = unquote(value);
    } else if (tag == "Comment") {
      problem.comment = unquote(value);
    } else if (tag == "PointProps") {
      int n = value.toInt();
      for (int i = 0; i < n; i++) {
        FemmThermalPointProp p;
        while (nextLine()) {
          if (line.trimmed() == "<EndPoint>")
            break;
          QString t2, v2;
          if (!splitTagValue(line, t2, v2))
            continue;
          if (t2 == "BeginPoint")
            continue;
          if (t2 == "PointName")
            p.name = unquote(v2);
          else if (t2 == "Tp")
            p.Tp = v2.toDouble();
          else if (t2 == "qp")
            p.qp = v2.toDouble();
        }
        problem.thermalPointProps.push_back(p);
      }
    } else if (tag == "BdryProps") {
      int n = value.toInt();
      for (int i = 0; i < n; i++) {
        FemmThermalBoundaryProp b;
        while (nextLine()) {
          if (line.trimmed() == "<EndBdry>")
            break;
          QString t2, v2;
          if (!splitTagValue(line, t2, v2))
            continue;
          if (t2 == "BeginBdry")
            continue;
          if (t2 == "BdryName")
            b.name = unquote(v2);
          else if (t2 == "BdryType")
            b.bdryFormat = v2.toInt();
          else if (t2 == "Tset")
            b.Tset = v2.toDouble();
          else if (t2 == "qs")
            b.qs = v2.toDouble();
          else if (t2 == "beta")
            b.beta = v2.toDouble();
          else if (t2 == "h")
            b.h = v2.toDouble();
          else if (t2 == "Tinf")
            b.Tinf = v2.toDouble();
          else if (t2 == "TinfRad")
            b.TinfRad = v2.toDouble();
        }
        problem.thermalBoundaryProps.push_back(b);
      }
    } else if (tag == "BlockProps") {
      int n = value.toInt();
      for (int i = 0; i < n; i++) {
        FemmThermalMaterialProp m;
        while (nextLine()) {
          if (line.trimmed() == "<EndBlock>")
            break;
          QString t2, v2;
          if (!splitTagValue(line, t2, v2))
            continue;
          if (t2 == "BeginBlock")
            continue;
          if (t2 == "BlockName")
            m.name = unquote(v2);
          else if (t2 == "Kx")
            m.Kx = v2.toDouble();
          else if (t2 == "Ky")
            m.Ky = v2.toDouble();
          else if (t2 == "Kt")
            m.Kt = v2.toDouble();
          else if (t2 == "qv")
            m.qv = v2.toDouble();
          else if (t2 == "TKPoints") {
            int n2 = v2.toInt();
            for (int k = 0; k < n2 && nextLine(); k++) {
              QVector<QString> f = splitFields(line);
              if (f.size() >= 2)
                m.tkData.push_back({ f[0].toDouble(), f[1].toDouble() });
            }
          }
        }
        problem.thermalMaterialProps.push_back(m);
      }
    } else if (tag == "ConductorProps") {
      int n = value.toInt();
      for (int i = 0; i < n; i++) {
        FemmThermalConductorProp c;
        while (nextLine()) {
          if (line.trimmed() == "<EndConductor>")
            break;
          QString t2, v2;
          if (!splitTagValue(line, t2, v2))
            continue;
          if (t2 == "BeginConductor")
            continue;
          if (t2 == "ConductorName")
            c.name = unquote(v2);
          else if (t2 == "Tc")
            c.Tc = v2.toDouble();
          else if (t2 == "qc")
            c.qc = v2.toDouble();
          else if (t2 == "ConductorType")
            c.circType = v2.toInt();
        }
        problem.thermalConductorProps.push_back(c);
      }
    } else if (tag == "NumPoints") {
      int n = value.toInt();
      for (int i = 0; i < n && nextLine(); i++) {
        QVector<QString> f = splitFields(line);
        if (f.size() < 4)
          continue;
        FemmNode node;
        node.x = f[0].toDouble();
        node.y = f[1].toDouble();
        node.thermalPointPropIndex = f[2].toInt();
        node.inGroup = f[3].toInt();
        node.thermalConductorIndex = (f.size() >= 5) ? f[4].toInt() : 0;
        problem.nodes.push_back(node);
      }
    } else if (tag == "NumSegments") {
      int n = value.toInt();
      for (int i = 0; i < n && nextLine(); i++) {
        QVector<QString> f = splitFields(line);
        if (f.size() < 5)
          continue;
        FemmSegment seg;
        seg.n0 = f[0].toInt();
        seg.n1 = f[1].toInt();
        seg.maxSideLength = f[2].toDouble();
        seg.thermalBoundaryMarker = f[3].toInt();
        seg.hidden = f[4].toInt() != 0;
        seg.inGroup = (f.size() >= 6) ? f[5].toInt() : 0;
        seg.thermalConductorIndex = (f.size() >= 7) ? f[6].toInt() : 0;
        problem.segments.push_back(seg);
      }
    } else if (tag == "NumArcSegments") {
      int n = value.toInt();
      for (int i = 0; i < n && nextLine(); i++) {
        QVector<QString> f = splitFields(line);
        if (f.size() < 7)
          continue;
        FemmArcSegment arc;
        arc.n0 = f[0].toInt();
        arc.n1 = f[1].toInt();
        arc.arcLength = f[2].toDouble();
        arc.maxSideLength = f[3].toDouble();
        arc.thermalBoundaryMarker = f[4].toInt();
        arc.hidden = f[5].toInt() != 0;
        arc.inGroup = f[6].toInt();
        arc.thermalConductorIndex = (f.size() >= 8) ? f[7].toInt() : 0;
        arc.mySideLength = (f.size() >= 9) ? f[8].toDouble() : arc.maxSideLength;
        problem.arcSegments.push_back(arc);
      }
    } else if (tag == "NumHoles") {
      int n = value.toInt();
      for (int i = 0; i < n && nextLine(); i++) {
        QVector<QString> f = splitFields(line);
        if (f.size() < 3)
          continue;
        FemmBlockLabel hole;
        hole.x = f[0].toDouble();
        hole.y = f[1].toDouble();
        hole.blockTypeIndex = -1;
        hole.thermalBlockTypeIndex = -1;
        hole.inGroup = f[2].toInt();
        problem.blockLabels.push_back(hole);
      }
    } else if (tag == "NumBlockLabels") {
      int n = value.toInt();
      for (int i = 0; i < n && nextLine(); i++) {
        QVector<QString> f = splitFields(line);
        if (f.size() < 6)
          continue;
        FemmBlockLabel lbl;
        lbl.x = f[0].toDouble();
        lbl.y = f[1].toDouble();
        lbl.thermalBlockTypeIndex = f[2].toInt();
        double sideLen = f[3].toDouble();
        lbl.maxArea = (sideLen > 0) ? (M_PI * sideLen * sideLen / 4.0) : 0.0;
        lbl.inGroup = f[4].toInt();
        if (f.size() >= 6) {
          int flags = f[5].toInt();
          lbl.isExternal = (flags & 1) != 0;
          lbl.isDefault = (flags & 2) != 0;
        }
        problem.blockLabels.push_back(lbl);
      }
    }
    // Unknown tags are silently skipped, matching FemmFileIO::readFem's
    // own scope -- nothing downstream depends on round-tripping fields
    // this reader doesn't know about yet.
  }

  return true;
}

bool HeatFileIO::writeFeh(const QString& path, const FemmProblem& p, QString& errorMessage)
{
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    errorMessage = QStringLiteral("Could not open \"%1\" for writing.").arg(path);
    return false;
  }
  QTextStream out(&file);

  out << "[Format]      =  1\n";
  out << "[Precision]   =  " << g17(p.precision) << "\n";
  out << "[GPUAccel]    =  " << p.gpuAccel << "\n";
  out << "[MinAngle]    =  " << g17(p.minAngle) << "\n";
  out << "[DoSmartMesh] =  " << (p.smartMesh ? 1 : 0) << "\n";
  out << "[Depth]       =  " << g17(p.depth) << "\n";
  out << "[LengthUnits] =  ";
  switch (p.lengthUnits) {
  case FemmLengthUnits::Millimeters: out << "millimeters\n"; break;
  case FemmLengthUnits::Centimeters: out << "centimeters\n"; break;
  case FemmLengthUnits::Meters: out << "meters\n"; break;
  case FemmLengthUnits::Mils: out << "mils\n"; break;
  case FemmLengthUnits::Microns: out << "microns\n"; break;
  default: out << "inches\n"; break;
  }

  if (p.problemType == FemmCoordinateType::Planar) {
    out << "[ProblemType] =  planar\n";
  } else {
    out << "[ProblemType] =  axisymmetric\n";
    if (p.extRo != 0 && p.extRi != 0) {
      out << "[extZo] = " << g17(p.extZo) << "\n";
      out << "[extRo] = " << g17(p.extRo) << "\n";
      out << "[extRi] = " << g17(p.extRi) << "\n";
    }
  }

  out << "[Coordinates] =  " << (p.coordsPolar ? "polar" : "cartesian") << "\n";
  out << "[PrevSoln] = \"" << p.prevSoln << "\"\n";
  out << "[dT] = 0\n"; // transient time step -- not supported, matches ChdrawDoc's own default
  out << "[Comment]     =  \"" << p.comment << "\"\n";

  out << "[PointProps]   = " << p.thermalPointProps.size() << "\n";
  for (const FemmThermalPointProp& pp : p.thermalPointProps) {
    out << "  <BeginPoint>\n";
    out << "    <PointName> = \"" << pp.name << "\"\n";
    out << "    <Tp> = " << g17(pp.Tp) << "\n";
    out << "    <qp> = " << g17(pp.qp) << "\n";
    out << "  <EndPoint>\n";
  }

  out << "[BdryProps]   = " << p.thermalBoundaryProps.size() << "\n";
  for (const FemmThermalBoundaryProp& b : p.thermalBoundaryProps) {
    out << "  <BeginBdry>\n";
    out << "    <BdryName> = \"" << b.name << "\"\n";
    out << "    <BdryType> = " << b.bdryFormat << "\n";
    out << "    <Tset> = " << g17(b.Tset) << "\n";
    out << "    <qs>   = " << g17(b.qs) << "\n";
    out << "    <beta> = " << g17(b.beta) << "\n";
    out << "    <h>    = " << g17(b.h) << "\n";
    out << "    <Tinf> = " << g17(b.Tinf) << "\n";
    out << "    <TinfRad> = " << g17(b.TinfRad) << "\n";
    out << "  <EndBdry>\n";
  }

  out << "[BlockProps]  = " << p.thermalMaterialProps.size() << "\n";
  for (const FemmThermalMaterialProp& m : p.thermalMaterialProps) {
    out << "  <BeginBlock>\n";
    out << "    <BlockName> = \"" << m.name << "\"\n";
    out << "    <Kx> = " << g17(m.Kx) << "\n";
    out << "    <Ky> = " << g17(m.Ky) << "\n";
    out << "    <Kt> = " << g17(m.Kt) << "\n";
    out << "    <qv> = " << g17(m.qv) << "\n";
    if (!m.tkData.isEmpty()) {
      out << "    <TKPoints> = " << m.tkData.size() << "\n";
      for (const auto& pt : m.tkData)
        out << "      " << g17(pt.first) << "\t" << g17(pt.second) << "\n";
    }
    out << "  <EndBlock>\n";
  }

  out << "[ConductorProps]  = " << p.thermalConductorProps.size() << "\n";
  for (const FemmThermalConductorProp& c : p.thermalConductorProps) {
    out << "  <BeginConductor>\n";
    out << "    <ConductorName> = \"" << c.name << "\"\n";
    out << "    <Tc> = " << g17(c.Tc) << "\n";
    out << "    <qc> = " << g17(c.qc) << "\n";
    out << "    <ConductorType> = " << c.circType << "\n";
    out << "  <EndConductor>\n";
  }

  out << "[NumPoints] = " << p.nodes.size() << "\n";
  for (const FemmNode& n : p.nodes)
    out << g17(n.x) << "\t" << g17(n.y) << "\t" << n.thermalPointPropIndex << "\t" << n.inGroup << "\t" << n.thermalConductorIndex << "\n";

  out << "[NumSegments] = " << p.segments.size() << "\n";
  for (const FemmSegment& s : p.segments) {
    out << s.n0 << "\t" << s.n1 << "\t";
    if (s.maxSideLength < 0)
      out << "-1\t";
    else
      out << g17(s.maxSideLength) << "\t";
    out << s.thermalBoundaryMarker << "\t" << (s.hidden ? 1 : 0) << "\t" << s.inGroup << "\t" << s.thermalConductorIndex << "\n";
  }

  out << "[NumArcSegments] = " << p.arcSegments.size() << "\n";
  for (const FemmArcSegment& a : p.arcSegments) {
    out << a.n0 << "\t" << a.n1 << "\t" << g17(a.arcLength) << "\t" << g17(a.maxSideLength) << "\t"
        << a.thermalBoundaryMarker << "\t" << (a.hidden ? 1 : 0) << "\t" << a.inGroup << "\t"
        << a.thermalConductorIndex << "\t" << g17(a.mySideLength) << "\n";
  }

  int holeCount = 0;
  for (const FemmBlockLabel& b : p.blockLabels)
    if (b.thermalBlockTypeIndex < 0)
      holeCount++;
  out << "[NumHoles] = " << holeCount << "\n";
  for (const FemmBlockLabel& b : p.blockLabels)
    if (b.thermalBlockTypeIndex < 0)
      out << g17(b.x) << "\t" << g17(b.y) << "\t" << b.inGroup << "\n";

  out << "[NumBlockLabels] = " << (p.blockLabels.size() - holeCount) << "\n";
  for (const FemmBlockLabel& b : p.blockLabels) {
    if (b.thermalBlockTypeIndex < 0)
      continue;
    out << g17(b.x) << "\t" << g17(b.y) << "\t" << b.thermalBlockTypeIndex << "\t";
    if (b.maxArea > 0)
      out << g17(std::sqrt(4.0 * b.maxArea / M_PI)) << "\t";
    else
      out << "-1\t";
    out << b.inGroup << "\t" << ((b.isExternal ? 1 : 0) + (b.isDefault ? 2 : 0)) << "\n";
  }

  return true;
}
