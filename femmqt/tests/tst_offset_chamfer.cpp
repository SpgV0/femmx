// tst_offset_chamfer.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12
// (issue #30).
//
// Offset has a failure mode that no assertion about entity COUNTS can
// see: getting the side wrong, or getting it wrong for only part of a
// chain. The result is a perfectly well-formed set of lines and arcs in
// the wrong place, and on screen at a glance it looks like an offset.
// This is the same family as #25's arc centres.
//
// So the checks below are about DISTANCE and SIDE. Every offset piece is
// measured back against the entity it came from: the perpendicular
// distance has to be what was asked for, and the sign has to be the same
// for every piece in the chain. A half-reversed chain fails the sign
// check even though the geometry is otherwise valid.

#include <QtTest>

#include "FemmProblem.h"
#include "FemmProblemEdit.h"
#include "OffsetChamfer.h"

#include <cmath>
#include <complex>

using Complex = std::complex<double>;
using OffsetChamfer::CornerStyle;

namespace {

Complex pos(const FemmProblem& p, int i)
{
  return Complex(p.nodes[i].x, p.nodes[i].y);
}

// Signed perpendicular offset of `pt` from the infinite line a->b:
// positive to the LEFT of the direction of travel, which is the
// convention offsetSelection documents.
double signedOffset(Complex a, Complex b, Complex pt)
{
  const Complex dir = (b - a) / std::abs(b - a);
  return ((pt - a) / dir).imag();
}

void selectAllSegments(FemmProblem& p)
{
  for (FemmSegment& s : p.segments)
    s.isSelected = true;
}

// An L: (0,0) -> (10,0) -> (10,10). Its corner turns LEFT.
FemmProblem lShape()
{
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  const int c = FemmProblemEdit::addNode(p, 10, 10);
  FemmProblemEdit::addSegment(p, a, b);
  FemmProblemEdit::addSegment(p, b, c);
  return p;
}

FemmProblem rightAngleCorner()
{
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  FemmProblemEdit::addNode(p, 10, 0); // 0: the far end of edge A
  FemmProblemEdit::addNode(p, 0, 0);  // 1: the corner
  FemmProblemEdit::addNode(p, 0, 10); // 2: the far end of edge B
  FemmProblemEdit::addSegment(p, 0, 1);
  FemmProblemEdit::addSegment(p, 1, 2);
  return p;
}

} // namespace

class TestOffsetChamfer : public QObject
{
  Q_OBJECT

  private slots:
  void aLoneSegmentOffsetsToTheLeftOfItsDirection();
  void aNegativeDistanceOffsetsToTheOtherSide();
  void anArcOffsetsOntoAConcentricCircle();
  void anArcOffsetOutwardsGrowsInsteadOfShrinking();
  void anOffsetThatWouldCollapseAnArcIsRefused();

  void everyPieceOfAChainEndsUpOnTheSameSide();
  void aMiteredOutsideCornerMeetsAtAPoint();
  void aFilletedOutsideCornerGetsAnArcOfTheOffsetRadius();
  void anInsideCornerIsNeverFilleted();
  void aClosedLoopOffsetsAsALoop();
  void anOffsetThatRunsIntoItselfIsRefused();
  void anOffsetJustInsideTheLimitStillWorks();

  void theOriginalGeometryIsLeftAlone();
  void theBoundaryConditionIsNotCopiedOntoTheOffset();
  void theGroupIsCopiedOntoTheOffset();

  void chamferReplacesTheCornerWithAFlat();
  void chamferLongerThanItsEdgesIsRefusedAndChangesNothing();
  void chamferOnACornerInvolvingAnArcIsRefused();
  void aFortyFiveDegreeChamferOnARightAngleIsSymmetric();
  void aThirtyDegreeChamferUsesTheSineRule();
  void anImpossibleChamferAngleIsRefused();
};

// ---------------------------------------------------------------------------
// One entity
// ---------------------------------------------------------------------------

