// tst_solution_integrals.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #83).
//
// Point values, line integrals and block integrals, for all four
// physics.
//
// Every expected number here comes from a closed form, not from a
// previous run. A uniform field in a square region has an energy you can
// write down; the flux through a line across a uniform field is the
// field times the length; a linear potential interpolates exactly. That
// matters because an integral that is wrong by a constant -- a factor of
// two from the 1/2 in an energy density, a length unit not converted, an
// area counted in model units instead of metres -- produces a number
// that looks entirely reasonable. There is nothing about "3.7 J/m" that
// announces it should have been 3.7e-6.
//
// The mesh below is deliberately trivial: two triangles forming a unit
// square, with a potential that varies linearly in one direction, so the
// field is uniform and every integral has an exact value.

#include <QtTest>

#include "FemmProblem.h"
#include "MeshSolution.h"
#include "ConductorAnalysis.h"
#include "SolutionAdapter.h"
#include "SolutionIntegrals.h"

#include <cmath>

namespace {

constexpr double kMu0 = 4.0e-7 * M_PI;
constexpr double kEps0 = 8.8541878128e-12;

// A unit square [0,1]x[0,1] as two triangles, with potential = g*x.
// The gradient is therefore (g, 0) everywhere.
SolvedMesh unitSquare(double g)
{
  SolvedMesh m;
  SolutionNode n;
  n.x = 0; n.y = 0; n.potentialRe = 0;      m.nodes << n;
  n.x = 1; n.y = 0; n.potentialRe = g;      m.nodes << n;
  n.x = 1; n.y = 1; n.potentialRe = g;      m.nodes << n;
  n.x = 0; n.y = 1; n.potentialRe = 0;      m.nodes << n;

  SolutionElement e;
  e.label = 0;
  e.p0 = 0; e.p1 = 1; e.p2 = 2; m.elements << e;
  e.p0 = 0; e.p1 = 2; e.p2 = 3; m.elements << e;
  return m;
}

FemmProblem squareProblem(FemmProblemKind kind)
{
  FemmProblem p;
  p.kind = kind;
  p.lengthUnits = FemmLengthUnits::Meters;
  FemmBlockLabel lbl;
  lbl.blockTypeIndex = 1;
  p.blockLabels << lbl;
  return p;
}

bool close(double a, double b, double rel = 1e-9)
{
  const double scale = std::max({ std::abs(a), std::abs(b), 1e-300 });
  return std::abs(a - b) <= rel * scale;
}

} // namespace

class TestSolutionIntegrals : public QObject
{
  Q_OBJECT

  private slots:
  void aPointInsideAnElementIsFound();
  void aPointOutsideTheMeshIsReportedNotGuessed();
  void aPointOnASharedEdgeBelongsToAnElement();
  void thePotentialInterpolatesExactlyAcrossAnElement();

  void theBlockAreaIsInSquareMetresNotModelUnits();
  void magneticEnergyMatchesTheClosedForm();
  void electricEnergyMatchesTheClosedForm();
  void currentFlowReportsLossNotStoredEnergy();
  void heatFlowHasNoEnergyDensityAndSaysSo();
  void blockIntegralCanBeRestrictedToSelectedLabels();

  void lineFluxAcrossAUniformFieldIsFieldTimesLength();
  void lineIntegralNamesWhatItMeasuresPerPhysics();

  void theAdapterGivesAUniformFieldAUniformRange();
  void theAdapterCarriesThePotentialAndTheFieldOntoTheRenderersShape();

  void aConductorReportsItsEquipotentialValue();
  void aConductorReportsTheFluxLeavingItsSurface();
  void anUntaggedConductorIsAbsentNotZero();
  void conductorQuantitiesAreNamedPerPhysicsAndAbsentForMagnetics();
};

// ---------------------------------------------------------------------------
// Point values
// ---------------------------------------------------------------------------

void TestSolutionIntegrals::aPointInsideAnElementIsFound()
{
  const SolvedMesh m = unitSquare(10.0);
  QVERIFY(SolutionIntegrals::elementAt(m, 0.75, 0.25) >= 0);
  QVERIFY(SolutionIntegrals::elementAt(m, 0.25, 0.75) >= 0);
}

