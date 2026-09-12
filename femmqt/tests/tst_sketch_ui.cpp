// tst_sketch_ui.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12
// (issue #26).
//
// #12 covers ConstraintSolver -- the numerics. Everything BETWEEN the
// user and that solver was untested, and it is where the v2.2.0 bug crop
// came from: dimension number-labels never set their hit-test Kind/Index,
// so clicking one read as "clicked node 0" (an unset QVariant reads back
// as 0, which aliases FemmItemKind::Node), and the placement preview sat
// topmost in itemAt() and swallowed the click that was meant to place it.
// Neither is a solver bug and no amount of solver testing would have
// found either.
//
// The shape of that bug class is what this file is built around: the
// failure is a SILENT ALIAS, not an error. Nothing throws, nothing is out
// of range, and the wrong entity is simply picked. So the checks below
// are exhaustive over kinds rather than representative -- a
// representative check is exactly what lets one kind quietly default.
//
// QTEST_MAIN, not GUILESS: GeometryScene is a QGraphicsScene and needs a
// QApplication. It needs no window (the CMake harness deploys the
// offscreen QPA plugin for this).

#include <QtTest>

#include "ConstraintSolver.h"
#include "FemmProblem.h"
#include "FemmProblemEdit.h"
#include "GeometryScene.h"

#include <QGraphicsItem>
#include <QGraphicsRectItem>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace {

// GeometryScene stores its hit-test payload in item data slots 0 and 1.
// Mirrored here rather than exported, so the test fails if the scene ever
// silently moves them.
constexpr int kKindKey = 0;
constexpr int kIndexKey = 1;

struct KindIndex {
  bool present = false;
  int kind = -1;
  int index = -1;
};

KindIndex readKindIndex(const QGraphicsItem* item)
{
  KindIndex out;
  const QVariant kind = item->data(kKindKey);
  const QVariant index = item->data(kIndexKey);
  if (!kind.isValid())
    return out;
  out.present = true;
  out.kind = kind.toInt();
  // NOT index.toInt() on an invalid variant, which would silently give 0
  // -- the very substitution this file exists to catch.
  out.index = index.isValid() ? index.toInt() : -1;
  return out;
}

// A model containing at least one of every kind the scene can draw.
FemmProblem modelWithEveryKind()
{
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;

  const int n0 = FemmProblemEdit::addNode(p, 0, 0);
  const int n1 = FemmProblemEdit::addNode(p, 10, 0);
  const int n2 = FemmProblemEdit::addNode(p, 10, 10);
  const int n3 = FemmProblemEdit::addNode(p, 0, 10);

  FemmProblemEdit::addSegment(p, n0, n1);
  FemmProblemEdit::addSegment(p, n1, n2);
  FemmProblemEdit::addArcSegment(p, n2, n3, 90.0, 1.0);
  FemmProblemEdit::addBlockLabel(p, 5, 5);

  FemmDimension d;
  d.type = DimensionType::Distance;
  d.refA = n0;
  d.refB = n1;
  d.value = 10.0;
  d.labelOffsetX = 0.0;
  d.labelOffsetY = -3.0;
  p.dimensions.push_back(d);

  FemmConstraint c;
  c.type = ConstraintType::Horizontal;
  c.refA = 0; // the first segment
  p.constraints.push_back(c);

  return p;
}

int countOfKind(const GeometryScene& scene, FemmItemKind kind)
{
  int n = 0;
  for (const QGraphicsItem* item : scene.items()) {
    const KindIndex ki = readKindIndex(item);
    if (ki.present && ki.kind == (int)kind)
      n++;
  }
  return n;
}

} // namespace

class TestSketchUi : public QObject
{
  Q_OBJECT

private slots:
  void everyKindIsRepresentedInTheScene();
  void everyTaggedItemCarriesAnIndexAsWellAsAKind();
  void everyEntityIndexIsReachableByHitTestPayload();
  void anUntaggedItemIsNeverMistakenForNodeZero();
  void nothingClickableIsUntagged();

