#include "AnhFileIO.h"

#include "FemmProblem.h"
#include "HeatFileIO.h"
#include "MeshSolution.h"

#include <QFile>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

// Same table as AnsFileIO.cpp's kLengthConv (femm/FemmviewDoc.cpp's
// LengthConv[]) -- duplicated per this codebase's existing per-file-
// format-module convention (see FemmFileIO.cpp's header comment).
constexpr double kLengthConv[6] = {
  0.0254, // Inches
  0.001, // Millimeters
  0.01, // Centimeters
  1.0, // Meters
  2.54e-05, // Mils
  1.0e-06, // Microns
};

// Recovers heat flux G = -k*grad(T) over a linear triangular element from
// its 3 nodal temperatures, via the standard shape-function-gradient
// formula -- same b[i]/c[i]/da construction as AnsFileIO.cpp's
// computeElementB (itself mirroring femm/FemmviewDoc.cpp's GetElementB),
// but a plain gradient rather than a curl (heat flux is -k*grad(T) of a
// true scalar field, not derived from a vector potential the way B is
// from A) -- no axisymmetric special case is needed either, for the same
// reason: unlike GetElementB's r-weighted circulation terms (specific to
// A being a vector potential), a scalar gradient's in-plane formula is
// identical in (r,z) as in (x,y).
void computeElementFlux(
    double x0, double y0, double T0,
    double x1, double y1, double T1,
    double x2, double y2, double T2,
    double Kx, double Ky, double lengthConv,
    double& Gx, double& Gy)
{
  double b[3] = { y1 - y2, y2 - y0, y0 - y1 };
  double c[3] = { x2 - x1, x0 - x2, x1 - x0 };
  double da = b[0] * c[1] - b[1] * c[0];
  double TN[3] = { T0, T1, T2 };

  double dTdx = 0, dTdy = 0;
  for (int i = 0; i < 3; i++) {
    dTdx += TN[i] * b[i] / (da * lengthConv);
    dTdy += TN[i] * c[i] / (da * lengthConv);
  }
  Gx = -Kx * dTdx;
  Gy = -Ky * dTdy;
}

} // namespace

bool AnhFileIO::readAnh(const QString& path, FemmProblem& problem, MeshSolution& solution, QString& errorMessage)
{
  // .anh's header/properties/geometry section is byte-for-byte the same
  // format as .feh (hsolv/prob1big.cpp's WriteResults literally echoes
  // the source .feh verbatim before appending [Solution]) -- reuse the
  // already-verified reader for it, same as AnsFileIO::readAns reuses
  // FemmFileIO::readFem.
  if (!HeatFileIO::readFeh(path, problem, errorMessage))
    return false;

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    errorMessage = QStringLiteral("Could not open \"%1\" for reading.").arg(path);
    return false;
  }

  bool foundSolution = false;
  QByteArray line;
  while (!file.atEnd()) {
    line = file.readLine();
    QByteArray trimmed = line.trimmed();
    if (trimmed.compare("[Solution]", Qt::CaseInsensitive) == 0) {
      foundSolution = true;
      break;
    }
  }
  if (!foundSolution) {
    errorMessage = QStringLiteral("\"%1\" has no [Solution] section -- is it a solved .anh file?").arg(path);
    return false;
  }

  // Mesh node rows: x, y, T, Q (Q is a per-node boundary-condition marker
  // hsolv writes for its own internal bookkeeping -- not a physical
  // quantity, not used here). Manual strtod/strtol field extraction
  // rather than per-line tokenizing, matching AnsFileIO::readAns' own
  // proven-necessary-at-scale technique.
  line = file.readLine();
  long nodeCount = std::strtol(line.constData(), nullptr, 10);
  solution.nodes.resize((int)nodeCount);
  for (long i = 0; i < nodeCount; i++) {
    line = file.readLine();
    const char* p = line.constData();
    char* next = nullptr;
    MeshSolutionNode& n = solution.nodes[(int)i];
    n.x = std::strtod(p, &next);
    p = next;
    n.y = std::strtod(p, &next);
    p = next;
    n.Are = std::strtod(p, &next); // repurposed: nodal temperature, K -- see AnhFileIO.h
    n.Aim = 0.0; // steady-state heat flow has no imaginary part
    // trailing Q (node marker) intentionally left unparsed.
  }

  line = file.readLine();
  long elemCount = std::strtol(line.constData(), nullptr, 10);
  solution.elements.resize((int)elemCount);
  double lengthConv = kLengthConv[(int)problem.lengthUnits];
  bool first = true;
  for (long i = 0; i < elemCount; i++) {
    line = file.readLine();
    const char* p = line.constData();
    char* next = nullptr;
    MeshSolutionElement& e = solution.elements[(int)i];
    e.p0 = (int)std::strtol(p, &next, 10);
    p = next;
    e.p1 = (int)std::strtol(p, &next, 10);
    p = next;
    e.p2 = (int)std::strtol(p, &next, 10);
    p = next;
    e.lbl = (int)std::strtol(p, &next, 10);

    // Resolve this element's material once here (mirrors AnsFileIO::
    // readAns' identical precompute-once pattern) -- e.lbl indexes
    // problem.blockLabels directly, same reasoning as that file's own
    // comment (both this reader and hsolv populate that list by reading
    // the identical NumHoles-then-NumBlockLabels .feh-format sections in
    // file order).
    if (e.lbl >= 0 && e.lbl < problem.blockLabels.size()) {
      int matIdx = problem.blockLabels[e.lbl].thermalBlockTypeIndex - 1; // 1-based -> 0-based
      if (matIdx >= 0 && matIdx < problem.thermalMaterialProps.size()) {
        const FemmThermalMaterialProp& mat = problem.thermalMaterialProps[matIdx];
        e.muX = mat.Kx; // repurposed: thermal conductivity -- see AnhFileIO.h
        e.muY = mat.Ky;
      }
    }

    if (e.p0 < 0 || e.p0 >= solution.nodes.size() || e.p1 < 0 || e.p1 >= solution.nodes.size() || e.p2 < 0 || e.p2 >= solution.nodes.size())
      continue;
    const MeshSolutionNode& n0 = solution.nodes[e.p0];
    const MeshSolutionNode& n1 = solution.nodes[e.p1];
    const MeshSolutionNode& n2 = solution.nodes[e.p2];

    e.ctrX = (n0.x + n1.x + n2.x) / 3.0;
    e.ctrY = (n0.y + n1.y + n2.y) / 3.0;

    computeElementFlux(
        n0.x, n0.y, n0.Are,
        n1.x, n1.y, n1.Are,
        n2.x, n2.y, n2.Are,
        e.muX, e.muY, lengthConv,
        e.B1re, e.B2re); // repurposed: Gx, Gy -- see AnhFileIO.h
    e.B1im = e.B2im = 0.0;

    double fluxMag = std::hypot(e.B1re, e.B2re);
    if (first) {
      solution.bMagMin = solution.bMagMax = fluxMag;
      first = false;
    } else {
      solution.bMagMin = std::min(solution.bMagMin, fluxMag);
      solution.bMagMax = std::max(solution.bMagMax, fluxMag);
    }
  }

  // NumCircProps (conductor) rows follow, then EOF -- nothing downstream
  // needs them yet (no thermal conductor UI, see FemmThermalConductorProp's
  // comment), so parsing stops here rather than reading-and-discarding.
  return true;
}