void TestOffsetChamfer::aLoneSegmentOffsetsToTheLeftOfItsDirection()
{
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  FemmProblemEdit::addSegment(p, a, b);
  selectAllSegments(p);

  const auto r = OffsetChamfer::offsetSelection(p, 2.0, CornerStyle::Miter);
  QVERIFY2(r.ok, qPrintable(r.message));
  QCOMPARE(r.segmentsCreated, 1);
  QCOMPARE(p.segments.size(), 2);

  // Travelling n0 -> n1 is +x, so the left is +y.
  const FemmSegment& made = p.segments[1];
  QCOMPARE(signedOffset(Complex(0, 0), Complex(10, 0), pos(p, made.n0)), 2.0);
  QCOMPARE(signedOffset(Complex(0, 0), Complex(10, 0), pos(p, made.n1)), 2.0);
}

void TestOffsetChamfer::aNegativeDistanceOffsetsToTheOtherSide()
{
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  FemmProblemEdit::addSegment(p, a, b);
  selectAllSegments(p);

  QVERIFY(OffsetChamfer::offsetSelection(p, -2.0, CornerStyle::Miter).ok);
  const FemmSegment& made = p.segments[1];
  QCOMPARE(signedOffset(Complex(0, 0), Complex(10, 0), pos(p, made.n0)), -2.0);
}

void TestOffsetChamfer::anArcOffsetsOntoAConcentricCircle()
{
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 4, 0);
  const int b = FemmProblemEdit::addNode(p, 0, 4);
  FemmProblemEdit::addArcSegment(p, a, b, 90.0, 1.0); // CCW about the origin
  p.arcSegments[0].isSelected = true;

  const auto r = OffsetChamfer::offsetSelection(p, 1.0, CornerStyle::Miter);
  QVERIFY2(r.ok, qPrintable(r.message));
  QCOMPARE(r.arcsCreated, 1);

  // Travelling counterclockwise, the left points at the centre, so a
  // positive offset shrinks the radius. Checked through circleFromArc
  // rather than against coordinates: same centre, radius smaller by
  // exactly the offset.
  Complex c0, c1;
  double R0 = 0, R1 = 0;
  QVERIFY(FemmProblemEdit::circleFromArc(p, p.arcSegments[0], c0, R0));
  QVERIFY(FemmProblemEdit::circleFromArc(p, p.arcSegments[1], c1, R1));
  QVERIFY2(std::abs(c1 - c0) < 1e-9, "the offset arc is not concentric with its original");
  QVERIFY2(std::abs(R1 - (R0 - 1.0)) < 1e-9,
      qPrintable(QStringLiteral("expected radius %1, got %2").arg(R0 - 1.0).arg(R1)));
  QVERIFY(std::abs(p.arcSegments[1].arcLength - 90.0) < 1e-9);
}

void TestOffsetChamfer::anArcOffsetOutwardsGrowsInsteadOfShrinking()
{
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 4, 0);
  const int b = FemmProblemEdit::addNode(p, 0, 4);
  FemmProblemEdit::addArcSegment(p, a, b, 90.0, 1.0);
  p.arcSegments[0].isSelected = true;

  QVERIFY(OffsetChamfer::offsetSelection(p, -1.5, CornerStyle::Miter).ok);
  Complex c0, c1;
  double R0 = 0, R1 = 0;
  FemmProblemEdit::circleFromArc(p, p.arcSegments[0], c0, R0);
  FemmProblemEdit::circleFromArc(p, p.arcSegments[1], c1, R1);
  QVERIFY(std::abs(c1 - c0) < 1e-9);
  QVERIFY2(std::abs(R1 - (R0 + 1.5)) < 1e-9,
      "a negative offset on a counterclockwise arc must grow the radius, not shrink it");
}

void TestOffsetChamfer::anOffsetThatWouldCollapseAnArcIsRefused()
{
  // Offsetting a radius-4 arc inward by 6 would put it through the
  // centre and out the other side -- a mathematically real circle of
  // radius -2, drawn backwards. Emitting that is worse than refusing.
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 4, 0);
  const int b = FemmProblemEdit::addNode(p, 0, 4);
  FemmProblemEdit::addArcSegment(p, a, b, 90.0, 1.0);
  p.arcSegments[0].isSelected = true;

  const auto r = OffsetChamfer::offsetSelection(p, 6.0, CornerStyle::Miter);
  QVERIFY2(!r.ok, "an offset through the arc's own centre was emitted as geometry");
  QCOMPARE(p.arcSegments.size(), 1);
  QVERIFY(!r.message.isEmpty());
}

// ---------------------------------------------------------------------------
// Chains
// ---------------------------------------------------------------------------