void TestSolutionIntegrals::aPointOutsideTheMeshIsReportedNotGuessed()
{
  // Clamping to the nearest element would give a plausible reading for a
  // point that is not in the model at all.
  const SolvedMesh m = unitSquare(10.0);
  QCOMPARE(SolutionIntegrals::elementAt(m, 5.0, 5.0), -1);

  const FemmProblem p = squareProblem(FemmProblemKind::HeatFlow);
  const auto v = SolutionIntegrals::pointValue(m, p, 5.0, 5.0);
  QVERIFY2(!v.ok, "a point outside the mesh returned a value");
  QCOMPARE(v.element, -1);
}

void TestSolutionIntegrals::aPointOnASharedEdgeBelongsToAnElement()
{
  // The diagonal is shared by both triangles. A strict inside test fails
  // on every shared edge in a mesh, which is a lot of the mesh.
  const SolvedMesh m = unitSquare(10.0);
  QVERIFY2(SolutionIntegrals::elementAt(m, 0.5, 0.5) >= 0,
      "a point on the shared diagonal belonged to neither element");
  // And the outer boundary.
  QVERIFY(SolutionIntegrals::elementAt(m, 0.5, 0.0) >= 0);
}

void TestSolutionIntegrals::thePotentialInterpolatesExactlyAcrossAnElement()
{
  // Potential = 10x, so at x = 0.25 it is exactly 2.5. Linear across a
  // linear element: this is exact, not approximate.
  const SolvedMesh m = unitSquare(10.0);
  const FemmProblem p = squareProblem(FemmProblemKind::HeatFlow);

  const auto v = SolutionIntegrals::pointValue(m, p, 0.25, 0.75);
  QVERIFY(v.ok);
  QVERIFY2(close(v.potentialRe, 2.5),
      qPrintable(QStringLiteral("expected 2.5, got %1").arg(v.potentialRe, 0, 'g', 17)));
}

// ---------------------------------------------------------------------------
// Block integrals
// ---------------------------------------------------------------------------

void TestSolutionIntegrals::theBlockAreaIsInSquareMetresNotModelUnits()
{
  const SolvedMesh m = unitSquare(1.0);

  {
    FemmProblem p = squareProblem(FemmProblemKind::HeatFlow);
    const auto r = SolutionIntegrals::blockIntegral(m, p, {});
    QVERIFY(r.ok);
    QCOMPARE(r.elementCount, 2);
    QVERIFY2(close(r.area, 1.0), qPrintable(QString::number(r.area, 'g', 17)));
  }
  {
    // The same square in millimetres is a millionth of the area. An area
    // left in model units is the kind of error that makes an energy
    // wrong by 1e6 while still looking like a number.
    FemmProblem p = squareProblem(FemmProblemKind::HeatFlow);
    p.lengthUnits = FemmLengthUnits::Millimeters;
    const auto r = SolutionIntegrals::blockIntegral(m, p, {});
    QVERIFY2(close(r.area, 1e-6),
        qPrintable(QStringLiteral("expected 1e-6 m2, got %1").arg(r.area, 0, 'g', 17)));
  }
}

void TestSolutionIntegrals::magneticEnergyMatchesTheClosedForm()
{
  // A = 1*x over a 1 m square in free space. B = curl A = (dA/dy,
  // -dA/dx) = (0, -1), so |B| = 1 T, and the stored energy is
  // |B|^2/(2 mu0) * area = 1/(2 mu0) J/m.
  const SolvedMesh m = unitSquare(1.0);
  FemmProblem p = squareProblem(FemmProblemKind::Magnetics);
  FemmMaterialProp air; // mu_r = 1 by default
  p.materialProps << air;

  const auto r = SolutionIntegrals::blockIntegral(m, p, {});
  QVERIFY(r.ok);
  const double expected = 1.0 / (2.0 * kMu0);
  QVERIFY2(close(r.energy, expected, 1e-9),
      qPrintable(QStringLiteral("expected %1 J/m, got %2")
                     .arg(expected, 0, 'g', 10).arg(r.energy, 0, 'g', 10)));
}

void TestSolutionIntegrals::electricEnergyMatchesTheClosedForm()
{
  // V = 100x over a 1 m square with er = 2. E = -grad V = (-100, 0), so
  // |E| = 100 V/m and the energy is eps0*er*|E|^2/2 * area.
  const SolvedMesh m = unitSquare(100.0);
  FemmProblem p = squareProblem(FemmProblemKind::Electrostatics);
  FemmEsMaterialProp mat;
  mat.ex = 2.0;
  mat.ey = 2.0;
  p.esMaterialProps << mat;

  const auto r = SolutionIntegrals::blockIntegral(m, p, {});
  QVERIFY(r.ok);
  const double expected = 0.5 * kEps0 * 2.0 * 100.0 * 100.0;
  QVERIFY2(close(r.energy, expected, 1e-9),
      qPrintable(QStringLiteral("expected %1 J/m, got %2")
                     .arg(expected, 0, 'g', 10).arg(r.energy, 0, 'g', 10)));
}

