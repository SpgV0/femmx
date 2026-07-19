#define _USE_MATH_DEFINES

#include "CircuitAnalysis.h"

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

#include <cmath>

namespace {
using Complex = std::complex<double>;

double lengthConvToMeters(FemmLengthUnits u)
{
  switch (u) {
  case FemmLengthUnits::Inches: return 0.0254;
  case FemmLengthUnits::Millimeters: return 0.001;
  case FemmLengthUnits::Centimeters: return 0.01;
  case FemmLengthUnits::Meters: return 1.0;
  case FemmLengthUnits::Mils: return 0.0000254;
  case FemmLengthUnits::Microns: return 0.000001;
  }
  return 1.0;
}

// Matches femm/FemmviewDoc.cpp's ElmArea exactly (signed; sign is
// unused here since only fabs() of the result is ever taken, but keeping
// the same formula avoids yet another independent area computation).
double elmAreaSigned(double x0, double y0, double x1, double y1, double x2, double y2)
{
  double b0 = y1 - y2, b1 = y2 - y0, c0 = x2 - x1, c1 = x0 - x2;
  return (b0 * c1 - b1 * c0) / 2.0;
}

// femm/FemmviewDoc.cpp's PlnInt/AxiInt, ported verbatim.
Complex plnInt(double a, const Complex u[3], const Complex v[3])
{
  Complex z[3] = { 2.0 * u[0] + u[1] + u[2], u[0] + 2.0 * u[1] + u[2], u[0] + u[1] + 2.0 * u[2] };
  Complex x = 0;
  for (int i = 0; i < 3; i++)
    x += v[i] * z[i];
  return a * x / 12.0;
}

Complex axiInt(double a, const Complex u[3], const Complex v[3], const double r[3])
{
  double M[3][3];
  M[0][0] = 6 * r[0] + 2 * r[1] + 2 * r[2];
  M[0][1] = 2 * r[0] + 2 * r[1] + 1 * r[2];
  M[0][2] = 2 * r[0] + 1 * r[1] + 2 * r[2];
  M[1][1] = 2 * r[0] + 6 * r[1] + 2 * r[2];
  M[1][2] = 1 * r[0] + 2 * r[1] + 2 * r[2];
  M[2][2] = 2 * r[0] + 2 * r[1] + 6 * r[2];
  M[1][0] = M[0][1];
  M[2][0] = M[0][2];
  M[2][1] = M[1][2];
  Complex z[3];
  for (int i = 0; i < 3; i++)
    z[i] = M[i][0] * u[0] + M[i][1] * u[1] + M[i][2] * u[2];
  Complex x = 0;
  for (int i = 0; i < 3; i++)
    x += v[i] * z[i];
  return M_PI * a * x / 30.0;
}

} // namespace

bool CircuitAnalysis::readBlockCircuitInfo(const QString& ansPath, QVector<BlockCircuitInfo>& out, QString& errorMessage)
{
  QFile file(ansPath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    errorMessage = QStringLiteral("Could not open \"%1\" for reading.").arg(ansPath);
    return false;
  }
  QTextStream in(&file);

  bool foundSolution = false;
  while (!in.atEnd()) {
    if (in.readLine().trimmed().compare("[Solution]", Qt::CaseInsensitive) == 0) {
      foundSolution = true;
      break;
    }
  }
  if (!foundSolution) {
    errorMessage = "No [Solution] section -- this file hasn't been solved.";
    return false;
  }

  auto readCount = [&](const char* what, int& count) {
    if (in.atEnd()) {
      errorMessage = QStringLiteral("Truncated .ans file (%1).").arg(what);
      return false;
    }
    bool ok = false;
    count = in.readLine().trimmed().toInt(&ok);
    if (!ok) {
      errorMessage = QStringLiteral("Malformed .ans file (%1 count).").arg(what);
      return false;
    }
    return true;
  };

  int numNodes = 0;
  if (!readCount("nodes", numNodes))
    return false;
  for (int i = 0; i < numNodes && !in.atEnd(); i++)
    in.readLine();

  int numElements = 0;
  if (!readCount("elements", numElements))
    return false;
  for (int i = 0; i < numElements && !in.atEnd(); i++)
    in.readLine();

  int numLabels = 0;
  if (!readCount("block labels", numLabels))
    return false;

  out.clear();
  static const QRegularExpression ws("\\s+");
  for (int i = 0; i < numLabels; i++) {
    if (in.atEnd()) {
      errorMessage = "Truncated .ans file (block label circuit info).";
      return false;
    }
    QStringList fields = in.readLine().trimmed().split(ws, Qt::SkipEmptyParts);
    if (fields.size() < 2) {
      errorMessage = "Malformed .ans file (block label circuit info).";
      return false;
    }
    BlockCircuitInfo info;
    info.caseType = fields[0].toInt();
    info.value = fields[1].toDouble();
    out.push_back(info);
  }
  return true;
}