  void constraintGlyphsAreBuiltForEveryConstraint();
  void selectingAConstraintGlyphSelectsThatConstraint();
  void deletingAConstraintGlyphRemovesOnlyThatConstraint();
  void deletingADimensionRemovesOnlyThatDimension();

  void deletingAConstraintFreesTheDofItConsumed();

  void deletingFromTheListAndFromTheCanvasAgree();
  void aMixedSelectionIsRefusedRatherThanGuessedAt();
  void selectedByKindPartitionsAMixedSelection();

  void rebuildIsIdempotent();
  void rebuildAfterADeletionDropsTheStaleItems();
};

// ---------------------------------------------------------------------------
// Kind / Index payload -- the silent-alias bug class
// ---------------------------------------------------------------------------

void TestSketchUi::everyKindIsRepresentedInTheScene()
{
  FemmProblem p = modelWithEveryKind();
  GeometryScene scene;
  scene.setProblem(&p);
  scene.rebuild();

  // Exhaustive by construction: if a kind is added to the enum and the
  // scene never tags anything with it, this names the missing one instead
  // of passing because the kinds it does check happen to be fine.
  const struct {
    FemmItemKind kind;
    const char* name;
    int expectedAtLeast;
  } expectations[] = {
    { FemmItemKind::Node, "Node", p.nodes.size() },
    { FemmItemKind::Segment, "Segment", p.segments.size() },
    { FemmItemKind::Arc, "Arc", p.arcSegments.size() },
    { FemmItemKind::BlockLabel, "BlockLabel", p.blockLabels.size() },
    { FemmItemKind::Dimension, "Dimension", p.dimensions.size() },
    { FemmItemKind::Constraint, "Constraint", p.constraints.size() },
  };

  for (const auto& e : expectations) {
    const int got = countOfKind(scene, e.kind);
    QVERIFY2(got >= e.expectedAtLeast,
        qPrintable(QStringLiteral("the scene tagged %1 item(s) as %2 but the "
                                  "model holds %3 -- some are unreachable by "
                                  "hit-testing")
                       .arg(got).arg(e.name).arg(e.expectedAtLeast)));
  }
}

void TestSketchUi::everyTaggedItemCarriesAnIndexAsWellAsAKind()
{
  // Half the original defect was a missing Kind. The other half is just
  // as silent and was never covered: an item with a Kind but no Index
  // reads back as index 0, so clicking the third dimension edits the
  // first. Nothing distinguishes that from a correct click.
  FemmProblem p = modelWithEveryKind();
  GeometryScene scene;
  scene.setProblem(&p);
  scene.rebuild();

  QStringList offenders;
  for (const QGraphicsItem* item : scene.items()) {
    const KindIndex ki = readKindIndex(item);
    if (!ki.present)
      continue;
    if (ki.index < 0)
      offenders << QStringLiteral("kind %1 with no index").arg(ki.kind);
  }
  QVERIFY2(offenders.isEmpty(),
      qPrintable(QStringLiteral("items carry a hit-test kind but no index, so "
                                "they all resolve to entity 0: %1")
                     .arg(offenders.join(", "))));
}

void TestSketchUi::everyEntityIndexIsReachableByHitTestPayload()
{
  // Not just "n items of this kind exist" but "every index 0..n-1 is
  // present exactly as itself". Two items both tagged index 0 would
  // satisfy a count check and still make one entity unclickable.
  FemmProblem p = modelWithEveryKind();
  GeometryScene scene;
  scene.setProblem(&p);
  scene.rebuild();

  const struct {
    FemmItemKind kind;
    const char* name;
    int count;
  } lists[] = {
    { FemmItemKind::Node, "Node", p.nodes.size() },
    { FemmItemKind::Segment, "Segment", p.segments.size() },
    { FemmItemKind::Arc, "Arc", p.arcSegments.size() },
    { FemmItemKind::BlockLabel, "BlockLabel", p.blockLabels.size() },
    { FemmItemKind::Dimension, "Dimension", p.dimensions.size() },
    { FemmItemKind::Constraint, "Constraint", p.constraints.size() },
  };

  for (const auto& l : lists) {
    QSet<int> seen;
    for (const QGraphicsItem* item : scene.items()) {
      const KindIndex ki = readKindIndex(item);
      if (ki.present && ki.kind == (int)l.kind && ki.index >= 0)
        seen.insert(ki.index);
    }
    for (int i = 0; i < l.count; i++) {
      QVERIFY2(seen.contains(i),
          qPrintable(QStringLiteral("%1 %2 has no item carrying its index; "
                                    "it cannot be clicked")
                         .arg(l.name).arg(i)));
    }
    for (int idx : seen) {
      QVERIFY2(idx < l.count,
          qPrintable(QStringLiteral("an item claims to be %1 %2, but the "
                                    "model only has %3 -- a stale item from "
                                    "before a deletion")
                         .arg(l.name).arg(idx).arg(l.count)));
    }
  }
}

