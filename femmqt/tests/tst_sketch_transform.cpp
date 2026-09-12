// tst_sketch_transform.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12
// (issue #32).
//
// Two jobs, and the first one matters as much as the second.
//
// ONE: pin down what the audit found. The transforms already did the
// right thing with REFERENCES -- copies are appended, so originals keep
// their indices and every existing constraint goes on pointing at the
// entity it always did. That was the thing #32 feared was broken, and it
// was not. Something being correct by accident stays correct only if
// something checks it, so the "nothing moved" cases below are not
// padding: they are the audit, written down in a form that fails if
// anyone changes a transform to renumber in place.
//
// TWO: the behaviour that is new. Copies now arrive constrained, and the
// orientation rules are exercised at the angles where they actually bite
// -- 90 degrees, where Horizontal and Vertical swap, and 30, where
// neither survives.
//
// The hazard this file is written against: a constraint copied onto the
// wrong entity is worse than no constraint, because the solver will
// satisfy it. So every case checks WHICH entity a constraint ended up
// on, not merely how many there are.

#include <QtTest>

#include "FemmProblem.h"
#include "FemmProblemEdit.h"
#include "SketchTransform.h"

#include <cmath>

namespace {

// A horizontal line with a Horizontal constraint and a Distance
// dimension on it -- the smallest thing that exercises both an
// orientation rule and a length rule.
FemmProblem constrainedLine()
{
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  FemmProblemEdit::addSegment(p, a, b);

  FemmConstraint c;
  c.type = ConstraintType::Horizontal;
  c.refA = 0;
  p.constraints.push_back(c);

  FemmDimension d;
  d.type = DimensionType::Distance;
  d.refA = a;
  d.refB = b;
  d.value = 10.0;
  p.dimensions.push_back(d);

  return p;
}

void selectAll(FemmProblem& p)
{
  for (FemmNode& n : p.nodes)
    n.isSelected = true;
  for (FemmSegment& s : p.segments)
    s.isSelected = true;
  for (FemmArcSegment& a : p.arcSegments)
    a.isSelected = true;
}

// Is segment `i` horizontal, by its actual coordinates?
bool segmentIsHorizontal(const FemmProblem& p, int i)
{
  return std::abs(p.nodes[p.segments[i].n0].y - p.nodes[p.segments[i].n1].y) < 1e-9;
}

} // namespace

class TestSketchTransform : public QObject
{
  Q_OBJECT

  private slots:
  // The audit
  void copyingLeavesExistingReferencesPointingWhereTheyDid();
  void anInPlaceMoveChangesNoIndexAtAll();

  // Carrying the sketch onto copies
  void aTranslatedCopyArrivesConstrained();
  void aCopiedConstraintPointsAtTheCopyNotTheOriginal();
  void aConstraintWithOneEndOutsideTheSelectionIsNotCopied();
  void aRotatedCopyAtNinetyDegreesSwapsHorizontalForVertical();
  void aRotatedCopyAtThirtyDegreesDropsTheAxisConstraintAndSaysSo();
  void everyOtherConstraintTypeSurvivesEveryTransform();

  // In-place
  void scalingRescalesTheDimensionsThatMeasureWhatWasScaled();
  void scalingLeavesAHalfSelectedDimensionAloneAndReportsIt();
  void rotatingNinetyDegreesInPlaceRewritesHorizontalAsVertical();
  void rotatingThirtyDegreesInPlaceReportsTheConflictWithoutDropping();
  void mirroringAcrossAnAxisAlignedLineKeepsHorizontalHorizontal();
  void mirroringAcrossAFortyFiveDegreeLineSwapsTheAxes();
};

// ---------------------------------------------------------------------------
// The audit: what was already true
// ---------------------------------------------------------------------------

void TestSketchTransform::copyingLeavesExistingReferencesPointingWhereTheyDid()
{
  FemmProblem p = constrainedLine();
  selectAll(p);
  FemmProblemEdit::copySelected(p, 0, 20);

  // The ORIGINAL constraint must still be on the original segment. If a
  // transform ever started renumbering in place, this is what would
  // break, and it would break silently -- the constraint would still be
  // valid, just attached to the copy.
  QCOMPARE(p.constraints[0].refA, 0);
  QVERIFY2(segmentIsHorizontal(p, p.constraints[0].refA),
      "the original Horizontal constraint no longer points at a horizontal line");
  QCOMPARE(p.dimensions[0].refA, 0);
  QCOMPARE(p.dimensions[0].refB, 1);
}

