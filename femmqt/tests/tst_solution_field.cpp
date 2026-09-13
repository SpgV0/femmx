// tst_solution_field.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #83).
//
// The derived field quantities: B, E, heat flux and current density,
// each from the gradient of the one scalar its solver stored.
//
// These are checked against ANALYTIC answers, not against recorded
// output. A linear potential over a triangle has an exactly constant
// gradient that can be written down -- so if the potential is V = 2x + 3y
// then grad V is (2, 3) everywhere in the element, in per-model-unit
// terms, and the expected field follows from the physics with no
// arithmetic of mine to be wrong about.
//
// That matters more here than usual. A field that is right in shape and
// wrong by a constant -- a unit conversion missed, a relative
// permittivity not multiplied by eps0, a conductivity read as MS/m when
// the format stores S/m -- is invisible on a plot with an auto-scaled
// legend. It looks exactly like a correct plot. So every case below
// pins the MAGNITUDE against a number derived from the definition, not
// just the direction.

#include <QtTest>

#include "FemmProblem.h"
#include "SolutionField.h"
#include "SolutionFileIO.h"

#include <cmath>

namespace {

// One right triangle with corners at (0,0), (1,0), (0,1) in model units,
// carrying a potential that is linear in x and y: P = p0 + gx*x + gy*y.
// Its gradient is therefore exactly (gx, gy) per model unit.
SolvedMesh linearPotential(double gx, double gy, double p0 = 0.0)
{
  SolvedMesh m;
  SolutionNode a, b, c;
  a.x = 0; a.y = 0; a.potentialRe = p0;
  b.x = 1; b.y = 0; b.potentialRe = p0 + gx;
  c.x = 0; c.y = 1; c.potentialRe = p0 + gy;
  m.nodes << a << b << c;

  SolutionElement e;
  e.p0 = 0; e.p1 = 1; e.p2 = 2;
  e.label = 0;
  m.elements << e;
  return m;
}

// A problem with one block label pointing at material 1 of its kind.
FemmProblem problemWith(FemmProblemKind kind, FemmLengthUnits units)
{
  FemmProblem p;
  p.kind = kind;
  p.lengthUnits = units;
  FemmBlockLabel lbl;
  lbl.blockTypeIndex = 1; // 1-based
  p.blockLabels << lbl;
  return p;
}

constexpr double kEps0 = 8.8541878128e-12;

bool close(double a, double b, double rel = 1e-9)
{
  const double scale = std::max({ std::abs(a), std::abs(b), 1e-300 });
  return std::abs(a - b) <= rel * scale;
}

} // namespace

class TestSolutionField : public QObject
{
  Q_OBJECT

  private slots:
  void theGradientOfALinearPotentialIsExact();
  void theGradientScalesWithTheModelsLengthUnits();
  void aDegenerateElementIsRefusedRatherThanReturningNaN();

  void magneticsFieldIsTheCurlOfANotItsNegativeGradient();
  void electrostaticsMultipliesRelativePermittivityByEpsilonZero();
  void heatFluxUsesTheMaterialsConductivity();
  void currentDensityTreatsConductivityAsSiemensPerMetre();
  void anisotropicMaterialsScaleTheAxesIndependently();

  void everyKindListsItsOwnPlotQuantities();
  void theFieldLabelNamesTheFieldThatIsActuallyComputed();
  void aHoleBehavesAsFreeSpaceRatherThanAsZero();
};

// ---------------------------------------------------------------------------
// The shared half
// ---------------------------------------------------------------------------

void TestSolutionField::theGradientOfALinearPotentialIsExact()
{
  // P = 2x + 3y over a triangle in METRES, so grad P = (2, 3) exactly.
  const SolvedMesh m = linearPotential(2.0, 3.0);
  const FemmProblem p = problemWith(FemmProblemKind::HeatFlow, FemmLengthUnits::Meters);

  SolutionField::Vector2 g;
  QVERIFY(SolutionField::potentialGradient(m, p, 0, g));
  QVERIFY2(close(g.xRe, 2.0), qPrintable(QString::number(g.xRe, 'g', 17)));
  QVERIFY2(close(g.yRe, 3.0), qPrintable(QString::number(g.yRe, 'g', 17)));
  QCOMPARE(g.xIm, 0.0);
  QCOMPARE(g.yIm, 0.0);
}