void TestSolutionIntegrals::currentFlowReportsLossNotStoredEnergy()
{
  // V = 1x over a 1 m square of sigma = 58e6 S/m. J = sigma*E, and the
  // dissipation is sigma*|E|^2/2 per unit volume.
  const SolvedMesh m = unitSquare(1.0);
  FemmProblem p = squareProblem(FemmProblemKind::CurrentFlow);
  FemmCfMaterialProp mat;
  mat.ox = 58.0e6;
  mat.oy = 58.0e6;
  p.cfMaterialProps << mat;

  const auto r = SolutionIntegrals::blockIntegral(m, p, {});
  QVERIFY(r.ok);
  const double expected = 0.5 * 58.0e6 * 1.0 * 1.0;
  QVERIFY2(close(r.energy, expected, 1e-9),
      qPrintable(QStringLiteral("expected %1 W/m, got %2")
                     .arg(expected, 0, 'g', 10).arg(r.energy, 0, 'g', 10)));

  // And it is called a loss, in watts, not an energy in joules.
  const auto q = SolutionIntegrals::blockEnergyQuantity(FemmProblemKind::CurrentFlow);
  QVERIFY(q.name.contains("loss", Qt::CaseInsensitive));
  QCOMPARE(q.unit, QStringLiteral("W/m"));
}

void TestSolutionIntegrals::heatFlowHasNoEnergyDensityAndSaysSo()
{
  // A steady conduction solution stores no energy. Returning 0 with a
  // label would invite reading it as a result; the quantity is empty so
  // a caller knows not to offer it at all.
  const auto q = SolutionIntegrals::blockEnergyQuantity(FemmProblemKind::HeatFlow);
  QVERIFY2(q.name.isEmpty(),
      "heat flow was given an energy quantity, which would read as a result "
      "rather than as not applicable");

  // The area and average temperature are still meaningful.
  const SolvedMesh m = unitSquare(10.0);
  FemmProblem p = squareProblem(FemmProblemKind::HeatFlow);
  FemmHtMaterialProp mat;
  mat.Kx = 401.0;
  mat.Ky = 401.0;
  p.htMaterialProps << mat;
  const auto r = SolutionIntegrals::blockIntegral(m, p, {});
  QVERIFY(r.ok);
  QVERIFY(close(r.area, 1.0));
  QCOMPARE(r.energy, 0.0);
  // Potential = 10x averaged over the unit square is 5.
  QVERIFY2(close(r.averagePotential, 5.0, 1e-9),
      qPrintable(QString::number(r.averagePotential, 'g', 17)));
}

void TestSolutionIntegrals::blockIntegralCanBeRestrictedToSelectedLabels()
{
  SolvedMesh m = unitSquare(1.0);
  m.elements[1].label = 1; // put the second triangle on another label

  FemmProblem p = squareProblem(FemmProblemKind::HeatFlow);
  FemmBlockLabel second;
  second.blockTypeIndex = 1;
  p.blockLabels << second;

  const auto all = SolutionIntegrals::blockIntegral(m, p, {});
  QCOMPARE(all.elementCount, 2);
  QVERIFY(close(all.area, 1.0));

  const auto justOne = SolutionIntegrals::blockIntegral(m, p, { 0 });
  QCOMPARE(justOne.elementCount, 1);
  QVERIFY2(close(justOne.area, 0.5),
      qPrintable(QStringLiteral("half the square is 0.5 m2, got %1").arg(justOne.area)));
}

// ---------------------------------------------------------------------------
// Line integrals
// ---------------------------------------------------------------------------