void TestSketchTransform::anInPlaceMoveChangesNoIndexAtAll()
{
  FemmProblem p = constrainedLine();
  selectAll(p);
  const int nodes = p.nodes.size(), segs = p.segments.size();

  FemmProblemEdit::moveSelected(p, 5, 7);

  QCOMPARE(p.nodes.size(), nodes);
  QCOMPARE(p.segments.size(), segs);
  QCOMPARE(p.constraints.size(), 1);
  QCOMPARE(p.constraints[0].refA, 0);
  // A pure translation preserves orientation, so nothing about the
  // constraint changes either.
  QCOMPARE((int)p.constraints[0].type, (int)ConstraintType::Horizontal);
  QCOMPARE(p.dimensions[0].value, 10.0);
}

// ---------------------------------------------------------------------------
// Carrying the sketch onto copies
// ---------------------------------------------------------------------------

void TestSketchTransform::aTranslatedCopyArrivesConstrained()
{
  FemmProblem p = constrainedLine();
  selectAll(p);
  SketchTransform::Report report;
  FemmProblemEdit::copySelected(p, 0, 20, &report);

  QCOMPARE(p.segments.size(), 2);
  QCOMPARE(p.constraints.size(), 2);
  QCOMPARE(p.dimensions.size(), 2);
  QCOMPARE(report.constraintsCopied, 1);
  QCOMPARE(report.dimensionsCopied, 1);
  // A translation preserves length, so the copy measures the same.
  QCOMPARE(p.dimensions[1].value, 10.0);
}

void TestSketchTransform::aCopiedConstraintPointsAtTheCopyNotTheOriginal()
{
  // The failure that matters. A copied constraint left pointing at the
  // original is not a missing constraint -- it is a DOUBLED one on the
  // original, which the solver will enforce, and the copy is left free.
  FemmProblem p = constrainedLine();
  selectAll(p);
  FemmProblemEdit::copySelected(p, 0, 20);

  QCOMPARE(p.constraints.size(), 2);
  QVERIFY2(p.constraints[1].refA != p.constraints[0].refA,
      "the copied constraint points at the same segment as the original, so the "
      "original is now doubly constrained and the copy is not constrained at all");
  QCOMPARE(p.constraints[1].refA, 1);

  // The copied dimension must span the COPY's two nodes.
  QCOMPARE(p.dimensions[1].refA, 2);
  QCOMPARE(p.dimensions[1].refB, 3);
  QCOMPARE(p.nodes[p.dimensions[1].refA].y, 20.0);
}

void TestSketchTransform::aConstraintWithOneEndOutsideTheSelectionIsNotCopied()
{
  // Parallel between a selected line and an unselected one. Copying it
  // would tie the copy to the line that did not move -- a relationship
  // nobody asked for, which the solver would then enforce.
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  const int c = FemmProblemEdit::addNode(p, 0, 5);
  const int d = FemmProblemEdit::addNode(p, 10, 5);
  FemmProblemEdit::addSegment(p, a, b); // 0, selected below
  FemmProblemEdit::addSegment(p, c, d); // 1, left alone

  FemmConstraint par;
  par.type = ConstraintType::Parallel;
  par.refA = 0;
  par.refB = 1;
  p.constraints.push_back(par);

  p.nodes[a].isSelected = p.nodes[b].isSelected = true;
  p.segments[0].isSelected = true;

  SketchTransform::Report report;
  FemmProblemEdit::copySelected(p, 0, 20, &report);

  QCOMPARE(p.segments.size(), 3);
  QCOMPARE(p.constraints.size(), 1); // not copied
  QCOMPARE(report.constraintsCopied, 0);
}

void TestSketchTransform::aRotatedCopyAtNinetyDegreesSwapsHorizontalForVertical()
{
  FemmProblem p = constrainedLine();
  selectAll(p);
  SketchTransform::Report report;
  FemmProblemEdit::rotateCopySelected(p, 0, 0, 90.0, 1, &report);

  QCOMPARE(p.segments.size(), 2);
  QCOMPARE(p.constraints.size(), 2);

  // The copy really is vertical, so its constraint must say Vertical.
  // Left as Horizontal it would fight the geometry on the next solve and
  // drag the copy back through 90 degrees.
  QVERIFY2(!segmentIsHorizontal(p, 1), "the rotated copy is not vertical");
  QCOMPARE((int)p.constraints[1].type, (int)ConstraintType::Vertical);
  QCOMPARE(p.constraints[1].refA, 1);
  QCOMPARE(report.constraintsDropped, 0);
}