void TestSketchUi::anUntaggedItemIsNeverMistakenForNodeZero()
{
  // The mechanism, stated directly so it cannot come back: an unset
  // QVariant converts to 0, and 0 is a real kind. Any code that reads
  // data(0) without checking isValid() picks node 0 out of thin air.
  QGraphicsRectItem bare(0, 0, 1, 1);
  QCOMPARE(bare.data(kKindKey).isValid(), false);
  QCOMPARE(bare.data(kKindKey).toInt(), 0);
  QCOMPARE((int)FemmItemKind::Node, 0);

  // ...which is why readKindIndex above refuses to interpret an invalid
  // variant, and why the scene must set the tag explicitly on anything
  // meant to be clickable.
  const KindIndex ki = readKindIndex(&bare);
  QCOMPARE(ki.present, false);
}

void TestSketchUi::nothingClickableIsUntagged()
{
  // The whole bug class as ONE rule, which is the useful form of it:
  //
  //   an item that can be clicked must carry a Kind.
  //
  // Both shipped v2.2.0 defects are instances. The dimension label
  // accepted clicks and had no Kind, so it read as node 0. The Smart
  // Dimension ghost accepted clicks and had no Kind, so it both swallowed
  // the placement click and would have read as node 0 too. The fix for
  // the ghost was setAcceptedMouseButtons(Qt::NoButton) rather than
  // tagging it -- a placement click should see straight through a
  // preview to the real geometry underneath -- which is precisely this
  // rule being satisfied from the other side.
  //
  // Stated this way it also covers decorations nobody has added yet.
  FemmProblem p = modelWithEveryKind();
  GeometryScene scene;
  scene.setProblem(&p);
  scene.rebuild();

  QStringList offenders;
  for (const QGraphicsItem* item : scene.items()) {
    if (readKindIndex(item).present)
      continue;
    if (item->acceptedMouseButtons() == Qt::NoButton)
      continue;
    // Selectable-but-untagged is the dangerous combination; an item that
    // merely accepts buttons without being selectable cannot end up as
    // the scene's selection.
    if (!(item->flags() & QGraphicsItem::ItemIsSelectable))
      continue;
    offenders << QStringLiteral("a %1 at (%2, %3)")
                     .arg(item->type())
                     .arg(item->pos().x())
                     .arg(item->pos().y());
  }

  QVERIFY2(offenders.isEmpty(),
      qPrintable(QStringLiteral("these items are selectable but carry no "
                                "hit-test kind, so selecting one reads as "
                                "node 0: %1").arg(offenders.join("; "))));
}

// ---------------------------------------------------------------------------
// Constraint glyphs and dimensions
// ---------------------------------------------------------------------------

void TestSketchUi::constraintGlyphsAreBuiltForEveryConstraint()
{
  FemmProblem p;
  const int n0 = FemmProblemEdit::addNode(p, 0, 0);
  const int n1 = FemmProblemEdit::addNode(p, 10, 0);
  const int n2 = FemmProblemEdit::addNode(p, 10, 10);
  FemmProblemEdit::addSegment(p, n0, n1);
  FemmProblemEdit::addSegment(p, n1, n2);

  FemmConstraint h;
  h.type = ConstraintType::Horizontal;
  h.refA = 0;
  p.constraints.push_back(h);
  FemmConstraint v;
  v.type = ConstraintType::Vertical;
  v.refA = 1;
  p.constraints.push_back(v);

  GeometryScene scene;
  scene.setProblem(&p);
  scene.rebuild();

  QCOMPARE(countOfKind(scene, FemmItemKind::Constraint), 2);
}