void TestSolutionIntegrals::lineFluxAcrossAUniformFieldIsFieldTimesLength()
{
  // T = 10x over a square of k = 2 W/(m*K). The heat flux is
  // F = -k grad T = (-20, 0) W/m^2, uniform.
  //
  // Take a vertical line from (0.5,0) to (0.5,1): direction (0,1), so
  // the normal (dy,-dx) is (1,0). The flux is F.n * length = -20 * 1.
  const SolvedMesh m = unitSquare(10.0);
  FemmProblem p = squareProblem(FemmProblemKind::HeatFlow);
  FemmHtMaterialProp mat;
  mat.Kx = 2.0;
  mat.Ky = 2.0;
  p.htMaterialProps << mat;

  QVector<QPair<double, double>> line;
  line << QPair<double, double>(0.5, 0.0) << QPair<double, double>(0.5, 1.0);

  const auto r = SolutionIntegrals::lineIntegral(m, p, line);
  QVERIFY(r.ok);
  QVERIFY2(close(r.length, 1.0), qPrintable(QString::number(r.length)));
  QVERIFY2(close(r.fluxRe, -20.0, 1e-9),
      qPrintable(QStringLiteral("expected -20 W/m, got %1").arg(r.fluxRe, 0, 'g', 10)));

  // Reversing the line reverses the normal, and so the sign -- which is
  // what makes a closed contour's direction meaningful.
  QVector<QPair<double, double>> reversed;
  reversed << QPair<double, double>(0.5, 1.0) << QPair<double, double>(0.5, 0.0);
  const auto rr = SolutionIntegrals::lineIntegral(m, p, reversed);
  QVERIFY2(close(rr.fluxRe, 20.0, 1e-9),
      qPrintable(QStringLiteral("reversing the contour should flip the sign; got %1")
                     .arg(rr.fluxRe)));
}

void TestSolutionIntegrals::lineIntegralNamesWhatItMeasuresPerPhysics()
{
  // The same integral is four different measurements, and a number
  // without its name is not a result.
  QCOMPARE(SolutionIntegrals::lineFluxQuantity(FemmProblemKind::HeatFlow).unit,
      QStringLiteral("W/m"));
  QVERIFY(SolutionIntegrals::lineFluxQuantity(FemmProblemKind::CurrentFlow)
              .name.contains("Current"));
  QVERIFY(SolutionIntegrals::lineFluxQuantity(FemmProblemKind::Magnetics)
              .name.contains("flux", Qt::CaseInsensitive));
  for (FemmProblemKind k : { FemmProblemKind::Magnetics, FemmProblemKind::Electrostatics,
           FemmProblemKind::HeatFlow, FemmProblemKind::CurrentFlow }) {
    QVERIFY(!SolutionIntegrals::lineFluxQuantity(k).name.isEmpty());
    QVERIFY(!SolutionIntegrals::lineFluxQuantity(k).unit.isEmpty());
  }
}

// ---------------------------------------------------------------------------
// The renderer adapter (issue #83)
// ---------------------------------------------------------------------------

void TestSolutionIntegrals::theAdapterGivesAUniformFieldAUniformRange()
{
  // A LINEAR potential has a CONSTANT gradient, so every element's field
  // is identical and the density plot's auto-range should collapse to a
  // single value. This is the sanity check on the whole adapter: if the
  // range comes back wide, some elements disagree, and a plot of a
  // uniform field would show structure that is not there.
  //
  // Built as a grid rather than as two triangles so that elements of
  // BOTH orientations are present -- a sign error tied to node winding
  // shows up only when both appear.
  const int n = 6;
  SolvedMesh mesh;
  for (int j = 0; j < n; j++) {
    for (int i = 0; i < n; i++) {
      SolutionNode nd;
      nd.x = i * 10.0 / (n - 1);
      nd.y = j * 10.0 / (n - 1);
      nd.potentialRe = 20.0 + 8.0 * nd.x; // linear in x
      mesh.nodes << nd;
    }
  }
  for (int j = 0; j + 1 < n; j++) {
    for (int i = 0; i + 1 < n; i++) {
      const int a = j * n + i, b = a + 1, c = a + n, d = c + 1;
      SolutionElement e;
      e.label = 0;
      e.p0 = a; e.p1 = b; e.p2 = d; mesh.elements << e;
      e.p0 = a; e.p1 = d; e.p2 = c; mesh.elements << e;
    }
  }

  FemmProblem p;
  p.kind = FemmProblemKind::HeatFlow;
  p.lengthUnits = FemmLengthUnits::Millimeters;
  FemmBlockLabel lbl;
  lbl.blockTypeIndex = 1;
  p.blockLabels << lbl;
  FemmHtMaterialProp mat;
  mat.Kx = 401.0;
  mat.Ky = 401.0;
  p.htMaterialProps << mat;

  MeshSolution adapted;
  QVERIFY(SolutionAdapter::toMeshSolution(mesh, p, adapted));
  QCOMPARE(adapted.elements.size(), mesh.elements.size());
  QCOMPARE(adapted.nodes.size(), mesh.nodes.size());

  // 8 K/mm = 8000 K/m, times 401 W/(m*K) = 3.208e6 W/m^2, everywhere.
  const double expected = 8000.0 * 401.0;
  QVERIFY2(close(adapted.bMagMin, expected, 1e-9),
      qPrintable(QStringLiteral("min %1, expected %2")
                     .arg(adapted.bMagMin, 0, 'g', 10).arg(expected, 0, 'g', 10)));
  QVERIFY2(close(adapted.bMagMax, expected, 1e-9),
      qPrintable(QStringLiteral("max %1, expected %2 -- a uniform field must not "
                                "produce a range, or the plot shows structure that "
                                "is not in the solution")
                     .arg(adapted.bMagMax, 0, 'g', 10).arg(expected, 0, 'g', 10)));
}