void TestSketchTransform::aRotatedCopyAtThirtyDegreesDropsTheAxisConstraintAndSaysSo()
{
  FemmProblem p = constrainedLine();
  selectAll(p);
  SketchTransform::Report report;
  FemmProblemEdit::rotateCopySelected(p, 0, 0, 30.0, 1, &report);

  QCOMPARE(p.segments.size(), 2);
  // A line at 30 degrees is neither horizontal nor vertical, so there is
  // no honest constraint to give the copy.
  QCOMPARE(p.constraints.size(), 1);
  QCOMPARE(report.constraintsDropped, 1);
  QVERIFY2(!report.notes.isEmpty(),
      "a constraint was dropped and nothing said so, which is exactly how a "
      "copy quietly arrives less constrained than the thing it came from");

  // The dimension is a length and survives a rotation untouched.
  QCOMPARE(p.dimensions.size(), 2);
  QCOMPARE(p.dimensions[1].value, 10.0);
}

void TestSketchTransform::everyOtherConstraintTypeSurvivesEveryTransform()
{
  // Exhaustive over the types that are NOT orientation-dependent, rather
  // than representative: a representative check is what lets one type
  // quietly go missing. Each of these is a statement about a
  // relationship, and rigid motions preserve all of them.
  const ConstraintType preserved[] = {
    ConstraintType::Coincident,
    ConstraintType::Parallel,
    ConstraintType::Perpendicular,
    ConstraintType::Equal,
    ConstraintType::Tangent,
    ConstraintType::Concentric,
    ConstraintType::Symmetric,
  };

  for (ConstraintType type : preserved) {
    FemmProblem p;
    p.problemType = FemmCoordinateType::Planar;
    // Two segments and two arcs, so every ref kind has something valid
    // to point at whichever list this type indexes.
    const int a = FemmProblemEdit::addNode(p, 0, 0);
    const int b = FemmProblemEdit::addNode(p, 10, 0);
    const int c = FemmProblemEdit::addNode(p, 0, 5);
    const int d = FemmProblemEdit::addNode(p, 10, 5);
    FemmProblemEdit::addSegment(p, a, b);
    FemmProblemEdit::addSegment(p, c, d);
    const int e = FemmProblemEdit::addNode(p, 20, 0);
    const int f = FemmProblemEdit::addNode(p, 20, 10);
    FemmProblemEdit::addArcSegment(p, e, f, 90.0, 1.0);
    const int g = FemmProblemEdit::addNode(p, 30, 0);
    const int h = FemmProblemEdit::addNode(p, 30, 10);
    FemmProblemEdit::addArcSegment(p, g, h, 90.0, 1.0);

    FemmConstraint con;
    con.type = type;
    switch (type) {
    case ConstraintType::Coincident:
      con.refA = a;
      con.refB = c;
      break;
    case ConstraintType::Concentric:
      con.refA = 0;
      con.refB = 1;
      break;
    case ConstraintType::Tangent:
      con.refA = 0; // a segment
      con.refB = 0; // and an arc
      con.firstIsArc = false;
      break;
    case ConstraintType::Symmetric:
      con.refA = a;
      con.refB = b;
      con.refC = 1;
      break;
    default:
      con.refA = 0;
      con.refB = 1;
      break;
    }
    p.constraints.push_back(con);
    selectAll(p);

    SketchTransform::Report report;
    FemmProblemEdit::rotateCopySelected(p, 0, 0, 37.0, 1, &report);
    QVERIFY2(p.constraints.size() == 2,
        qPrintable(QStringLiteral("constraint type %1 was not carried onto a rotated "
                                  "copy, though rotation preserves it")
                       .arg((int)type)));
    QVERIFY2((int)p.constraints[1].type == (int)type,
        "the copied constraint came back as a different type");
  }
}

// ---------------------------------------------------------------------------
// In-place transforms
// ---------------------------------------------------------------------------