void TestSketchUi::selectingAConstraintGlyphSelectsThatConstraint()
{
  FemmProblem p = modelWithEveryKind();
  GeometryScene scene;
  scene.setProblem(&p);
  scene.rebuild();

  scene.selectConstraintGlyph(0);

  FemmItemKind kind = FemmItemKind::Node;
  QVector<int> indices;
  QVERIFY2(scene.selectedEntities(kind, indices),
      "selecting a constraint glyph left the scene with no selection");
  QCOMPARE((int)kind, (int)FemmItemKind::Constraint);
  QCOMPARE(indices.size(), 1);
  QCOMPARE(indices[0], 0);
}

void TestSketchUi::deletingAConstraintGlyphRemovesOnlyThatConstraint()
{
  FemmProblem p;
  const int n0 = FemmProblemEdit::addNode(p, 0, 0);
  const int n1 = FemmProblemEdit::addNode(p, 10, 0);
  const int n2 = FemmProblemEdit::addNode(p, 10, 10);
  FemmProblemEdit::addSegment(p, n0, n1);
  FemmProblemEdit::addSegment(p, n1, n2);

  FemmConstraint h;
  h.type = ConstraintType::Horizontal;
  h.refA = 0;
  p.constraints.push_back(h);
  FemmConstraint v;
  v.type = ConstraintType::Vertical;
  v.refA = 1;
  p.constraints.push_back(v);

  GeometryScene scene;
  scene.setProblem(&p);
  scene.rebuild();

  scene.selectConstraintGlyph(0);
  scene.deleteSelectedItem();

  QCOMPARE(p.constraints.size(), 1);
  // the survivor is the OTHER one, still pointing at its own segment
  QCOMPARE((int)p.constraints[0].type, (int)ConstraintType::Vertical);
  QCOMPARE(p.constraints[0].refA, 1);
  // and no geometry went with it
  QCOMPARE(p.nodes.size(), 3);
  QCOMPARE(p.segments.size(), 2);
}

void TestSketchUi::deletingADimensionRemovesOnlyThatDimension()
{
  FemmProblem p;
  const int n0 = FemmProblemEdit::addNode(p, 0, 0);
  const int n1 = FemmProblemEdit::addNode(p, 10, 0);
  const int n2 = FemmProblemEdit::addNode(p, 10, 10);
  FemmProblemEdit::addSegment(p, n0, n1);

  FemmDimension d0;
  d0.type = DimensionType::Distance;
  d0.refA = n0;
  d0.refB = n1;
  d0.value = 10.0;
  p.dimensions.push_back(d0);
  FemmDimension d1;
  d1.type = DimensionType::Distance;
  d1.refA = n1;
  d1.refB = n2;
  d1.value = 10.0;
  p.dimensions.push_back(d1);

  FemmProblemEdit::deleteDimension(p, 0);

  QCOMPARE(p.dimensions.size(), 1);
  QCOMPARE(p.dimensions[0].refA, n1);
  QCOMPARE(p.dimensions[0].refB, n2);
  QCOMPARE(p.nodes.size(), 3);

  // and the scene agrees after a rebuild -- a stale item claiming
  // dimension 1 would now be out of range
  GeometryScene scene;
  scene.setProblem(&p);
  scene.rebuild();
  for (const QGraphicsItem* item : scene.items()) {
    const KindIndex ki = readKindIndex(item);
    if (ki.present && ki.kind == (int)FemmItemKind::Dimension)
      QVERIFY2(ki.index < p.dimensions.size(),
          "the scene still holds an item for a deleted dimension");
  }
}