void TestSolutionField::theGradientScalesWithTheModelsLengthUnits()
{
  // The same numbers in millimetres describe a triangle a thousand times
  // smaller, so the gradient per METRE is a thousand times larger. This
  // is the conversion that is invisible when it is wrong: the plot has
  // the right shape and the legend has the wrong numbers.
  const SolvedMesh m = linearPotential(2.0, 3.0);
  const FemmProblem mm = problemWith(FemmProblemKind::HeatFlow, FemmLengthUnits::Millimeters);

  SolutionField::Vector2 g;
  QVERIFY(SolutionField::potentialGradient(m, mm, 0, g));
  QVERIFY2(close(g.xRe, 2000.0),
      qPrintable(QStringLiteral("expected 2000 K/m across a 1 mm element, got %1")
                     .arg(g.xRe, 0, 'g', 17)));
  QVERIFY2(close(g.yRe, 3000.0), qPrintable(QString::number(g.yRe, 'g', 17)));
}

void TestSolutionField::aDegenerateElementIsRefusedRatherThanReturningNaN()
{
  // triangle can emit zero-area elements at sharp corners. Dividing by
  // that area puts a NaN into the plot's auto-range, which flattens the
  // whole legend and reads as a solver failure rather than one bad
  // triangle.
  SolvedMesh m = linearPotential(1.0, 1.0);
  m.nodes[2].x = 2; // all three corners now collinear with (0,0),(1,0)
  m.nodes[2].y = 0;
  const FemmProblem p = problemWith(FemmProblemKind::HeatFlow, FemmLengthUnits::Meters);

  SolutionField::Vector2 g;
  QVERIFY2(!SolutionField::potentialGradient(m, p, 0, g),
      "a zero-area element produced a gradient instead of being refused");

  SolutionField::Vector2 f;
  QVERIFY(!SolutionField::elementField(m, p, 0, f));
}

// ---------------------------------------------------------------------------
// The per-physics half
// ---------------------------------------------------------------------------

void TestSolutionField::magneticsFieldIsTheCurlOfANotItsNegativeGradient()
{
  // B = (dA/dy, -dA/dx). Writing it as -grad A instead gives a field
  // rotated by 90 degrees -- the same magnitude, a plausible-looking
  // plot, and wrong everywhere. With A = 2x + 3y the answer is (3, -2).
  const SolvedMesh m = linearPotential(2.0, 3.0);
  const FemmProblem p = problemWith(FemmProblemKind::Magnetics, FemmLengthUnits::Meters);

  SolutionField::Vector2 b;
  QVERIFY(SolutionField::elementField(m, p, 0, b));
  QVERIFY2(close(b.xRe, 3.0),
      qPrintable(QStringLiteral("Bx should be dA/dy = 3, got %1").arg(b.xRe)));
  QVERIFY2(close(b.yRe, -2.0),
      qPrintable(QStringLiteral("By should be -dA/dx = -2, got %1").arg(b.yRe)));
  QVERIFY(close(b.magnitude(), std::hypot(2.0, 3.0)));
}

void TestSolutionField::electrostaticsMultipliesRelativePermittivityByEpsilonZero()
{
  // The format stores RELATIVE permittivity, so D = eps0 * er * E with
  // E = -grad V. Forgetting eps0 gives a D about 1e11 times too large,
  // which an auto-scaled legend hides completely.
  const SolvedMesh m = linearPotential(2.0, 3.0);
  FemmProblem p = problemWith(FemmProblemKind::Electrostatics, FemmLengthUnits::Meters);
  FemmEsMaterialProp mat;
  mat.ex = 4.0;
  mat.ey = 4.0;
  p.esMaterialProps << mat;

  SolutionField::Vector2 d;
  QVERIFY(SolutionField::elementField(m, p, 0, d));
  QVERIFY2(close(d.xRe, -2.0 * kEps0 * 4.0),
      qPrintable(QStringLiteral("expected %1, got %2")
                     .arg(-2.0 * kEps0 * 4.0, 0, 'g', 6).arg(d.xRe, 0, 'g', 6)));
  QVERIFY(close(d.yRe, -3.0 * kEps0 * 4.0));
}

void TestSolutionField::heatFluxUsesTheMaterialsConductivity()
{
  // F = -k grad T, with k already in W/(m*K).
  const SolvedMesh m = linearPotential(10.0, 0.0);
  FemmProblem p = problemWith(FemmProblemKind::HeatFlow, FemmLengthUnits::Meters);
  FemmHtMaterialProp mat;
  mat.Kx = 401.0; // copper
  mat.Ky = 401.0;
  p.htMaterialProps << mat;

  SolutionField::Vector2 f;
  QVERIFY(SolutionField::elementField(m, p, 0, f));
  // 10 K/m down a 401 W/(m*K) conductor: 4010 W/m^2, flowing from hot to
  // cold, hence negative.
  QVERIFY2(close(f.xRe, -4010.0),
      qPrintable(QStringLiteral("expected -4010 W/m2, got %1").arg(f.xRe)));
  QVERIFY(close(f.yRe, 0.0, 1e-6) || f.yRe == 0.0);
}