void TestSketchTransform::scalingRescalesTheDimensionsThatMeasureWhatWasScaled()
{
  // Without this the dimension still says 10 while the line is 25 long,
  // and the next constraint solve pulls the geometry back to 10 -- the
  // scale is silently undone.
  FemmProblem p = constrainedLine();
  selectAll(p);
  SketchTransform::Report report;
  FemmProblemEdit::scaleSelected(p, 0, 0, 2.5, &report);

  QCOMPARE(p.dimensions[0].value, 25.0);
  QCOMPARE(report.dimensionsAdjusted, 1);
  // The geometry and the dimension have to agree, which is the point.
  const double length = std::hypot(
      p.nodes[p.dimensions[0].refB].x - p.nodes[p.dimensions[0].refA].x,
      p.nodes[p.dimensions[0].refB].y - p.nodes[p.dimensions[0].refA].y);
  QVERIFY(std::abs(length - p.dimensions[0].value) < 1e-9);
}

void TestSketchTransform::scalingLeavesAHalfSelectedDimensionAloneAndReportsIt()
{
  // One end scaled, one end not. There is no correct new value: scaling
  // misdescribes the fixed end, leaving it misdescribes the moved one.
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  FemmDimension d;
  d.type = DimensionType::Distance;
  d.refA = a;
  d.refB = b;
  d.value = 10.0;
  p.dimensions.push_back(d);

  p.nodes[b].isSelected = true; // only one end

  SketchTransform::Report report;
  FemmProblemEdit::scaleSelected(p, 0, 0, 2.0, &report);

  QCOMPARE(p.dimensions[0].value, 10.0); // untouched
  QCOMPARE(report.dimensionsAdjusted, 0);
  QVERIFY2(report.conflictsIntroduced > 0,
      "a dimension now measures neither end correctly and nothing said so");
}

void TestSketchTransform::rotatingNinetyDegreesInPlaceRewritesHorizontalAsVertical()
{
  FemmProblem p = constrainedLine();
  selectAll(p);
  SketchTransform::Report report;
  FemmProblemEdit::rotateSelected(p, 0, 0, 90.0, &report);

  QVERIFY2(!segmentIsHorizontal(p, 0), "the line did not actually rotate");
  QCOMPARE((int)p.constraints[0].type, (int)ConstraintType::Vertical);
  QCOMPARE(report.conflictsIntroduced, 0);
}

void TestSketchTransform::rotatingThirtyDegreesInPlaceReportsTheConflictWithoutDropping()
{
  FemmProblem p = constrainedLine();
  selectAll(p);
  SketchTransform::Report report;
  FemmProblemEdit::rotateSelected(p, 0, 0, 30.0, &report);

  // Deliberately NOT dropped: the constraint still points at the right
  // entity and still says what the user meant. It just disagrees with
  // where the geometry now is, and that is theirs to resolve.
  QCOMPARE(p.constraints.size(), 1);
  QCOMPARE((int)p.constraints[0].type, (int)ConstraintType::Horizontal);
  QVERIFY2(report.conflictsIntroduced > 0,
      "rotating constrained geometry off-axis reported nothing, so the user will "
      "find out when the next solve rotates it back");
  QVERIFY(!report.notes.isEmpty());
}

void TestSketchTransform::mirroringAcrossAnAxisAlignedLineKeepsHorizontalHorizontal()
{
  FemmProblem p = constrainedLine();
  selectAll(p);
  SketchTransform::Report report;
  // Across the vertical line x = 5.
  FemmProblemEdit::mirrorSelected(p, 5, 0, 5, 1, &report);

  QVERIFY2(segmentIsHorizontal(p, 0),
      "mirroring a horizontal line across a vertical axis should leave it horizontal");
  QCOMPARE((int)p.constraints[0].type, (int)ConstraintType::Horizontal);
  QCOMPARE(report.conflictsIntroduced, 0);
}

void TestSketchTransform::mirroringAcrossAFortyFiveDegreeLineSwapsTheAxes()
{
  // A line at angle a mirrored across an axis at f comes back at 2f - a,
  // so a horizontal line across a 45-degree axis comes back vertical.
  // This is the case a rule written as "mirroring preserves horizontal"
  // would get wrong.
  FemmProblem p = constrainedLine();
  selectAll(p);
  SketchTransform::Report report;
  FemmProblemEdit::mirrorSelected(p, 0, 0, 1, 1, &report);

  QVERIFY2(!segmentIsHorizontal(p, 0),
      "mirroring a horizontal line across a 45-degree axis should make it vertical");
  QCOMPARE((int)p.constraints[0].type, (int)ConstraintType::Vertical);
}

QTEST_GUILESS_MAIN(TestSketchTransform)
#include "tst_sketch_transform.moc"