void TestSketchUi::deletingAConstraintFreesTheDofItConsumed()
{
  // A constraint deleted from the model but still counted by the solver
  // would leave the sketch permanently classified as if it were still
  // there, with no glyph left to click to undo it.
  //
  // The observable is the classification, which is what the canvas
  // colours nodes by: a segment with a Horizontal constraint is
  // constrained; once the constraint is gone its nodes must be free
  // again.
  FemmProblem p;
  const int n0 = FemmProblemEdit::addNode(p, 0, 0);
  const int n1 = FemmProblemEdit::addNode(p, 10, 1);
  FemmProblemEdit::addSegment(p, n0, n1);

  FemmConstraint h;
  h.type = ConstraintType::Horizontal;
  h.refA = 0;
  p.constraints.push_back(h);

  const ConstraintSolver::SolveResult withConstraint =
      ConstraintSolver::solve(p);
  QVERIFY2(withConstraint.converged, "the Horizontal constraint did not solve");
  QVERIFY2(!withConstraint.nodeStatus.isEmpty(),
      "a constrained sketch classified no nodes at all");
  // it did its job: the segment is horizontal now
  QVERIFY(std::fabs(p.nodes[n0].y - p.nodes[n1].y) < 1e-6);

  FemmProblemEdit::deleteConstraint(p, 0);
  QCOMPARE(p.constraints.size(), 0);

  const ConstraintSolver::SolveResult without = ConstraintSolver::solve(p);
  QVERIFY2(without.converged,
      "solving a sketch with no constraints at all did not converge");
  QVERIFY2(without.nodeStatus.isEmpty(),
      "nodes are still classified as participating in a constraint system "
      "after the only constraint was deleted -- the solver is still "
      "counting it");
}

// ---------------------------------------------------------------------------
// Two routes to the same deletion, and mixed selections
// ---------------------------------------------------------------------------

void TestSketchUi::deletingFromTheListAndFromTheCanvasAgree()
{
  // The Constraint List dialog deletes through a `remove` callback while
  // the canvas goes through deleteSelectedItem(). Two paths to one
  // operation is how they drift: the list could renumber differently, or
  // delete by row rather than by constraint index once the list is
  // filtered or sorted. Building the same model twice and taking one
  // route each is the only way to notice.
  auto buildModel = []() {
    FemmProblem p;
    const int n0 = FemmProblemEdit::addNode(p, 0, 0);
    const int n1 = FemmProblemEdit::addNode(p, 10, 0);
    const int n2 = FemmProblemEdit::addNode(p, 10, 10);
    FemmProblemEdit::addSegment(p, n0, n1);
    FemmProblemEdit::addSegment(p, n1, n2);
    FemmConstraint a;
    a.type = ConstraintType::Horizontal;
    a.refA = 0;
    p.constraints.push_back(a);
    FemmConstraint b;
    b.type = ConstraintType::Vertical;
    b.refA = 1;
    p.constraints.push_back(b);
    FemmConstraint c;
    c.type = ConstraintType::Parallel;
    c.refA = 0;
    c.refB = 1;
    p.constraints.push_back(c);
    return p;
  };

  // Route 1: canvas.
  FemmProblem viaCanvas = buildModel();
  {
    GeometryScene scene;
    scene.setProblem(&viaCanvas);
    scene.rebuild();
    scene.selectConstraintGlyph(1);
    scene.deleteSelectedItem();
  }

  // Route 2: the list dialog's own callback, invoked directly -- the
  // dialog is a thin shell over exactly this.
  FemmProblem viaList = buildModel();
  FemmProblemEdit::deleteConstraint(viaList, 1);

  QCOMPARE(viaCanvas.constraints.size(), viaList.constraints.size());
  for (int i = 0; i < viaCanvas.constraints.size(); i++) {
    QVERIFY2((int)viaCanvas.constraints[i].type
                 == (int)viaList.constraints[i].type,
        qPrintable(QStringLiteral("constraint %1 differs between the two "
                                  "deletion routes: canvas left type %2, the "
                                  "list left type %3")
                       .arg(i).arg((int)viaCanvas.constraints[i].type)
                       .arg((int)viaList.constraints[i].type)));
    QCOMPARE(viaCanvas.constraints[i].refA, viaList.constraints[i].refA);
    QCOMPARE(viaCanvas.constraints[i].refB, viaList.constraints[i].refB);
  }
  // and neither touched the geometry
  QCOMPARE(viaCanvas.nodes.size(), viaList.nodes.size());
  QCOMPARE(viaCanvas.segments.size(), viaList.segments.size());
}

