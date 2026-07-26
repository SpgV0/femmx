#define _USE_MATH_DEFINES

#include "FemmFileIO.h"

#include "FemmProblem.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include <cmath>

namespace {

// Matches C's "%.17g" closely enough for exact double round-trip (17
// significant digits is always sufficient to reconstruct any IEEE-754
// double bit-for-bit) -- doesn't need to match femm/FemmeDoc.cpp's writer
// byte-for-byte, just needs fkn.exe/triangle.exe/femmx.exe's own %.17g
// readers (which use plain strtod) to parse it back to the same value.
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

// Splits "  <Tag>   =  value" (or "[Tag]  =  value") into tag ("Tag") and
// value ("value", not yet unquoted/trimmed further). Returns false if
// there's no '=' on the line.
bool splitTagValue(const QString& line, QString& tag, QString& value)
{
  int eq = line.indexOf('=');
  if (eq < 0)
    return false;
  tag = line.left(eq).trimmed();
  // strip a single leading/trailing bracket pair: [Tag] or <Tag>
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

bool FemmFileIO::readFem(const QString& path, FemmProblem& problem, QString& errorMessage)
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

    if (tag == "Format") {
      // ignored -- this reader targets the one .fem format version this
      // repo's writer produces (4.0); no older-version compatibility
      // shims, matching this phase's scope.
    } else if (tag == "Frequency") {
      problem.frequency = value.toDouble();
    } else if (tag == "Precision") {
      problem.precision = value.toDouble();
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
    } else if (tag == "ACSolver") {
      problem.acSolver = value.toInt();
    } else if (tag == "GPUAccel") {
      problem.gpuAccel = value.toInt();
    } else if (tag == "PrevType") {
      problem.prevType = value.toInt();
    } else if (tag == "PrevSoln") {
      problem.prevSoln = unquote(value);
    } else if (tag == "Comment") {
      problem.comment = unquote(value);
    } else if (tag == "PointProps") {
      int n = value.toInt();
      for (int i = 0; i < n; i++) {
        FemmPointProp p;
        while (nextLine()) {
          // <BeginPoint>/<EndPoint> etc. are bare tags with no '=', so
          // splitTagValue (which requires one) can't recognize them --
          // check for the closing tag directly, first, or it silently
          // falls through to "continue" below and this loop (and every
          // property-block loop like it) never terminates, swallowing
          // the rest of the file. See the equivalent check in each of
          // the other three property-block loops below.
          if (line.trimmed() == "<EndPoint>")
            break;
          QString t2, v2;
          if (!splitTagValue(line, t2, v2))
            continue;
          if (t2 == "BeginPoint")
            continue;
          if (t2 == "PointName")
            p.name = unquote(v2);
          else if (t2 == "I_re")
            p.Jr = v2.toDouble();
          else if (t2 == "I_im")
            p.Ji = v2.toDouble();
          else if (t2 == "A_re")
            p.Ar = v2.toDouble();
          else if (t2 == "A_im")
            p.Ai = v2.toDouble();
        }
        problem.pointProps.push_back(p);
      }
    } else if (tag == "BdryProps") {
      int n = value.toInt();
      for (int i = 0; i < n; i++) {
        FemmBoundaryProp b;
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
          else if (t2 == "A_0")
            b.A0 = v2.toDouble();
          else if (t2 == "A_1")
            b.A1 = v2.toDouble();
          else if (t2 == "A_2")
            b.A2 = v2.toDouble();
          else if (t2 == "Phi")
            b.phi = v2.toDouble();
          else if (t2 == "c0")
            b.c0re = v2.toDouble();
          else if (t2 == "c0i")
            b.c0im = v2.toDouble();
          else if (t2 == "c1")
            b.c1re = v2.toDouble();
          else if (t2 == "c1i")
            b.c1im = v2.toDouble();
          else if (t2 == "Mu_ssd")
            b.muSsd = v2.toDouble();
          else if (t2 == "Sigma_ssd")
            b.sigmaSsd = v2.toDouble();
          else if (t2 == "innerangle")
            b.innerAngle = v2.toDouble();
          else if (t2 == "outerangle")
            b.outerAngle = v2.toDouble();
        }
        problem.boundaryProps.push_back(b);
      }
    } else if (tag == "BlockProps") {
      int n = value.toInt();
      for (int i = 0; i < n; i++) {
        FemmMaterialProp m;
        int bhPoints = 0;
        while (nextLine()) {
          if (line.trimmed() == "<EndBlock>")
            break;
          QString t2, v2;
          if (!splitTagValue(line, t2, v2)) {
            // BH curve data rows have no '=' -- two whitespace-separated
            // numbers per line, immediately after <BHPoints>.
            continue;
          }
          if (t2 == "BeginBlock")
            continue;
          if (t2 == "BlockName")
            m.name = unquote(v2);
          else if (t2 == "Mu_x")
            m.muX = v2.toDouble();
          else if (t2 == "Mu_y")
            m.muY = v2.toDouble();
          else if (t2 == "H_c")
            m.Hc = v2.toDouble();
          else if (t2 == "H_cAngle")
            m.HcAngle = v2.toDouble();
          else if (t2 == "J_re")
            m.JsrcRe = v2.toDouble();
          else if (t2 == "J_im")
            m.JsrcIm = v2.toDouble();
          else if (t2 == "Sigma")
            m.sigma = v2.toDouble();
          else if (t2 == "d_lam")
            m.dLam = v2.toDouble();
          else if (t2 == "Phi_h")
            m.phiH = v2.toDouble();
          else if (t2 == "Phi_hx")
            m.phiHx = v2.toDouble();
          else if (t2 == "Phi_hy")
            m.phiHy = v2.toDouble();
          else if (t2 == "LamType")
            m.lamType = v2.toInt();
          else if (t2 == "LamFill")
            m.lamFill = v2.toDouble();
          else if (t2 == "NStrands")
            m.nStrands = v2.toInt();
          else if (t2 == "WireD")
            m.wireD = v2.toDouble();
          else if (t2 == "BHPoints") {
            bhPoints = v2.toInt();
            for (int k = 0; k < bhPoints && nextLine(); k++) {
              QVector<QString> f = splitFields(line);
              if (f.size() >= 2)
                m.bhData.push_back({ f[0].toDouble(), f[1].toDouble() });
            }
          }
        }
        problem.materialProps.push_back(m);
      }
    } else if (tag == "CircuitProps") {
      int n = value.toInt();
      for (int i = 0; i < n; i++) {
        FemmCircuitProp c;
        while (nextLine()) {
          if (line.trimmed() == "<EndCircuit>")
            break;
          QString t2, v2;
          if (!splitTagValue(line, t2, v2))
            continue;
          if (t2 == "BeginCircuit")
            continue;
          if (t2 == "CircuitName")
            c.name = unquote(v2);
          else if (t2 == "TotalAmps_re")
            c.ampsRe = v2.toDouble();
          else if (t2 == "TotalAmps_im")
            c.ampsIm = v2.toDouble();
          else if (t2 == "CircuitType")
            c.circType = v2.toInt();
        }
        problem.circuitProps.push_back(c);
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
        node.pointPropIndex = f[2].toInt();
        node.inGroup = f[3].toInt();
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
        seg.boundaryMarker = f[3].toInt();
        seg.hidden = f[4].toInt() != 0;
        seg.inGroup = (f.size() >= 6) ? f[5].toInt() : 0;
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
        arc.boundaryMarker = f[4].toInt();
        arc.hidden = f[5].toInt() != 0;
        arc.inGroup = f[6].toInt();
        arc.mySideLength = (f.size() >= 8) ? f[7].toDouble() : arc.maxSideLength;
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
        hole.inGroup = f[2].toInt();
        problem.blockLabels.push_back(hole);
      }
    } else if (tag == "NumBlockLabels") {
      int n = value.toInt();
      for (int i = 0; i < n && nextLine(); i++) {
        QVector<QString> f = splitFields(line);
        if (f.size() < 8)
          continue;
        FemmBlockLabel lbl;
        lbl.x = f[0].toDouble();
        lbl.y = f[1].toDouble();
        lbl.blockTypeIndex = f[2].toInt();
        double sideLen = f[3].toDouble();
        lbl.maxArea = (sideLen > 0) ? (M_PI * sideLen * sideLen / 4.0) : 0.0;
        lbl.circuitIndex = f[4].toInt();
        lbl.magDir = f[5].toDouble();
        lbl.inGroup = f[6].toInt();
        lbl.turns = f[7].toInt();
        if (f.size() >= 9) {
          int flags = f[8].toInt();
          lbl.isExternal = (flags & 1) != 0;
          lbl.isDefault = (flags & 2) != 0;
        }
        // optional trailing quoted MagDirFctn -- extract from the raw
        // line rather than the whitespace-split tokens, since it may
        // itself contain spaces.
        int q0 = line.indexOf('"');
        if (q0 >= 0) {
          int q1 = line.indexOf('"', q0 + 1);
          if (q1 > q0)
            lbl.magDirFctn = line.mid(q0 + 1, q1 - q0 - 1);
        }
        problem.blockLabels.push_back(lbl);
      }
    }
    // Unknown tags (e.g. a future format addition) are silently skipped --
    // matches this phase's "don't need full fidelity" scope; nothing
    // downstream depends on round-tripping fields this reader doesn't
    // know about yet.
  }

  return true;
}