void TestSolutionIntegrals::theAdapterCarriesThePotentialAndTheFieldOntoTheRenderersShape()
{
  // The renderer paints MeshSolutionNode::Are and MeshSolutionElement::
  // B1/B2. For a non-magnetics solution those hold that physics'
  // potential and field, and getting the mapping wrong would draw a
  // correct-looking plot of the wrong thing.
  SolvedMesh mesh;
  SolutionNode nd;
  nd.x = 0; nd.y = 0; nd.potentialRe = 0;   mesh.nodes << nd;
  nd.x = 1; nd.y = 0; nd.potentialRe = 100; mesh.nodes << nd;
  nd.x = 0; nd.y = 1; nd.potentialRe = 0;   mesh.nodes << nd;
  SolutionElement e;
  e.p0 = 0; e.p1 = 1; e.p2 = 2; e.label = 0;
  mesh.elements << e;

  FemmProblem p;
  p.kind = FemmProblemKind::Electrostatics;
  p.lengthUnits = FemmLengthUnits::Meters;
  FemmBlockLabel lbl;
  lbl.blockTypeIndex = 1;
  p.blockLabels << lbl;
  FemmEsMaterialProp mat;
  mat.ex = 1.0;
  mat.ey = 1.0;
  p.esMaterialProps << mat;

  MeshSolution adapted;
  QVERIFY(SolutionAdapter::toMeshSolution(mesh, p, adapted));

  // The potential rides on Are, untouched.
  QCOMPARE(adapted.nodes[1].Are, 100.0);
  // The field rides on B1/B2: D = -eps0 * grad V, so Dx is negative here.
  QVERIFY2(adapted.elements[0].B1re < 0,
      "the x component of D should be negative for a potential rising in x");
  // And the centroid is the average of the corners.
  QVERIFY(close(adapted.elements[0].ctrX, 1.0 / 3.0, 1e-12));
  QVERIFY(close(adapted.elements[0].ctrY, 1.0 / 3.0, 1e-12));
}

// ---------------------------------------------------------------------------
// Conductors (issue #83)
// ---------------------------------------------------------------------------
//
// The conductor counterpart of CircuitAnalysis. Magnetics drives a
// region with a circuit; the other three hold a surface at a potential
// with a conductor, and the questions are the mirror image -- what is it
// sitting at, and what is crossing it.

namespace {

// One triangle whose (0,0)-(1,0) edge is tagged as conductor 1, in a
// uniform field. Both endpoints of that edge carry the tag, so it is the
// conductor's surface; no other edge qualifies.
SolvedMesh taggedEdge(double potentialAtX1, int tag)
{
  SolvedMesh m;
  SolutionNode n;
  n.x = 0; n.y = 0; n.potentialRe = 0; n.conductor = tag;              m.nodes << n;
  n.x = 1; n.y = 0; n.potentialRe = 0; n.conductor = tag;              m.nodes << n;
  n.x = 0; n.y = 1; n.potentialRe = potentialAtX1; n.conductor = 0;    m.nodes << n;

  SolutionElement e;
  e.p0 = 0; e.p1 = 1; e.p2 = 2; e.label = 0;
  m.elements << e;
  return m;
}

} // namespace