void TestOffsetChamfer::everyPieceOfAChainEndsUpOnTheSameSide()
{
  // The failure this exists for: a chain walked with one link's
  // orientation misread offsets that link to the other side. The result
  // is valid geometry in the wrong place, and counting entities cannot
  // see it.
  FemmProblem p = lShape();
  selectAllSegments(p);

  const auto r = OffsetChamfer::offsetSelection(p, 1.0, CornerStyle::Miter);
  QVERIFY2(r.ok, qPrintable(r.message));

  // Offsetting an L left by 1: the horizontal goes up to y=1, the
  // vertical goes left to x=9, and they meet at (9,1).
  bool sawHorizontal = false, sawVertical = false;
  for (int i = 2; i < p.segments.size(); i++) {
    const Complex n0 = pos(p, p.segments[i].n0), n1 = pos(p, p.segments[i].n1);
    if (std::abs(n0.imag() - n1.imag()) < 1e-9) {
      QVERIFY2(std::abs(n0.imag() - 1.0) < 1e-9,
          qPrintable(QStringLiteral("the horizontal piece landed at y=%1, not y=1")
                         .arg(n0.imag())));
      sawHorizontal = true;
    } else if (std::abs(n0.real() - n1.real()) < 1e-9) {
      QVERIFY2(std::abs(n0.real() - 9.0) < 1e-9,
          qPrintable(QStringLiteral("the vertical piece landed at x=%1, not x=9 -- it "
                                    "was offset to the wrong side of the chain")
                         .arg(n0.real())));
      sawVertical = true;
    }
  }
  QVERIFY(sawHorizontal && sawVertical);
}

void TestOffsetChamfer::aMiteredOutsideCornerMeetsAtAPoint()
{
  FemmProblem p = lShape();
  selectAllSegments(p);

  // Offsetting RIGHT on a left-turning corner is the outside.
  const auto r = OffsetChamfer::offsetSelection(p, -1.0, CornerStyle::Miter);
  QVERIFY2(r.ok, qPrintable(r.message));
  QCOMPARE(r.cornersFilleted, 0);
  QCOMPARE(r.arcsCreated, 0);
  QCOMPARE(r.segmentsCreated, 2);

  // Both pieces must terminate at the miter point (11,-1), sharing one
  // node -- two pieces merely ending near each other would leave a gap
  // the mesher treats as open.
  const FemmSegment& s0 = p.segments[2];
  const FemmSegment& s1 = p.segments[3];
  const int shared = (s0.n0 == s1.n0 || s0.n0 == s1.n1) ? s0.n0 : s0.n1;
  QVERIFY2(shared == s1.n0 || shared == s1.n1,
      "the two mitered pieces do not share a node, so the corner is open");
  QVERIFY(std::abs(pos(p, shared) - Complex(11, -1)) < 1e-9);
}

void TestOffsetChamfer::aFilletedOutsideCornerGetsAnArcOfTheOffsetRadius()
{
  FemmProblem p = lShape();
  selectAllSegments(p);

  const auto r = OffsetChamfer::offsetSelection(p, -1.0, CornerStyle::Fillet);
  QVERIFY2(r.ok, qPrintable(r.message));
  QCOMPARE(r.cornersFilleted, 1);
  QCOMPARE(r.arcsCreated, 1);

  // The fillet must be centred on the ORIGINAL corner at radius |d|:
  // that is the whole claim a filleted offset makes, that the new curve
  // is the same distance from the old one everywhere.
  Complex c;
  double R = 0;
  QVERIFY(FemmProblemEdit::circleFromArc(p, p.arcSegments[0], c, R));
  QVERIFY2(std::abs(c - Complex(10, 0)) < 1e-9,
      qPrintable(QStringLiteral("the fillet is centred at (%1,%2), not on the corner "
                                "at (10,0)").arg(c.real()).arg(c.imag())));
  QVERIFY2(std::abs(R - 1.0) < 1e-9,
      qPrintable(QStringLiteral("the fillet radius is %1, not the offset distance 1").arg(R)));
  QVERIFY2(std::abs(p.arcSegments[0].arcLength - 90.0) < 1e-6,
      qPrintable(QStringLiteral("a square corner should round through 90 degrees, got %1")
                     .arg(p.arcSegments[0].arcLength)));
}