CircuitAnalysis::Result CircuitAnalysis::compute(const FemmProblem& problem, const MeshSolution& solution,
    const QVector<BlockCircuitInfo>& blockCircuitInfo, int circuitIndex1Based)
{
  Result result;
  if (circuitIndex1Based < 1 || circuitIndex1Based > problem.circuitProps.size()) {
    result.error = "Invalid circuit.";
    return result;
  }
  const FemmCircuitProp& circuit = problem.circuitProps[circuitIndex1Based - 1];
  result.amps = Complex(circuit.ampsRe, circuit.ampsIm);
  if (result.amps == Complex(0, 0)) {
    result.error = "This circuit carries zero prescribed current -- flux linkage from mutual "
                    "inductance alone (classic FEMM's zero-current special case) isn't supported.";
    return result;
  }

  bool axisym = problem.problemType == FemmCoordinateType::Axisymmetric;
  double lc = lengthConvToMeters(problem.lengthUnits);
  double w = 2.0 * M_PI * problem.frequency;

  QVector<int> blockIndices; // 0-based into problem.blockLabels
  for (int i = 0; i < problem.blockLabels.size(); i++) {
    if (problem.blockLabels[i].circuitIndex != circuitIndex1Based)
      continue;
    const FemmBlockLabel& b = problem.blockLabels[i];
    if (b.blockTypeIndex < 1 || b.blockTypeIndex > problem.materialProps.size()) {
      result.error = "A block assigned to this circuit has no material.";
      return result;
    }
    const FemmMaterialProp& mat = problem.materialProps[b.blockTypeIndex - 1];
    if (mat.lamType >= 3) {
      result.error = QStringLiteral("This circuit includes a stranded/litz-wire region (material \"%1\") "
                                     "-- not supported (needs femm/FemmviewDoc.cpp's GetFillFactor "
                                     "skin-effect model, not ported here).")
                          .arg(mat.name);
      return result;
    }
    if (i >= blockCircuitInfo.size()) {
      result.error = "Solved-circuit data is missing for a block in this circuit.";
      return result;
    }
    if (blockCircuitInfo[i].caseType != 0) {
      result.error = "Unsupported circuit configuration for this block (expected a solved-voltage solid conductor).";
      return result;
    }
    blockIndices.push_back(i);
  }
  if (blockIndices.isEmpty()) {
    result.error = "No geometry is assigned to this circuit.";
    return result;
  }

  // Voltage drop -- femm/FemmviewDoc.cpp's GetVoltageDrop, solid-conductor
  // branches only. See this file's header comment: Depth (planar) IS
  // converted to meters here; the raw dVolts value read from .ans is used
  // unconverted -- empirically validated, not just read off the source.
  double factor = axisym ? (2.0 * M_PI) : (problem.depth * lc);
  Complex volts = 0;
  if (circuit.circType == 1) { // series -- every block carries the full circuit current
    for (int idx : blockIndices)
      volts -= factor * blockCircuitInfo[idx].value * problem.blockLabels[idx].turns;
  } else { // parallel -- every block shares the same voltage; one representative value is enough
    volts -= factor * blockCircuitInfo[blockIndices.first()].value;
  }
  result.voltsDrop = volts;

  // Flux linkage -- femm/FemmviewDoc.cpp's GetFluxLinkage's general
  // (Amps != 0) branch: Integral(A . conj(J)) / conj(Amps) over every
  // element in every block belonging to this circuit, J reconstructed
  // per element exactly as GetJA does (solid-conductor terms only).
  QSet<int> qualifying(blockIndices.begin(), blockIndices.end());
  Complex fluxLinkage = 0;
  for (const MeshSolutionElement& e : solution.elements) {
    if (!qualifying.contains(e.lbl))
      continue;
    if (e.p0 < 0 || e.p0 >= solution.nodes.size() || e.p1 < 0 || e.p1 >= solution.nodes.size() || e.p2 < 0 || e.p2 >= solution.nodes.size())
      continue;
    const MeshSolutionNode& n0 = solution.nodes[e.p0];
    const MeshSolutionNode& n1 = solution.nodes[e.p1];
    const MeshSolutionNode& n2 = solution.nodes[e.p2];

    double a = std::fabs(elmAreaSigned(n0.x, n0.y, n1.x, n1.y, n2.x, n2.y)) * lc * lc;

    const FemmMaterialProp& mat = problem.materialProps[problem.blockLabels[e.lbl].blockTypeIndex - 1];
    double sigma = mat.sigma; // MS/m, used raw -- matches GetJA (see header comment)
    Complex Jsrc(mat.JsrcRe, mat.JsrcIm); // MA/m^2, raw
    double dVolts = blockCircuitInfo[e.lbl].value;

    Complex Araw[3] = { Complex(n0.Are, n0.Aim), Complex(n1.Are, n1.Aim), Complex(n2.Are, n2.Aim) };
    Complex Aphys[3];
    Complex J[3];

    if (!axisym) {
      for (int k = 0; k < 3; k++) {
        Aphys[k] = Araw[k];
        J[k] = (Jsrc - Complex(0, 1) * w * sigma * Aphys[k] - sigma * dVolts) * 1.0e6;
      }
      Complex Jc[3] = { std::conj(J[0]), std::conj(J[1]), std::conj(J[2]) };
      fluxLinkage += plnInt(a, Aphys, Jc) * (problem.depth * lc);
    } else {
      double rNode[3] = { n0.x * lc, n1.x * lc, n2.x * lc };
      double rCentroid = (rNode[0] + rNode[1] + rNode[2]) / 3.0;
      for (int k = 0; k < 3; k++) {
        bool nearAxis = std::fabs(rNode[k]) < 1.0e-6;
        double r = nearAxis ? rCentroid : rNode[k];
        Aphys[k] = nearAxis ? Complex(0, 0) : Araw[k] / (2.0 * M_PI * rNode[k]);
        J[k] = (Jsrc - Complex(0, 1) * w * sigma * Aphys[k] - sigma * dVolts / r) * 1.0e6;
      }
      Complex Jc[3] = { std::conj(J[0]), std::conj(J[1]), std::conj(J[2]) };
      fluxLinkage += axiInt(a, Aphys, Jc, rNode);
    }
  }
  fluxLinkage /= std::conj(result.amps);
  result.fluxLinkage = fluxLinkage;
  result.ok = true;
  return result;
}