bool FemmFileIO::writeFem(const QString& path, const FemmProblem& p, QString& errorMessage)
{
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    errorMessage = QStringLiteral("Could not open \"%1\" for writing.").arg(path);
    return false;
  }
  QTextStream out(&file);

  out << "[Format]      =  4.0\n";
  out << "[Frequency]   =  " << g17(p.frequency) << "\n";
  out << "[Precision]   =  " << g17(p.precision) << "\n";
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
  out << "[ACSolver]    =  " << p.acSolver << "\n";
  out << "[GPUAccel]    =  " << p.gpuAccel << "\n";
  out << "[PrevType]    =  " << p.prevType << "\n";
  out << "[PrevSoln]    =  \"" << p.prevSoln << "\"\n";
  out << "[Comment]     =  \"" << p.comment << "\"\n";

  out << "[PointProps]   = " << p.pointProps.size() << "\n";
  for (const FemmPointProp& pp : p.pointProps) {
    out << "  <BeginPoint>\n";
    out << "    <PointName> = \"" << pp.name << "\"\n";
    out << "    <I_re> = " << g17(pp.Jr) << "\n";
    out << "    <I_im> = " << g17(pp.Ji) << "\n";
    out << "    <A_re> = " << g17(pp.Ar) << "\n";
    out << "    <A_im> = " << g17(pp.Ai) << "\n";
    out << "  <EndPoint>\n";
  }

  out << "[BdryProps]   = " << p.boundaryProps.size() << "\n";
  for (const FemmBoundaryProp& b : p.boundaryProps) {
    out << "  <BeginBdry>\n";
    out << "    <BdryName> = \"" << b.name << "\"\n";
    out << "    <BdryType> = " << b.bdryFormat << "\n";
    out << "    <A_0> = " << g17(b.A0) << "\n";
    out << "    <A_1> = " << g17(b.A1) << "\n";
    out << "    <A_2> = " << g17(b.A2) << "\n";
    out << "    <Phi> = " << g17(b.phi) << "\n";
    out << "    <c0> = " << g17(b.c0re) << "\n";
    out << "    <c0i> = " << g17(b.c0im) << "\n";
    out << "    <c1> = " << g17(b.c1re) << "\n";
    out << "    <c1i> = " << g17(b.c1im) << "\n";
    out << "    <Mu_ssd> = " << g17(b.muSsd) << "\n";
    out << "    <Sigma_ssd> = " << g17(b.sigmaSsd) << "\n";
    out << "    <innerangle> = " << g17(b.innerAngle) << "\n";
    out << "    <outerangle> = " << g17(b.outerAngle) << "\n";
    out << "  <EndBdry>\n";
  }

  out << "[BlockProps]  = " << p.materialProps.size() << "\n";
  for (const FemmMaterialProp& m : p.materialProps) {
    out << "  <BeginBlock>\n";
    out << "    <BlockName> = \"" << m.name << "\"\n";
    out << "    <Mu_x> = " << g17(m.muX) << "\n";
    out << "    <Mu_y> = " << g17(m.muY) << "\n";
    out << "    <H_c> = " << g17(m.Hc) << "\n";
    out << "    <H_cAngle> = " << g17(m.HcAngle) << "\n";
    out << "    <J_re> = " << g17(m.JsrcRe) << "\n";
    out << "    <J_im> = " << g17(m.JsrcIm) << "\n";
    out << "    <Sigma> = " << g17(m.sigma) << "\n";
    out << "    <d_lam> = " << g17(m.dLam) << "\n";
    out << "    <Phi_h> = " << g17(m.phiH) << "\n";
    out << "    <Phi_hx> = " << g17(m.phiHx) << "\n";
    out << "    <Phi_hy> = " << g17(m.phiHy) << "\n";
    out << "    <LamType> = " << m.lamType << "\n";
    out << "    <LamFill> = " << g17(m.lamFill) << "\n";
    out << "    <NStrands> = " << m.nStrands << "\n";
    out << "    <WireD> = " << g17(m.wireD) << "\n";
    out << "    <BHPoints> = " << m.bhData.size() << "\n";
    for (const auto& pt : m.bhData)
      out << "      " << g17(pt.first) << "\t" << g17(pt.second) << "\n";
    out << "  <EndBlock>\n";
  }

  out << "[CircuitProps]  = " << p.circuitProps.size() << "\n";
  for (const FemmCircuitProp& c : p.circuitProps) {
    out << "  <BeginCircuit>\n";
    out << "    <CircuitName> = \"" << c.name << "\"\n";
    out << "    <TotalAmps_re> = " << g17(c.ampsRe) << "\n";
    out << "    <TotalAmps_im> = " << g17(c.ampsIm) << "\n";
    out << "    <CircuitType> = " << c.circType << "\n";
    out << "  <EndCircuit>\n";
  }

  out << "[NumPoints] = " << p.nodes.size() << "\n";
  for (const FemmNode& n : p.nodes)
    out << g17(n.x) << "\t" << g17(n.y) << "\t" << n.pointPropIndex << "\t" << n.inGroup << "\n";

  out << "[NumSegments] = " << p.segments.size() << "\n";
  for (const FemmSegment& s : p.segments) {
    out << s.n0 << "\t" << s.n1 << "\t";
    if (s.maxSideLength < 0)
      out << "-1\t";
    else
      out << g17(s.maxSideLength) << "\t";
    out << s.boundaryMarker << "\t" << (s.hidden ? 1 : 0) << "\t" << s.inGroup << "\n";
  }

  out << "[NumArcSegments] = " << p.arcSegments.size() << "\n";
  for (const FemmArcSegment& a : p.arcSegments) {
    out << a.n0 << "\t" << a.n1 << "\t" << g17(a.arcLength) << "\t" << g17(a.maxSideLength) << "\t"
        << a.boundaryMarker << "\t" << (a.hidden ? 1 : 0) << "\t" << a.inGroup << "\t" << g17(a.mySideLength) << "\n";
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
    out << b.circuitIndex << "\t" << g17(b.magDir) << "\t" << b.inGroup << "\t" << b.turns << "\t"
        << ((b.isExternal ? 1 : 0) + (b.isDefault ? 2 : 0));
    if (!b.magDirFctn.isEmpty())
      out << "\t\"" << b.magDirFctn << "\"";
    out << "\n";
  }

  return true;
}