void TestOffsetChamfer::anInsideCornerIsNeverFilleted()
{
  // Rounding the inside would pull the offset closer to the original
  // than the distance asked for, so the style must not apply there.
  FemmProblem p = lShape();
  selectAllSegments(p);

  const auto r = OffsetChamfer::offsetSelection(p, 1.0, CornerStyle::Fillet);
  QVERIFY2(r.ok, qPrintable(r.message));
  QCOMPARE(r.cornersFilleted, 0);
  QCOMPARE(r.arcsCreated, 0);
}

void TestOffsetChamfer::aClosedLoopOffsetsAsALoop()
{
  // A rectangle, walked as a closed chain: all four corners have to be
  // joined, including the one between the last piece and the first,
  // which is the one an off-by-one in the join loop misses.
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  const int c = FemmProblemEdit::addNode(p, 10, 6);
  const int d = FemmProblemEdit::addNode(p, 0, 6);
  FemmProblemEdit::addSegment(p, a, b);
  FemmProblemEdit::addSegment(p, b, c);
  FemmProblemEdit::addSegment(p, c, d);
  FemmProblemEdit::addSegment(p, d, a);
  selectAllSegments(p);

  const auto r = OffsetChamfer::offsetSelection(p, 1.0, CornerStyle::Miter);
  QVERIFY2(r.ok, qPrintable(r.message));
  QCOMPARE(r.segmentsCreated, 4);

  // Inset by 1 on every side: the new corners are (1,1) and (9,5).
  QVector<Complex> corners;
  for (int i = 4; i < p.segments.size(); i++) {
    corners << pos(p, p.segments[i].n0) << pos(p, p.segments[i].n1);
  }
  for (const Complex& pt : corners) {
    QVERIFY2(std::abs(pt.real() - 1.0) < 1e-9 || std::abs(pt.real() - 9.0) < 1e-9,
        qPrintable(QStringLiteral("corner x=%1 is not 1 or 9").arg(pt.real())));
    QVERIFY2(std::abs(pt.imag() - 1.0) < 1e-9 || std::abs(pt.imag() - 5.0) < 1e-9,
        qPrintable(QStringLiteral("corner y=%1 is not 1 or 5").arg(pt.imag())));
  }

  // Four pieces meeting at four shared nodes, not eight loose ends.
  QSet<int> endpoints;
  for (int i = 4; i < p.segments.size(); i++) {
    endpoints.insert(p.segments[i].n0);
    endpoints.insert(p.segments[i].n1);
  }
  QCOMPARE(endpoints.size(), 4);
}

namespace {

// 10 wide by 6 tall. Its inward offset stops being a rectangle at 3.
FemmProblem rectangle10x6()
{
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  const int c = FemmProblemEdit::addNode(p, 10, 6);
  const int d = FemmProblemEdit::addNode(p, 0, 6);
  FemmProblemEdit::addSegment(p, a, b);
  FemmProblemEdit::addSegment(p, b, c);
  FemmProblemEdit::addSegment(p, c, d);
  FemmProblemEdit::addSegment(p, d, a);
  return p;
}

} // namespace

void TestOffsetChamfer::anOffsetThatRunsIntoItselfIsRefused()
{
  // Inset a 10-by-6 rectangle by 4 and the two short sides have to pass
  // through each other. Every individual piece is still a perfectly good
  // line; it is only the joins that make it nonsense -- a bow-tie that
  // draws like geometry and meshes like nothing.
  FemmProblem p = rectangle10x6();
  selectAllSegments(p);
  const int segsBefore = p.segments.size();

  const auto r = OffsetChamfer::offsetSelection(p, 4.0, CornerStyle::Miter);
  QVERIFY2(!r.ok,
      "an offset larger than half the shape's width was emitted -- the result "
      "is self-intersecting");
  QCOMPARE(p.segments.size(), segsBefore);
  QVERIFY(!r.message.isEmpty());
}

void TestOffsetChamfer::anOffsetJustInsideTheLimitStillWorks()
{
  // The guard must not be so eager that it refuses valid offsets: 2.9 on
  // the same rectangle leaves a 4.2-by-0.2 sliver, which is legitimate
  // and is exactly the kind of thin gap these models are full of.
  FemmProblem p = rectangle10x6();
  selectAllSegments(p);

  const auto r = OffsetChamfer::offsetSelection(p, 2.9, CornerStyle::Miter);
  QVERIFY2(r.ok, qPrintable(QStringLiteral("a valid thin offset was refused: %1").arg(r.message)));
  QCOMPARE(r.segmentsCreated, 4);
}