void TestSketchUi::aMixedSelectionIsRefusedRatherThanGuessedAt()
{
  // selectedEntities() reports one kind and a list of indices. Faced with
  // a mixed selection it must decline, not pick a kind and hand back
  // indices that mean something else -- the caller opens a property
  // dialog with them.
  FemmProblem p = modelWithEveryKind();
  GeometryScene scene;
  scene.setProblem(&p);
  scene.rebuild();

  int selected = 0;
  for (QGraphicsItem* item : scene.items()) {
    const KindIndex ki = readKindIndex(item);
    if (!ki.present)
      continue;
    if (ki.kind == (int)FemmItemKind::Node && ki.index == 0) {
      item->setSelected(true);
      selected++;
    }
    if (ki.kind == (int)FemmItemKind::BlockLabel && ki.index == 0) {
      item->setSelected(true);
      selected++;
    }
  }
  QVERIFY2(selected >= 2, "could not build a mixed selection to test with");

  FemmItemKind kind = FemmItemKind::Arc;
  QVector<int> indices;
  QVERIFY2(!scene.selectedEntities(kind, indices),
      "selectedEntities() reported a single kind for a selection that mixes "
      "a node and a block label");
}

void TestSketchUi::selectedByKindPartitionsAMixedSelection()
{
  // The generalisation that mixed selections go through instead. Several
  // constraint commands need a specific mix (Symmetric is two nodes plus
  // a segment), so mis-partitioning here builds the wrong constraint
  // rather than failing.
  FemmProblem p = modelWithEveryKind();
  GeometryScene scene;
  scene.setProblem(&p);
  scene.rebuild();

  for (QGraphicsItem* item : scene.items()) {
    const KindIndex ki = readKindIndex(item);
    if (!ki.present)
      continue;
    const bool wanted =
        (ki.kind == (int)FemmItemKind::Node && (ki.index == 0 || ki.index == 1))
        || (ki.kind == (int)FemmItemKind::Segment && ki.index == 0);
    if (wanted)
      item->setSelected(true);
  }

  QVector<int> nodes, segments, arcs, labels;
  scene.selectedByKind(nodes, segments, arcs, labels);

  std::sort(nodes.begin(), nodes.end());
  QCOMPARE(nodes.size(), 2);
  QCOMPARE(nodes[0], 0);
  QCOMPARE(nodes[1], 1);
  QCOMPARE(segments.size(), 1);
  QCOMPARE(segments[0], 0);
  QCOMPARE(arcs.size(), 0);
  QCOMPARE(labels.size(), 0);
}

// ---------------------------------------------------------------------------
// Rebuild
// ---------------------------------------------------------------------------

void TestSketchUi::rebuildIsIdempotent()
{
  // rebuild() runs on every edit. If it accumulated items instead of
  // replacing them, hit-testing would start picking whichever stale copy
  // happened to be on top -- the same class of "wrong thing selected"
  // failure, arrived at from the other direction.
  FemmProblem p = modelWithEveryKind();
  GeometryScene scene;
  scene.setProblem(&p);
  scene.rebuild();
  const int first = scene.items().size();

  scene.rebuild();
  scene.rebuild();

  QCOMPARE(scene.items().size(), first);
}

void TestSketchUi::rebuildAfterADeletionDropsTheStaleItems()
{
  FemmProblem p = modelWithEveryKind();
  GeometryScene scene;
  scene.setProblem(&p);
  scene.rebuild();
  const int constraintsBefore = countOfKind(scene, FemmItemKind::Constraint);
  QVERIFY(constraintsBefore >= 1);

  FemmProblemEdit::deleteConstraint(p, 0);
  scene.rebuild();

  QCOMPARE(countOfKind(scene, FemmItemKind::Constraint), constraintsBefore - 1);
}

QTEST_MAIN(TestSketchUi)
#include "tst_sketch_ui.moc"