void TestSolutionIntegrals::aConductorReportsItsEquipotentialValue()
{
  SolvedMesh m = taggedEdge(50.0, 1);
  // Put both tagged nodes at the same potential, as a real conductor is.
  m.nodes[0].potentialRe = 7.0;
  m.nodes[1].potentialRe = 7.0;

  FemmProblem p = squareProblem(FemmProblemKind::HeatFlow);
  FemmHtMaterialProp mat;
  mat.Kx = 1.0; mat.Ky = 1.0;
  p.htMaterialProps << mat;
  FemmConductorProp c;
  c.name = "sink";
  p.conductorProps << c;

  const auto r = ConductorAnalysis::analyse(m, p, 1);
  QVERIFY(r.ok);
  QCOMPARE(r.name, QStringLiteral("sink"));
  QCOMPARE(r.nodeCount, 2);
  QVERIFY(close(r.potentialRe, 7.0));
  // A real conductor is an equipotential, so the spread is the check on
  // whether the tag actually describes one.
  QVERIFY2(close(r.potentialSpread, 0.0, 1e-12) || r.potentialSpread == 0.0,
      qPrintable(QString::number(r.potentialSpread)));

  // And a tag spanning nodes at different potentials reports the spread
  // rather than hiding it in an average.
  m.nodes[1].potentialRe = 9.0;
  const auto r2 = ConductorAnalysis::analyse(m, p, 1);
  QVERIFY(close(r2.potentialRe, 8.0));
  QVERIFY2(close(r2.potentialSpread, 2.0),
      "a conductor spanning two potentials should report the spread");
}

void TestSolutionIntegrals::aConductorReportsTheFluxLeavingItsSurface()
{
  // T = 0 along y = 0 and 100 at (0,1), so grad T is (0, 100) K/m over a
  // 1 m triangle, and the heat flux is -k*grad T = (0, -100) W/m^2 with
  // k = 1. The tagged edge is the bottom, whose OUTWARD normal (away
  // from the element's centroid, which is above it) is (0, -1). So the
  // flux out is (0,-100) . (0,-1) * 1 m = +100 W/m.
  const SolvedMesh m = taggedEdge(100.0, 1);
  FemmProblem p = squareProblem(FemmProblemKind::HeatFlow);
  FemmHtMaterialProp mat;
  mat.Kx = 1.0; mat.Ky = 1.0;
  p.htMaterialProps << mat;
  FemmConductorProp c;
  p.conductorProps << c;

  const auto r = ConductorAnalysis::analyse(m, p, 1);
  QVERIFY(r.ok);
  QVERIFY2(close(r.surfaceLength, 1.0),
      qPrintable(QStringLiteral("expected a 1 m surface, got %1").arg(r.surfaceLength)));
  QVERIFY2(close(r.fluxRe, 100.0, 1e-9),
      qPrintable(QStringLiteral("expected +100 W/m leaving the surface, got %1")
                     .arg(r.fluxRe, 0, 'g', 10)));
}

void TestSolutionIntegrals::anUntaggedConductorIsAbsentNotZero()
{
  // Asking about a conductor no node carries is not an error and is not
  // a reading of zero -- it is simply not in this solution, and a
  // caller must be able to tell those apart.
  const SolvedMesh m = taggedEdge(100.0, 1);
  FemmProblem p = squareProblem(FemmProblemKind::HeatFlow);
  FemmHtMaterialProp mat;
  p.htMaterialProps << mat;

  const auto r = ConductorAnalysis::analyse(m, p, 4);
  QVERIFY2(!r.ok, "a conductor no node carries reported a result");
  QCOMPARE(r.nodeCount, 0);

  // 0 means "no conductor" in the file format and is never a valid tag.
  QVERIFY(!ConductorAnalysis::analyse(m, p, 0).ok);
}

void TestSolutionIntegrals::conductorQuantitiesAreNamedPerPhysicsAndAbsentForMagnetics()
{
  QCOMPARE(ConductorAnalysis::fluxQuantity(FemmProblemKind::Electrostatics).unit,
      QStringLiteral("C/m"));
  QCOMPARE(ConductorAnalysis::fluxQuantity(FemmProblemKind::HeatFlow).unit,
      QStringLiteral("W/m"));
  QCOMPARE(ConductorAnalysis::fluxQuantity(FemmProblemKind::CurrentFlow).unit,
      QStringLiteral("A/m"));
  QCOMPARE(ConductorAnalysis::potentialQuantity(FemmProblemKind::HeatFlow).unit,
      QStringLiteral("K"));

  // Magnetics has circuits, not conductors. Offering it an empty-named
  // conductor readout is how a UI ends up showing a blank panel instead
  // of the circuit one it should.
  QVERIFY2(ConductorAnalysis::fluxQuantity(FemmProblemKind::Magnetics).name.isEmpty(),
      "magnetics was given a conductor quantity");
  QVERIFY(ConductorAnalysis::potentialQuantity(FemmProblemKind::Magnetics).name.isEmpty());
}

QTEST_GUILESS_MAIN(TestSolutionIntegrals)
#include "tst_solution_integrals.moc"