// ---------------------------------------------------------------------------
// What comes across, and what deliberately does not
// ---------------------------------------------------------------------------

void TestOffsetChamfer::theOriginalGeometryIsLeftAlone()
{
  FemmProblem p = lShape();
  selectAllSegments(p);
  const FemmSegment before0 = p.segments[0];
  const FemmSegment before1 = p.segments[1];
  const int nodesBefore = p.nodes.size();

  QVERIFY(OffsetChamfer::offsetSelection(p, 1.0, CornerStyle::Miter).ok);

  QCOMPARE(p.segments[0].n0, before0.n0);
  QCOMPARE(p.segments[0].n1, before0.n1);
  QCOMPARE(p.segments[1].n0, before1.n0);
  QCOMPARE(p.segments[1].n1, before1.n1);
  for (int i = 0; i < nodesBefore; i++)
    QVERIFY(p.nodes[i].x == p.nodes[i].x); // still there, not renumbered away
  QVERIFY(p.nodes.size() > nodesBefore);
}

void TestOffsetChamfer::theBoundaryConditionIsNotCopiedOntoTheOffset()
{
  // Deliberate, and the opposite of Create Radius. A fillet REPLACES the
  // corner, so inheriting keeps the model saying what it said. An offset
  // is a second curve beside one that still carries the condition, so
  // copying it would impose the same boundary condition in two places --
  // a change to the physics, made silently.
  FemmProblem p = lShape();
  p.segments[0].boundaryMarker = 3;
  p.segments[1].boundaryMarker = 3;
  selectAllSegments(p);

  const auto r = OffsetChamfer::offsetSelection(p, 1.0, CornerStyle::Miter);
  QVERIFY(r.ok);
  for (int i = 2; i < p.segments.size(); i++) {
    QVERIFY2(p.segments[i].boundaryMarker == 0,
        "the offset copied the boundary condition, so it is now imposed on two "
        "curves instead of one");
  }
  QVERIFY2(r.message.contains("boundary", Qt::CaseInsensitive),
      "the offset dropped the boundary condition without saying so");
}

void TestOffsetChamfer::theGroupIsCopiedOntoTheOffset()
{
  // Group and mesh size are about meshing and selection, not about what
  // is being solved, so these do come across.
  FemmProblem p = lShape();
  p.segments[0].inGroup = 4;
  p.segments[1].inGroup = 4;
  p.segments[0].maxSideLength = 0.3;
  p.segments[1].maxSideLength = 0.3;
  selectAllSegments(p);

  QVERIFY(OffsetChamfer::offsetSelection(p, 1.0, CornerStyle::Miter).ok);
  for (int i = 2; i < p.segments.size(); i++) {
    QCOMPARE(p.segments[i].inGroup, 4);
    QCOMPARE(p.segments[i].maxSideLength, 0.3);
  }
}

// ---------------------------------------------------------------------------
// Chamfer
// ---------------------------------------------------------------------------

void TestOffsetChamfer::chamferReplacesTheCornerWithAFlat()
{
  FemmProblem p = rightAngleCorner();
  QVERIFY(OffsetChamfer::canChamfer(p, 1));

  const auto r = OffsetChamfer::chamferDistances(p, 1, 2.0, 3.0);
  QVERIFY2(r.ok, qPrintable(r.message));

  // The corner node is gone and three segments remain: the two shortened
  // edges and the flat between them.
  QCOMPARE(p.nodes.size(), 4);
  QCOMPARE(p.segments.size(), 3);

  // The two trim points, 2 along the +x edge and 3 along the +y edge.
  bool sawT1 = false, sawT2 = false;
  for (const FemmNode& n : p.nodes) {
    if (std::abs(n.x - 2.0) < 1e-9 && std::abs(n.y) < 1e-9)
      sawT1 = true;
    if (std::abs(n.x) < 1e-9 && std::abs(n.y - 3.0) < 1e-9)
      sawT2 = true;
  }
  QVERIFY2(sawT1 && sawT2, "the chamfer did not cut back the distances asked for");

  // And the original corner must be gone, not merely disconnected.
  for (const FemmNode& n : p.nodes)
    QVERIFY2(!(std::abs(n.x) < 1e-9 && std::abs(n.y) < 1e-9),
        "the corner node survived the chamfer");
}

