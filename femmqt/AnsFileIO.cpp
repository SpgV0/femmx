#define _USE_MATH_DEFINES

#include "AnsFileIO.h"

#include "FemmFileIO.h"
#include "FemmProblem.h"
#include "MeshSolution.h"

#include <QFile>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>

namespace {

// femm/FemmviewDoc.cpp's LengthConv[] table (FemmviewDoc.cpp:80-86),
// indexed the same way as FemmLengthUnits.
constexpr double kLengthConv[6] = {
  0.0254, // Inches
  0.001, // Millimeters
  0.01, // Centimeters
  1.0, // Meters
  2.54e-05, // Mils
  1.0e-06, // Microns
};

// Mirrors CFemmviewDoc::GetElementB (femm/FemmviewDoc.cpp:2496-2607) for
// the two field values the density plot needs (B1, B2) -- omits the
// incremental-permeability (B1p/B2p) terms this phase doesn't use. Takes
// plain node positions/potentials rather than a CElement/meshnode array
// so it has no dependency on this file's own MeshSolution layout choices.
void computeElementB(
    double x0, double y0, double Are0, double Aim0,
    double x1, double y1, double Are1, double Aim1,
    double x2, double y2, double Are2, double Aim2,
    bool axisymmetric, double lengthConv,
    double& B1re, double& B1im, double& B2re, double& B2im)
{
  double b[3] = { y1 - y2, y2 - y0, y0 - y1 };
  double c[3] = { x2 - x1, x0 - x2, x1 - x0 };
  double da = b[0] * c[1] - b[1] * c[0];

  if (!axisymmetric) {
    double AreN[3] = { Are0, Are1, Are2 };
    double AimN[3] = { Aim0, Aim1, Aim2 };
    B1re = B1im = B2re = B2im = 0.0;
    for (int i = 0; i < 3; i++) {
      B1re += AreN[i] * c[i] / (da * lengthConv);
      B1im += AimN[i] * c[i] / (da * lengthConv);
      B2re -= AreN[i] * b[i] / (da * lengthConv);
      B2im -= AimN[i] * b[i] / (da * lengthConv);
    }
    return;
  }

  // Axisymmetric case (femm/FemmviewDoc.cpp:2530-2568), using complex
  // arithmetic exactly as the original does (mid-side node values blend
  // corner potentials weighted by radius).
  double R[3] = { x0, x1, x2 };
  double r = (R[0] + R[1] + R[2]) / 3.0;

  std::complex<double> v[6];
  v[0] = { Are0, Aim0 };
  v[2] = { Are1, Aim1 };
  v[4] = { Are2, Aim2 };

  auto midSide = [](double Ra, double Rb, std::complex<double> va, std::complex<double> vb) {
    if (Ra < 1.0e-06 && Rb < 1.0e-06)
      return (va + vb) / 2.0;
    return (Rb * (3.0 * va + vb) + Ra * (va + 3.0 * vb)) / (4.0 * (Ra + Rb));
  };
  v[1] = midSide(R[0], R[1], v[0], v[2]);
  v[3] = midSide(R[1], R[2], v[2], v[4]);
  v[5] = midSide(R[2], R[0], v[4], v[0]);

  std::complex<double> dp = (-v[0] + v[2] + 4.0 * v[3] - 4.0 * v[5]) / 3.0;
  std::complex<double> dq = (-v[0] - 4.0 * v[1] + 4.0 * v[3] + v[4]) / 3.0;

  double da2 = da * 2.0 * M_PI * r * lengthConv * lengthConv;
  std::complex<double> cB1 = -(c[1] * dp + c[2] * dq) / da2;
  std::complex<double> cB2 = (b[1] * dp + b[2] * dq) / da2;
  B1re = cB1.real();
  B1im = cB1.imag();
  B2re = cB2.real();
  B2im = cB2.imag();
}

} // namespace

bool AnsFileIO::readAns(const QString& path, FemmProblem& problem, MeshSolution& solution, QString& errorMessage)
{
  // .ans's header/properties/geometry section is byte-for-byte the same
  // format as .fem -- reuse the already-verified reader for it. Its
  // main loop silently skips any line without a recognized "[Tag] ="/
  // "<Tag> =" prefix (see FemmFileIO.cpp), which harmlessly no-ops
  // through the appended [Solution] section this pass doesn't care
  // about -- so this alone is enough to populate `problem` correctly.
  if (!FemmFileIO::readFem(path, problem, errorMessage))
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
    errorMessage = QStringLiteral("\"%1\" has no [Solution] section -- is it a solved .ans file?").arg(path);
    return false;
  }

  // Mesh node/element counts + rows: fast manual strtod/strtol field
  // extraction rather than per-line tokenizing, matching the technique
  // femm/FemmviewDoc.cpp's v1.1.1 fix already proved necessary at this
  // scale (see CHANGELOG.md) -- these files can have many millions of
  // lines here.
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
    n.Are = std::strtod(p, &next);
    p = next;
    n.Aim = (problem.frequency != 0) ? std::strtod(p, &next) : 0.0;
    // any trailing bc/Aprev fields (incremental-permeability problems
    // only) are intentionally left unparsed -- unused this phase.
  }

  line = file.readLine();
  long elemCount = std::strtol(line.constData(), nullptr, 10);
  solution.elements.resize((int)elemCount);
  bool axisymmetric = (problem.problemType == FemmCoordinateType::Axisymmetric);
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
    // trailing Jp (incremental only) intentionally left unparsed.

    if (e.p0 < 0 || e.p0 >= solution.nodes.size() || e.p1 < 0 || e.p1 >= solution.nodes.size() || e.p2 < 0 || e.p2 >= solution.nodes.size())
      continue;
    const MeshSolutionNode& n0 = solution.nodes[e.p0];
    const MeshSolutionNode& n1 = solution.nodes[e.p1];
    const MeshSolutionNode& n2 = solution.nodes[e.p2];

    e.ctrX = (n0.x + n1.x + n2.x) / 3.0;
    e.ctrY = (n0.y + n1.y + n2.y) / 3.0;

    computeElementB(
        n0.x, n0.y, n0.Are, n0.Aim,
        n1.x, n1.y, n1.Are, n1.Aim,
        n2.x, n2.y, n2.Are, n2.Aim,
        axisymmetric, lengthConv,
        e.B1re, e.B1im, e.B2re, e.B2im);

    double bMag = std::hypot(std::hypot(e.B1re, e.B1im), std::hypot(e.B2re, e.B2im));
    if (first) {
      solution.bMagMin = solution.bMagMax = bMag;
      first = false;
    } else {
      solution.bMagMin = std::min(solution.bMagMin, bMag);
      solution.bMagMax = std::max(solution.bMagMax, bMag);
    }
  }

  return true;
}