void TestSolutionField::currentDensityTreatsConductivityAsSiemensPerMetre()
{
  // .fec stores <ox>/<oy> in S/m -- NOT the MS/m that magnetics' <Sigma>
  // uses. Reading it as MS/m would give a current density a million
  // times too small, and the plot would still look like a plot.
  const SolvedMesh m = linearPotential(2.0, 0.0);
  FemmProblem p = problemWith(FemmProblemKind::CurrentFlow, FemmLengthUnits::Meters);
  FemmCfMaterialProp mat;
  mat.ox = 58.0e6; // copper, S/m, exactly as condlib.dat stores it
  mat.oy = 58.0e6;
  p.cfMaterialProps << mat;

  SolutionField::Vector2 j;
  QVERIFY(SolutionField::elementField(m, p, 0, j));
  QVERIFY2(close(j.xRe, -2.0 * 58.0e6),
      qPrintable(QStringLiteral("expected %1 A/m2, got %2")
                     .arg(-2.0 * 58.0e6, 0, 'g', 6).arg(j.xRe, 0, 'g', 6)));
}

void TestSolutionField::anisotropicMaterialsScaleTheAxesIndependently()
{
  // All three of the non-magnetics formats carry separate x and y
  // material constants. Applying one of them to both axes is a mistake
  // that is invisible in any isotropic model -- which is most of them.
  const SolvedMesh m = linearPotential(1.0, 1.0);
  FemmProblem p = problemWith(FemmProblemKind::HeatFlow, FemmLengthUnits::Meters);
  FemmHtMaterialProp mat;
  mat.Kx = 100.0;
  mat.Ky = 7.0;
  p.htMaterialProps << mat;

  SolutionField::Vector2 f;
  QVERIFY(SolutionField::elementField(m, p, 0, f));
  QVERIFY2(close(f.xRe, -100.0), qPrintable(QString::number(f.xRe)));
  QVERIFY2(close(f.yRe, -7.0),
      qPrintable(QStringLiteral("ky was not used for the y component; got %1")
                     .arg(f.yRe)));
}

// ---------------------------------------------------------------------------

void TestSolutionField::everyKindListsItsOwnPlotQuantities()
{
  for (FemmProblemKind kind : { FemmProblemKind::Magnetics,
           FemmProblemKind::Electrostatics, FemmProblemKind::HeatFlow,
           FemmProblemKind::CurrentFlow }) {
    const QVector<SolutionField::Quantity> qs = SolutionField::quantities(kind);
    QVERIFY2(qs.size() >= 3,
        "a physics offered fewer than three plottable quantities");
    for (const SolutionField::Quantity& q : qs) {
      QVERIFY2(!q.name.isEmpty(), "a plot quantity has no name");
      // The unit is the part that makes a legend mean anything.
      QVERIFY2(!q.unit.isEmpty(),
          qPrintable(QStringLiteral("\"%1\" has no unit").arg(q.name)));
    }
    const SolutionField::Quantity pot = SolutionField::potentialQuantity(kind);
    QVERIFY(!pot.name.isEmpty() && !pot.unit.isEmpty());
  }

  // The stored potential differs by physics and the names are not
  // interchangeable -- a plot labelled A when it is showing T is worse
  // than an unlabelled one.
  QCOMPARE(SolutionField::potentialQuantity(FemmProblemKind::Magnetics).name,
      QStringLiteral("A"));
  QCOMPARE(SolutionField::potentialQuantity(FemmProblemKind::HeatFlow).name,
      QStringLiteral("T"));
  QCOMPARE(SolutionField::potentialQuantity(FemmProblemKind::HeatFlow).unit,
      QStringLiteral("K"));
}

// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13 (#87).
void TestSolutionField::theFieldLabelNamesTheFieldThatIsActuallyComputed()
{
  // The legend over a density plot has to name the quantity the plot is
  // showing, and this is not a cosmetic requirement: the numbers on it
  // look plausible either way.
  //
  // It was wrong for electrostatics. The label was taken as
  // quantities(kind)[1] -- an index into a list ordered for a menu (V,
  // |E|, |D|) -- while SolutionAdapter carries elementField()'s result
  // onto the renderer, and elementField returns D. So the legend read
  // "Field intensity |E|, V/m" over a plot of |D| in C/m^2: wrong
  // quantity, wrong units, off by a factor of eps0*er.
  //
  // Both halves are pinned here, because either alone can drift: the
  // NAME the label uses, and the VALUE elementField returns. A unit
  // string cannot be derived from arithmetic, so the agreement between
  // them is asserted as a pair.

  // What each kind's field is called.
  QCOMPARE(SolutionField::fieldQuantity(FemmProblemKind::Magnetics).unit,
      QStringLiteral("T"));
  QCOMPARE(SolutionField::fieldQuantity(FemmProblemKind::Electrostatics).unit,
      QStringLiteral("C/m^2"));
  QCOMPARE(SolutionField::fieldQuantity(FemmProblemKind::HeatFlow).unit,
      QStringLiteral("W/m^2"));
  QCOMPARE(SolutionField::fieldQuantity(FemmProblemKind::CurrentFlow).unit,
      QStringLiteral("A/m^2"));

  QVERIFY2(SolutionField::fieldQuantity(FemmProblemKind::Electrostatics)
               .name.contains(QStringLiteral("|D|")),
      "the electrostatic field label does not name |D|, which is what "
      "elementField returns -- this is the exact defect #87 found");

  // And what the maths actually returns, for a unit potential gradient.
  // A linear potential V = 2x over the unit triangle gives grad V =
  // (2, 0), so with er = 1 the field is D = -eps0 * 2 -- a number in
  // C/m^2, not the -2 V/m that an |E| label would imply.
  {
    const SolvedMesh m = linearPotential(2.0, 0.0);
    FemmProblem p = problemWith(FemmProblemKind::Electrostatics, FemmLengthUnits::Meters);
    FemmEsMaterialProp mat;
    mat.ex = 1.0;
    mat.ey = 1.0;
    p.esMaterialProps << mat;
    // problemWith already points the label at this one -- blockTypeIndex
    // is 1-BASED, and setting it to 0 would make the label a hole, which
    // behaves as free space and would let this pass without ever reading
    // the material.

    SolutionField::Vector2 f;
    QVERIFY(SolutionField::elementField(m, p, 0, f));
    QVERIFY2(close(f.xRe, -2.0 * kEps0),
        qPrintable(QStringLiteral("expected D = %1 C/m^2, got %2. If this is -2, "
                                  "the field is E and the label has to say so.")
                       .arg(-2.0 * kEps0, 0, 'g', 6).arg(f.xRe, 0, 'g', 6)));
  }

  // Heat flow: F = -k grad T, so with k = 5 and grad T = (2, 0) the
  // flux is -10 W/m^2 -- the label's W/m^2, not the K/m of |G|.
  {
    const SolvedMesh m = linearPotential(2.0, 0.0);
    FemmProblem p = problemWith(FemmProblemKind::HeatFlow, FemmLengthUnits::Meters);
    FemmHtMaterialProp mat;
    mat.Kx = 5.0;
    mat.Ky = 5.0;
    p.htMaterialProps << mat;

    SolutionField::Vector2 f;
    QVERIFY(SolutionField::elementField(m, p, 0, f));
    QVERIFY2(close(f.xRe, -10.0),
        qPrintable(QStringLiteral("expected F = -10 W/m^2, got %1. If this is -2, "
                                  "the field is the gradient |G| in K/m.")
                       .arg(f.xRe, 0, 'g', 6)));
  }
}

void TestSolutionField::aHoleBehavesAsFreeSpaceRatherThanAsZero()
{
  // A block label with no material is a hole. For electrostatics that
  // should behave as free space (er = 1), not as er = 0, which would
  // show every hole as a region of exactly zero flux density.
  const SolvedMesh m = linearPotential(2.0, 0.0);
  FemmProblem p = problemWith(FemmProblemKind::Electrostatics, FemmLengthUnits::Meters);
  p.blockLabels[0].blockTypeIndex = -1; // a hole

  SolutionField::Vector2 d;
  QVERIFY(SolutionField::elementField(m, p, 0, d));
  QVERIFY2(close(d.xRe, -2.0 * kEps0),
      qPrintable(QStringLiteral("a hole should behave as free space; expected %1, got %2")
                     .arg(-2.0 * kEps0, 0, 'g', 6).arg(d.xRe, 0, 'g', 6)));
}

QTEST_GUILESS_MAIN(TestSolutionField)
#include "tst_solution_field.moc"