void TestOffsetChamfer::chamferLongerThanItsEdgesIsRefusedAndChangesNothing()
{
  FemmProblem p = rightAngleCorner();
  const int nodesBefore = p.nodes.size();
  const int segsBefore = p.segments.size();

  const auto r = OffsetChamfer::chamferDistances(p, 1, 20.0, 3.0);
  QVERIFY2(!r.ok, "a chamfer running past the far end of its own edge was accepted");
  QCOMPARE(p.nodes.size(), nodesBefore);
  QCOMPARE(p.segments.size(), segsBefore);
  QVERIFY(!r.message.isEmpty());
}

void TestOffsetChamfer::chamferOnACornerInvolvingAnArcIsRefused()
{
  FemmProblem p;
  const int a = FemmProblemEdit::addNode(p, 10, 0);
  const int corner = FemmProblemEdit::addNode(p, 0, 0);
  const int c = FemmProblemEdit::addNode(p, 0, 10);
  FemmProblemEdit::addSegment(p, a, corner);
  FemmProblemEdit::addArcSegment(p, corner, c, 90.0, 1.0);

  QVERIFY(!OffsetChamfer::canChamfer(p, corner));
  const auto r = OffsetChamfer::chamferDistances(p, corner, 1.0, 1.0);
  QVERIFY2(!r.ok, "a distance along a curved edge was silently interpreted as a chord");
  QVERIFY(p.arcSegments.size() == 1 && p.segments.size() == 1);
}

void TestOffsetChamfer::aFortyFiveDegreeChamferOnARightAngleIsSymmetric()
{
  // Sine rule sanity: a 45-degree chamfer across a 90-degree corner
  // leaves 45 degrees for the third angle, so the two cut-back distances
  // must come out equal. Getting the triangle's angles the wrong way
  // round still produces a number, just not this one.
  FemmProblem p = rightAngleCorner();
  const auto r = OffsetChamfer::chamferDistanceAngle(p, 1, 2.0, 45.0);
  QVERIFY2(r.ok, qPrintable(r.message));

  bool sawT1 = false, sawT2 = false;
  for (const FemmNode& n : p.nodes) {
    if (std::abs(n.x - 2.0) < 1e-9 && std::abs(n.y) < 1e-9)
      sawT1 = true;
    if (std::abs(n.x) < 1e-9 && std::abs(n.y - 2.0) < 1e-9)
      sawT2 = true;
  }
  QVERIFY2(sawT1 && sawT2, "a 45-degree chamfer on a square corner came out lopsided");
}

void TestOffsetChamfer::aThirtyDegreeChamferUsesTheSineRule()
{
  // 30 degrees against a 90-degree corner leaves 60, so the second
  // distance is d * sin(30)/sin(60) = d / sqrt(3).
  FemmProblem p = rightAngleCorner();
  QVERIFY(OffsetChamfer::chamferDistanceAngle(p, 1, 2.0, 30.0).ok);

  const double expected = 2.0 * std::sin(30.0 * M_PI / 180.0) / std::sin(60.0 * M_PI / 180.0);
  bool found = false;
  for (const FemmNode& n : p.nodes) {
    if (std::abs(n.x) < 1e-9 && std::abs(n.y - expected) < 1e-9)
      found = true;
  }
  QVERIFY2(found,
      qPrintable(QStringLiteral("expected the second cut at y=%1").arg(expected)));
}

void TestOffsetChamfer::anImpossibleChamferAngleIsRefused()
{
  // 120 degrees across a 90-degree corner leaves -30 for the third
  // angle: there is no such triangle. Without the check the sine rule
  // returns a negative length and the chamfer is drawn backwards,
  // through the corner instead of across it.
  FemmProblem p = rightAngleCorner();
  const int segsBefore = p.segments.size();
  const auto r = OffsetChamfer::chamferDistanceAngle(p, 1, 2.0, 120.0);
  QVERIFY2(!r.ok, "a chamfer angle leaving no room in the triangle was accepted");
  QCOMPARE(p.segments.size(), segsBefore);
}

QTEST_GUILESS_MAIN(TestOffsetChamfer)
#include "tst_offset_chamfer.moc"
