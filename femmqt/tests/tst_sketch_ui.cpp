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

#include "AppPreferences.h"
#include "ConstraintSolver.h"
#include "FemmProblem.h"
#include "FemmProblemEdit.h"
#include "GeometryScene.h"
#include "SnapEngine.h"

#include <QCoreApplication>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QGraphicsView>
#include <QGraphicsSimpleTextItem>
#include <QInputDialog>
#include <QKeyEvent>
#include <QTimer>
#include <QSignalSpy>
#include <QPen>
#include <QSet>

#include <algorithm>
#include <tuple>
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

  void theSnapIndicatorAppearsForAGeometrySnap();
  void theSnapIndicatorStaysHiddenForAGridSnap();
  void changingToolModeDropsAStaleSnapIndicator();
  void aDisabledSnapTypeProducesNoIndicator();
  void snapPreferencesRoundTripThroughFemmCfg();
  void anOutOfRangeSnapMaskFallsBackToTheDefault();

  void trimmingFromTheCanvasRemovesThePickedPiece();
  void trimClicksPickTheLineNotTheNodeMarkerOnTopOfIt();
  void aRefusedTrimCostsNoUndoStep();

  void constructionGeometryIsDrawnUnmistakablyDifferently();

  // #96: with the default snaps on, the snap indicator sat on top of the
  // very node it marked and every hit test found it instead.
  void aSegmentCanBeDrawnBetweenNodesWithDefaultSnaps();
  void aClickTheEndpointSnapClaimsPicksThatNode();
  void aDoubleClickUnderTheSnapIndicatorOpensTheEntityBeneathIt();
  void hoveringInSelectModeShowsNoSnap();

  // #97: the widened hit shape never got asked, because the bounding
  // rect of an axis-aligned line is zero-thickness.
  void aClickJustOffAHorizontalLineHitsIt();
  void aClickJustOffAnArcsExtremeHitsIt();
  void theLineHitMarginFollowsTheZoom();
  void ofTwoLinesInsideTheToleranceTheNearerIsPicked();

  // #99
  void aBlockLabelOnANodeIsDrawnAndPickedAboveIt();
  void placingABlockLabelOnANodeSaysSo();

  // #111
  void aRectangleCanBeDrawnWithTwoClicks();
  void aCircleCanBeDrawnWithTwoClicks();
  void aDraggedRectangleStillWorks();
  void escapeCancelsAPendingCorner();
  void theDrawToolsSurviveARebuild();
  void theDragToolsSurviveARebuild();
  // #116
  void clickingADimensionsLabelSelectsIt();
  void deletingALineAndItsLabelRemovesOneDimension();
  // #117
  void theDistanceToolPlacesWhereYouClick();
  void theRadiusToolPlacesWhereYouClick();
  void aPlacedDimensionCanBeDragged();
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

// ---------------------------------------------------------------------------
// Object snapping, GUI half (issue #28)
// ---------------------------------------------------------------------------
//
// SnapEngine itself is covered exhaustively by tst_snap_engine.cpp. What
// is checked here is the part a user actually touches: that the canvas
// SAYS which snap it applied, and that the Preferences toggles survive a
// round trip through femm.cfg.
//
// Both are the silent-alias shape this file exists for. A snap moves the
// point the user clicked; if it does that without saying so, the user
// gets geometry they did not draw and no indication of why -- and a
// toggle that does not persist is indistinguishable from one that does
// nothing.

namespace {

// Two nodes joined by one segment. Deliberately minimal: with several
// entities in range the priority rules decide the answer, and this file
// is not the place that tests those (tst_snap_engine.cpp is).
FemmProblem oneSegment()
{
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  const int n0 = FemmProblemEdit::addNode(p, 0, 0);
  const int n1 = FemmProblemEdit::addNode(p, 20, 0);
  FemmProblemEdit::addSegment(p, n0, n1);
  return p;
}

// The indicator carries no Kind/Index payload (it is a readout, not
// geometry, and must never be hit-tested), so it is identified by its
// z-value -- nothing else in the scene uses 50.
const QGraphicsItem* snapIndicator(const GeometryScene& scene)
{
  const QGraphicsItem* found = nullptr;
  for (const QGraphicsItem* item : scene.items()) {
    if (qFuzzyCompare(item->zValue(), 50.0)) {
      // More than one would mean the scene is leaking indicators, which
      // would leave stale markers parked on old snap positions.
      if (found)
        return nullptr;
      found = item;
    }
  }
  return found;
}

// A view is not decoration here: snapPoint() converts its capture radius
// from PIXELS using the attached view's scale, and with no view at all
// the radius collapses to zero and nothing geometric can ever snap. An
// identity transform gives a 12-unit radius, which is what the distances
// below are chosen against.
struct SceneWithView {
  GeometryScene scene;
  QGraphicsView view;
  SceneWithView()
      : view(&scene)
  {
    view.resize(400, 400);
  }
};

} // namespace

void TestSketchUi::theSnapIndicatorAppearsForAGeometrySnap()
{
  FemmProblem p = oneSegment();
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.rebuild();

  // Just off the node at (0,0) -- well inside the 12-unit capture radius.
  const QPointF snapped = h.scene.snapPoint(QPointF(0.4, 0.3));
  QCOMPARE(h.scene.lastSnap().type, SnapEngine::SnapType::Endpoint);
  QCOMPARE(snapped.x(), 0.0);
  QCOMPARE(snapped.y(), 0.0);

  h.scene.updateSnapIndicator();
  const QGraphicsItem* ind = snapIndicator(h.scene);
  QVERIFY2(ind, "no snap indicator was created for an endpoint snap");
  QVERIFY2(ind->isVisible(), "the snap indicator exists but is hidden");
  // It must sit ON the snapped point, not the cursor -- marking the
  // cursor would be worse than no marker, since it would assert the
  // point did not move when it did.
  QCOMPARE(ind->pos().x(), 0.0);
  QCOMPARE(ind->pos().y(), 0.0);
}

void TestSketchUi::theSnapIndicatorStaysHiddenForAGridSnap()
{
  FemmProblem p = oneSegment();
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setSnapToGrid(true);
  h.scene.rebuild();

  // Far from every entity: nearest is the node at (20,0), ~128 away.
  const QPointF snapped = h.scene.snapPoint(QPointF(100.4, 100.4));
  QCOMPARE(h.scene.lastSnap().type, SnapEngine::SnapType::Grid);
  QCOMPARE(snapped.x(), 100.0);
  QCOMPARE(snapped.y(), 100.0);

  h.scene.updateSnapIndicator();
  const QGraphicsItem* ind = snapIndicator(h.scene);
  // Grid snap is the pre-existing default behaviour and applies
  // everywhere; a marker chasing the cursor across every grid square is
  // noise, and noise is what stops an indicator being read at all. The
  // status bar still names it.
  QVERIFY2(!ind || !ind->isVisible(),
      "the grid snap put a marker on the canvas -- it would then follow "
      "the cursor everywhere and stop meaning anything");
}

void TestSketchUi::changingToolModeDropsAStaleSnapIndicator()
{
  FemmProblem p = oneSegment();
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.rebuild();

  h.scene.snapPoint(QPointF(0.4, 0.3));
  h.scene.updateSnapIndicator();
  QVERIFY(snapIndicator(h.scene) && snapIndicator(h.scene)->isVisible());

  // Switching tools ends the hover that produced it. A marker left
  // parked there claims a snap the next click will not necessarily get.
  h.scene.setToolMode(GeometryToolMode::AddNode);
  const QGraphicsItem* ind = snapIndicator(h.scene);
  QVERIFY2(!ind || !ind->isVisible(),
      "the snap indicator survived a tool change and is now asserting a "
      "snap that is no longer live");
}

void TestSketchUi::aDisabledSnapTypeProducesNoIndicator()
{
  FemmProblem p = oneSegment();
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.rebuild();
  // Everything off. This is what the Preferences checkboxes ultimately
  // do, and what Alt does for one point.
  h.scene.setSnapFlags(SnapEngine::SnapNone);

  const QPointF snapped = h.scene.snapPoint(QPointF(0.4, 0.3));
  QCOMPARE(h.scene.lastSnap().type, SnapEngine::SnapType::None);
  QCOMPARE(snapped.x(), 0.4);
  QCOMPARE(snapped.y(), 0.3);

  h.scene.updateSnapIndicator();
  const QGraphicsItem* ind = snapIndicator(h.scene);
  QVERIFY2(!ind || !ind->isVisible(),
      "snapping is disabled but the canvas still shows a snap marker");
}

void TestSketchUi::snapPreferencesRoundTripThroughFemmCfg()
{
  // femm.cfg is shared with the classic GUI, so this also checks the
  // thing #19 had to fix there: writing our own keys must not discard
  // anyone else's lines.
  const QString cfg = QCoreApplication::applicationDirPath() + "/femm.cfg";
  const QString backup = cfg + ".snaptest.bak";
  QFile::remove(backup);
  const bool hadOne = QFile::exists(cfg);
  if (hadOne)
    QVERIFY(QFile::copy(cfg, backup));

  {
    QFile seed(cfg);
    QVERIFY(seed.open(QIODevice::WriteOnly | QIODevice::Text));
    seed.write("<SomeClassicKeyWeDoNotKnow> = 7\n");
  }

  AppPreferences prefs = AppPreferences::load();
  const unsigned mask = SnapEngine::SnapEndpoint | SnapEngine::SnapCentre;
  prefs.snapFlags = mask;
  QVERIFY(prefs.save());

  const AppPreferences reloaded = AppPreferences::load();
  QCOMPARE(reloaded.snapFlags, mask);

  QFile check(cfg);
  QVERIFY(check.open(QIODevice::ReadOnly | QIODevice::Text));
  const QString text = QString::fromUtf8(check.readAll());
  check.close();
  QVERIFY2(text.contains("<SomeClassicKeyWeDoNotKnow>"),
      "saving the Qt-only snap setting dropped a key belonging to the "
      "classic GUI -- femm.cfg is shared between the two programs");

  QFile::remove(cfg);
  if (hadOne) {
    QVERIFY(QFile::copy(backup, cfg));
    QFile::remove(backup);
  }
}

void TestSketchUi::anOutOfRangeSnapMaskFallsBackToTheDefault()
{
  // A hand-edited or future-version value must not be able to switch
  // snapping off entirely or set bits that do not exist. Silently
  // honouring garbage here would look exactly like the feature being
  // broken.
  const QString cfg = QCoreApplication::applicationDirPath() + "/femm.cfg";
  const QString backup = cfg + ".snaptest2.bak";
  QFile::remove(backup);
  const bool hadOne = QFile::exists(cfg);
  if (hadOne)
    QVERIFY(QFile::copy(cfg, backup));

  {
    QFile seed(cfg);
    QVERIFY(seed.open(QIODevice::WriteOnly | QIODevice::Text));
    seed.write("<QtSnapFlags> = 999999\n");
  }
  QCOMPARE(AppPreferences::load().snapFlags, (unsigned)SnapEngine::SnapDefault);

  {
    QFile seed(cfg);
    QVERIFY(seed.open(QIODevice::WriteOnly | QIODevice::Text));
    seed.write("<QtSnapFlags> = not-a-number\n");
  }
  QCOMPARE(AppPreferences::load().snapFlags, (unsigned)SnapEngine::SnapDefault);

  QFile::remove(cfg);
  if (hadOne) {
    QVERIFY(QFile::copy(backup, cfg));
    QFile::remove(backup);
  }
}

// ---------------------------------------------------------------------------
// Trim from the canvas (issue #29)
// ---------------------------------------------------------------------------
//
// tst_trim_extend.cpp covers the geometry. What it cannot cover is the
// step in front of it: turning a click into an entity. That step has a
// specific trap, and it is this file's trap -- node and block-label
// markers sit ABOVE segments in z order on purpose, so that they stay
// clickable. Taking the topmost hit unfiltered therefore picks a node
// every time the user clicks near one, and near a junction is exactly
// where trimming happens. The failure is silent: the click appears to do
// nothing.

namespace {

// A horizontal line crossed by two verticals -- the classic trim
// picture, matching tst_trim_extend.cpp's own fixture.
FemmProblem crossedLine()
{
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  FemmProblemEdit::addSegment(p, a, b);
  const int c = FemmProblemEdit::addNode(p, 3, -2);
  const int d = FemmProblemEdit::addNode(p, 3, 2);
  FemmProblemEdit::addSegment(p, c, d);
  const int e = FemmProblemEdit::addNode(p, 7, -2);
  const int f = FemmProblemEdit::addNode(p, 7, 2);
  FemmProblemEdit::addSegment(p, e, f);
  return p;
}

void clickAt(GeometryScene& scene, QPointF scenePos)
{
  QGraphicsSceneMouseEvent ev(QEvent::GraphicsSceneMousePress);
  ev.setScenePos(scenePos);
  ev.setButton(Qt::LeftButton);
  ev.setButtons(Qt::LeftButton);
  QApplication::sendEvent(&scene, &ev);
}

} // namespace

void TestSketchUi::trimmingFromTheCanvasRemovesThePickedPiece()
{
  FemmProblem p = crossedLine();
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.rebuild();
  h.scene.setToolMode(GeometryToolMode::Trim);

  clickAt(h.scene, QPointF(5, 0)); // the middle piece

  // Two verticals plus the two surviving pieces of the horizontal.
  QCOMPARE(p.segments.size(), 4);
  for (const FemmSegment& s : p.segments) {
    if (p.nodes[s.n0].y == 0 && p.nodes[s.n1].y == 0) {
      const double lo = std::min(p.nodes[s.n0].x, p.nodes[s.n1].x);
      const double hi = std::max(p.nodes[s.n0].x, p.nodes[s.n1].x);
      QVERIFY2(!(lo < 4.9 && hi > 5.1), "the picked piece is still there");
    }
  }
}

void TestSketchUi::trimClicksPickTheLineNotTheNodeMarkerOnTopOfIt()
{
  // Right next to the crossing at x=3, where the node marker's
  // fixed-pixel hit area covers the line. Unfiltered, items() returns
  // that marker first -- it has the higher z-value precisely so it stays
  // clickable -- and the trim would quietly do nothing at all.
  FemmProblem p = crossedLine();
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.rebuild();
  h.scene.setToolMode(GeometryToolMode::Trim);

  const int segmentsBefore = p.segments.size();
  clickAt(h.scene, QPointF(3.2, 0));

  QVERIFY2(p.segments.size() != segmentsBefore,
      "the click landed on a node marker instead of the line under it, so "
      "the trim did nothing -- and said nothing");
}

void TestSketchUi::aRefusedTrimCostsNoUndoStep()
{
  // aboutToEdit is what MainWindow snapshots for Undo. A click that
  // changes nothing must not emit it, or one Ctrl+Z after a missed click
  // would undo the edit BEFORE it -- the user's last real change -- and
  // that is a data-loss bug, not a cosmetic one.
  FemmProblem p = crossedLine();
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.rebuild();
  h.scene.setToolMode(GeometryToolMode::Trim);

  QSignalSpy edits(&h.scene, &GeometryScene::aboutToEdit);
  QSignalSpy messages(&h.scene, &GeometryScene::toolMessage);

  clickAt(h.scene, QPointF(50, 50)); // empty space
  QCOMPARE(edits.count(), 0);
  QVERIFY2(messages.count() > 0,
      "a click that did nothing also said nothing, which is "
      "indistinguishable from a broken tool");

  clickAt(h.scene, QPointF(5, 0)); // a real trim
  QCOMPARE(edits.count(), 1);
}

void TestSketchUi::constructionGeometryIsDrawnUnmistakablyDifferently()
{
  // Issue #31. Construction geometry is not meshed and not solved, so
  // telling it apart from real geometry on the canvas is not decoration
  // -- it is the difference between a line that is part of the model and
  // one that is not. Two signals, because either alone is ambiguous:
  // dashing on its own reads as a hidden edge (femmqt already has those,
  // FemmSegment::hidden) and dimming on its own reads as deselected or
  // out of group.
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  FemmProblemEdit::addSegment(p, a, b); // real
  const int c = FemmProblemEdit::addNode(p, 0, 5);
  const int d = FemmProblemEdit::addNode(p, 10, 5);
  const int con = FemmProblemEdit::addSegment(p, c, d);
  p.segments[con].isConstruction = true;

  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.rebuild();

  QPen realPen, constructionPen;
  bool sawReal = false, sawConstruction = false;
  for (QGraphicsItem* item : h.scene.items()) {
    const KindIndex ki = readKindIndex(item);
    if (!ki.present || ki.kind != (int)FemmItemKind::Segment)
      continue;
    auto* line = dynamic_cast<QGraphicsLineItem*>(item);
    QVERIFY(line);
    if (ki.index == con) {
      constructionPen = line->pen();
      sawConstruction = true;
    } else {
      realPen = line->pen();
      sawReal = true;
    }
  }
  QVERIFY2(sawReal && sawConstruction, "the scene did not draw both segments");

  QVERIFY2(constructionPen.style() != realPen.style(),
      "the construction line is drawn with the same stroke as real geometry, so "
      "nothing on screen says it will not be meshed or solved");
  QVERIFY2(constructionPen.color().alphaF() < realPen.color().alphaF(),
      "the construction line is drawn at the same weight as real geometry");
}

namespace {

FemmProblem twoLooseNodes()
{
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  FemmProblemEdit::addNode(p, 0, 0);
  FemmProblemEdit::addNode(p, 40, 0);
  return p;
}

void sendMouse(GeometryScene& scene, QEvent::Type type, QPointF scenePos,
    Qt::MouseButtons buttons = Qt::LeftButton)
{
  QGraphicsSceneMouseEvent ev(type);
  ev.setScenePos(scenePos);
  ev.setButton(type == QEvent::GraphicsSceneMouseMove ? Qt::NoButton : Qt::LeftButton);
  ev.setButtons(buttons);
  QApplication::sendEvent(&scene, &ev);
}

// What a real user does: the pointer arrives first, the snap engine
// puts its marker on the node, and THEN the button goes down.
void hoverThenClick(GeometryScene& scene, QPointF at)
{
  sendMouse(scene, QEvent::GraphicsSceneMouseMove, at, Qt::NoButton);
  sendMouse(scene, QEvent::GraphicsSceneMousePress, at);
  sendMouse(scene, QEvent::GraphicsSceneMouseRelease, at, Qt::NoButton);
}

} // namespace

void TestSketchUi::aSegmentCanBeDrawnBetweenNodesWithDefaultSnaps()
{
  FemmProblem p = twoLooseNodes();
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setSnapFlags(SnapEngine::SnapDefault);
  h.scene.setToolMode(GeometryToolMode::AddSegment);

  hoverThenClick(h.scene, QPointF(0.3, 0.2));
  QVERIFY2(snapIndicator(h.scene) && snapIndicator(h.scene)->isVisible(),
      "precondition: the endpoint snap marker is up over the first node");
  hoverThenClick(h.scene, QPointF(40.3, 0.2));

  QVERIFY2(p.segments.size() == 1,
      "clicking node A then node B with the default snaps drew nothing -- "
      "the snap marker on top of each node took the click");
  QCOMPARE(p.segments[0].n0, 0);
  QCOMPARE(p.segments[0].n1, 1);
}

void TestSketchUi::aClickTheEndpointSnapClaimsPicksThatNode()
{
  // 8 units off each node: outside its few-pixel marker, inside the
  // 12-unit snap radius. The status bar says "Snap: Endpoint" there, so
  // the click must mean that node.
  FemmProblem p = twoLooseNodes();
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setSnapFlags(SnapEngine::SnapDefault);
  h.scene.setToolMode(GeometryToolMode::AddSegment);

  hoverThenClick(h.scene, QPointF(0, 8));
  QCOMPARE(h.scene.lastSnap().type, SnapEngine::SnapType::Endpoint);
  hoverThenClick(h.scene, QPointF(40, 8));
  QCOMPARE(p.segments.size(), 1);
}

void TestSketchUi::aDoubleClickUnderTheSnapIndicatorOpensTheEntityBeneathIt()
{
  // The review's case: double-clicking a segment opened Node Properties
  // for node 0, because the marker's missing kind read as 0 = Node and
  // its missing index as 0.
  // Long enough that its midpoint is out of both endpoints' snap range,
  // so the marker really is a Midpoint marker sitting on the line.
  FemmProblem p;
  FemmProblemEdit::addNode(p, 0, 0);
  FemmProblemEdit::addNode(p, 100, 0);
  FemmProblemEdit::addSegment(p, 0, 1);
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setSnapFlags(SnapEngine::SnapDefault);
  h.scene.setToolMode(GeometryToolMode::Select);
  // Park the marker on the segment's midpoint, where the pointer is.
  h.scene.snapPoint(QPointF(50, 0.2));
  QCOMPARE(h.scene.lastSnap().type, SnapEngine::SnapType::Midpoint);
  h.scene.updateSnapIndicator();
  QVERIFY(snapIndicator(h.scene) && snapIndicator(h.scene)->isVisible());

  QSignalSpy spy(&h.scene, &GeometryScene::entityDoubleClicked);
  sendMouse(h.scene, QEvent::GraphicsSceneMouseDoubleClick, QPointF(50, 0));
  QCOMPARE(spy.size(), 1);
  QVERIFY2(spy[0][0].value<FemmItemKind>() == FemmItemKind::Segment,
      qPrintable(QStringLiteral("double-click opened kind %1, index %2, not the segment")
                     .arg(static_cast<int>(spy[0][0].value<FemmItemKind>()))
                     .arg(spy[0][1].toInt())));
  QCOMPARE(spy[0][1].toInt(), 0);
}

void TestSketchUi::hoveringInSelectModeShowsNoSnap()
{
  FemmProblem p = oneSegment();
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setSnapFlags(SnapEngine::SnapDefault);

  h.scene.setToolMode(GeometryToolMode::Select);
  QSignalSpy positions(&h.scene, &GeometryScene::mousePositionChanged);
  sendMouse(h.scene, QEvent::GraphicsSceneMouseMove, QPointF(0.4, 0.3), Qt::NoButton);
  const QGraphicsItem* ind = snapIndicator(h.scene);
  QVERIFY2(!ind || !ind->isVisible(), "Select-mode hover put up a snap marker");
  QCOMPARE(h.scene.lastSnap().type, SnapEngine::SnapType::None);
  // and reports where the pointer really is, not where a snap would go
  QCOMPARE(positions.last()[0].toPointF(), QPointF(0.4, 0.3));

  // A placement tool still gets it.
  h.scene.setToolMode(GeometryToolMode::AddNode);
  sendMouse(h.scene, QEvent::GraphicsSceneMouseMove, QPointF(0.4, 0.3), Qt::NoButton);
  QVERIFY(snapIndicator(h.scene) && snapIndicator(h.scene)->isVisible());
  QCOMPARE(h.scene.lastSnap().type, SnapEngine::SnapType::Endpoint);
}

namespace {

// Which entity a Select-mode double-click at `at` opens, as (kind, index),
// or (-1, -1) for none.
QPair<int, int> doubleClickTarget(GeometryScene& scene, QPointF at)
{
  QSignalSpy spy(&scene, &GeometryScene::entityDoubleClicked);
  sendMouse(scene, QEvent::GraphicsSceneMouseDoubleClick, at);
  if (spy.isEmpty())
    return { -1, -1 };
  return { static_cast<int>(spy[0][0].value<FemmItemKind>()), spy[0][1].toInt() };
}

} // namespace

void TestSketchUi::aClickJustOffAHorizontalLineHitsIt()
{
  FemmProblem p;
  FemmProblemEdit::addNode(p, 0, 0);
  FemmProblemEdit::addNode(p, 100, 0);
  FemmProblemEdit::addSegment(p, 0, 1);
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setToolMode(GeometryToolMode::Select);

  // Identity view: 1 unit = 1 pixel. 2 px off the line, inside the
  // 4 px half-tolerance either side.
  const auto above = doubleClickTarget(h.scene, QPointF(50, 2));
  QVERIFY2(above.first == static_cast<int>(FemmItemKind::Segment),
      "a click 2 px above a horizontal line found nothing");
  QCOMPARE(doubleClickTarget(h.scene, QPointF(50, -2)).first, static_cast<int>(FemmItemKind::Segment));

  // and the same for a vertical one
  FemmProblemEdit::addNode(p, 200, 0);
  FemmProblemEdit::addNode(p, 200, 100);
  FemmProblemEdit::addSegment(p, 2, 3);
  h.scene.rebuild();
  const auto beside = doubleClickTarget(h.scene, QPointF(202, 50));
  QCOMPARE(beside.first, static_cast<int>(FemmItemKind::Segment));
  QCOMPARE(beside.second, 1);

  // Well clear of it is still a miss: the margin is a tolerance, not a
  // land grab.
  QCOMPARE(doubleClickTarget(h.scene, QPointF(50, 20)).first, -1);
}

void TestSketchUi::aClickJustOffAnArcsExtremeHitsIt()
{
  // A half circle of radius 50 about the origin, counterclockwise from
  // (50,0) to (-50,0): its top point (0,50) is exactly on the edge of
  // its path's bounding rect.
  FemmProblem p;
  FemmProblemEdit::addNode(p, 50, 0);
  FemmProblemEdit::addNode(p, -50, 0);
  FemmProblemEdit::addArcSegment(p, 0, 1, 180.0, 1.0);
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setToolMode(GeometryToolMode::Select);

  QVERIFY2(doubleClickTarget(h.scene, QPointF(0, 52)).first == static_cast<int>(FemmItemKind::Arc),
      "a click 2 px above the top of a circle found nothing");
  QCOMPARE(doubleClickTarget(h.scene, QPointF(0, 48)).first, static_cast<int>(FemmItemKind::Arc));
}

void TestSketchUi::theLineHitMarginFollowsTheZoom()
{
  FemmProblem p;
  FemmProblemEdit::addNode(p, 0, 0);
  FemmProblemEdit::addNode(p, 10, 0);
  FemmProblemEdit::addSegment(p, 0, 1);
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setToolMode(GeometryToolMode::Select);

  // Zoomed in 10x, 2 px is 0.2 units; at 1x that click would be in the
  // margin anyway, so test the converse too: 2 units off is 20 px now
  // and must miss.
  h.view.scale(10, 10);
  h.scene.refreshFixedPixelItemSizes(); // what GeometryView does on zoom
  QCOMPARE(doubleClickTarget(h.scene, QPointF(5, 0.2)).first, static_cast<int>(FemmItemKind::Segment));
  QCOMPARE(doubleClickTarget(h.scene, QPointF(5, 2.0)).first, -1);
}

void TestSketchUi::ofTwoLinesInsideTheToleranceTheNearerIsPicked()
{
  // Two horizontals 3 px apart. Both contain a click 1 px off the lower
  // one; the upper is added later, so it is on top. Topmost-wins would
  // pick the line the user was not pointing at.
  FemmProblem p;
  FemmProblemEdit::addNode(p, 0, 0);
  FemmProblemEdit::addNode(p, 100, 0);
  FemmProblemEdit::addSegment(p, 0, 1);
  FemmProblemEdit::addNode(p, 0, 3);
  FemmProblemEdit::addNode(p, 100, 3);
  FemmProblemEdit::addSegment(p, 2, 3);
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setToolMode(GeometryToolMode::Select);
  QCOMPARE(doubleClickTarget(h.scene, QPointF(50, 1)).second, 0);
  QCOMPARE(doubleClickTarget(h.scene, QPointF(50, 2)).second, 1);
}

void TestSketchUi::aBlockLabelOnANodeIsDrawnAndPickedAboveIt()
{
  // The review's case: grid snap put the label exactly on a corner node
  // and its "+" vanished under the node dot.
  FemmProblem p;
  FemmProblemEdit::addNode(p, 0, 0);
  FemmProblemEdit::addBlockLabel(p, 0, 0);
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setToolMode(GeometryToolMode::Select);
  QCOMPARE(doubleClickTarget(h.scene, QPointF(0, 0)).first, static_cast<int>(FemmItemKind::BlockLabel));
}

void TestSketchUi::placingABlockLabelOnANodeSaysSo()
{
  FemmProblem p;
  FemmProblemEdit::addNode(p, 0, 0);
  FemmProblemEdit::addNode(p, 100, 0);
  FemmProblemEdit::addSegment(p, 0, 1);
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setSnapFlags(SnapEngine::SnapNone);
  h.scene.setToolMode(GeometryToolMode::AddBlockLabel);
  QSignalSpy messages(&h.scene, &GeometryScene::toolMessage);

  hoverThenClick(h.scene, QPointF(0, 0)); // on the node
  QCOMPARE(p.blockLabels.size(), 1);
  QCOMPARE(messages.size(), 1);
  QVERIFY2(messages[0][0].toString().contains(QStringLiteral("node or a line")), qPrintable(messages[0][0].toString()));

  hoverThenClick(h.scene, QPointF(50, 0)); // on the line
  QCOMPARE(messages.size(), 2);

  hoverThenClick(h.scene, QPointF(50, 40)); // clear of everything: quiet
  QCOMPARE(p.blockLabels.size(), 3);
  QCOMPARE(messages.size(), 2);
}

namespace {

void click(GeometryScene& scene, QPointF at)
{
  sendMouse(scene, QEvent::GraphicsSceneMousePress, at);
  sendMouse(scene, QEvent::GraphicsSceneMouseRelease, at, Qt::NoButton);
}

bool hasNodeAt(const FemmProblem& p, double x, double y)
{
  for (const FemmNode& n : p.nodes)
    if (std::abs(n.x - x) < 1e-9 && std::abs(n.y - y) < 1e-9)
      return true;
  return false;
}

} // namespace

void TestSketchUi::aRectangleCanBeDrawnWithTwoClicks()
{
  FemmProblem p;
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setSnapFlags(SnapEngine::SnapNone);
  h.scene.setToolMode(GeometryToolMode::DrawRectangle);
  QSignalSpy hint(&h.scene, &GeometryScene::toolMessage);

  click(h.scene, QPointF(0, 0));
  QCOMPARE(p.nodes.size(), 0); // one corner is not a rectangle yet
  QVERIFY2(!hint.isEmpty(), "the first click said nothing about the second");
  sendMouse(h.scene, QEvent::GraphicsSceneMouseMove, QPointF(20, 10), Qt::NoButton);
  click(h.scene, QPointF(20, 10));

  QVERIFY2(p.nodes.size() == 4, "click, click drew nothing -- it used to need a drag");
  QCOMPARE(p.segments.size(), 4);
  QVERIFY(hasNodeAt(p, 0, 0) && hasNodeAt(p, 20, 0) && hasNodeAt(p, 20, 10) && hasNodeAt(p, 0, 10));

  // and the tool is ready for the next one, starting from scratch
  click(h.scene, QPointF(50, 50));
  QCOMPARE(p.nodes.size(), 4);
}

void TestSketchUi::aCircleCanBeDrawnWithTwoClicks()
{
  FemmProblem p;
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setSnapFlags(SnapEngine::SnapNone);
  h.scene.setToolMode(GeometryToolMode::DrawCircle);
  click(h.scene, QPointF(5, 5));
  sendMouse(h.scene, QEvent::GraphicsSceneMouseMove, QPointF(15, 5), Qt::NoButton);
  click(h.scene, QPointF(15, 5));
  QCOMPARE(p.nodes.size(), 2);
  QCOMPARE(p.arcSegments.size(), 2);
  QVERIFY(hasNodeAt(p, 15, 5) && hasNodeAt(p, -5, 5));
}

void TestSketchUi::aDraggedRectangleStillWorks()
{
  FemmProblem p;
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setSnapFlags(SnapEngine::SnapNone);
  h.scene.setToolMode(GeometryToolMode::DrawRectangle);
  sendMouse(h.scene, QEvent::GraphicsSceneMousePress, QPointF(0, 0));
  sendMouse(h.scene, QEvent::GraphicsSceneMouseMove, QPointF(8, 6));
  sendMouse(h.scene, QEvent::GraphicsSceneMouseRelease, QPointF(8, 6), Qt::NoButton);
  QCOMPARE(p.nodes.size(), 4);
  QVERIFY(hasNodeAt(p, 8, 6));
}

void TestSketchUi::escapeCancelsAPendingCorner()
{
  FemmProblem p;
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setSnapFlags(SnapEngine::SnapNone);
  h.scene.setToolMode(GeometryToolMode::DrawRectangle);
  click(h.scene, QPointF(0, 0));
  QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QApplication::sendEvent(&h.scene, &esc);
  click(h.scene, QPointF(20, 10)); // a fresh first corner, not the second
  QCOMPARE(p.nodes.size(), 0);
  click(h.scene, QPointF(30, 30));
  QCOMPARE(p.nodes.size(), 4);
  QVERIFY(hasNodeAt(p, 20, 10) && hasNodeAt(p, 30, 30));
}

void TestSketchUi::theDrawToolsSurviveARebuild()
{
  // rebuild() -- Undo, Open, a solve -- deleted the kept preview items
  // and left the pointers to them: the next rectangle wrote to freed
  // memory. Draw, rebuild, draw, for each of the three drag tools.
  FemmProblem p;
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setSnapFlags(SnapEngine::SnapNone);
  h.scene.setToolMode(GeometryToolMode::DrawRectangle);
  click(h.scene, QPointF(0, 0));
  click(h.scene, QPointF(10, 10));
  h.scene.rebuild();
  click(h.scene, QPointF(20, 20));
  click(h.scene, QPointF(30, 30));
  QCOMPARE(p.nodes.size(), 8);

  h.scene.setToolMode(GeometryToolMode::DrawCircle);
  click(h.scene, QPointF(50, 0));
  click(h.scene, QPointF(55, 0));
  h.scene.rebuild();
  click(h.scene, QPointF(70, 0));
  click(h.scene, QPointF(75, 0));
  QCOMPARE(p.arcSegments.size(), 4);

  // a rebuild between the two clicks forgets the first one cleanly
  h.scene.setToolMode(GeometryToolMode::DrawRectangle);
  click(h.scene, QPointF(100, 100));
  h.scene.rebuild();
  click(h.scene, QPointF(120, 120));
  QCOMPARE(p.nodes.size(), 12);
}

void TestSketchUi::theDragToolsSurviveARebuild()
{
  // The use-after-free alone, with drags only -- the pre-#111 gesture.
  FemmProblem p;
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setSnapFlags(SnapEngine::SnapNone);
  auto drag = [&](QPointF a, QPointF b) {
    sendMouse(h.scene, QEvent::GraphicsSceneMousePress, a);
    sendMouse(h.scene, QEvent::GraphicsSceneMouseMove, b);
    sendMouse(h.scene, QEvent::GraphicsSceneMouseRelease, b, Qt::NoButton);
  };
  h.scene.setToolMode(GeometryToolMode::DrawRectangle);
  drag(QPointF(0, 0), QPointF(10, 10));
  h.scene.rebuild(); // what Undo does
  drag(QPointF(20, 20), QPointF(30, 30));
  QCOMPARE(p.nodes.size(), 8);
  h.scene.setToolMode(GeometryToolMode::SelectCircle);
  drag(QPointF(0, 0), QPointF(5, 0));
  h.scene.rebuild();
  h.scene.setToolMode(GeometryToolMode::SelectCircle);
  drag(QPointF(0, 0), QPointF(5, 0)); // no crash is the assertion
}

namespace {

FemmProblem twoDimensions()
{
  FemmProblem p;
  FemmProblemEdit::addNode(p, 0, 0);
  FemmProblemEdit::addNode(p, 100, 0);
  FemmProblemEdit::addNode(p, 100, 60);
  FemmProblemEdit::addSegment(p, 0, 1);
  FemmProblemEdit::addSegment(p, 1, 2);
  for (auto [a, b, v] : { std::tuple<int, int, double> { 0, 1, 100 }, { 1, 2, 60 } }) {
    FemmDimension d;
    d.type = DimensionType::Distance;
    d.refA = a;
    d.refB = b;
    d.value = v;
    d.labelOffsetX = 0;
    d.labelOffsetY = -20;
    p.dimensions << d;
  }
  return p;
}

QGraphicsSimpleTextItem* dimensionLabel(GeometryScene& scene, int index)
{
  for (QGraphicsItem* item : scene.items())
    if (auto* t = qgraphicsitem_cast<QGraphicsSimpleTextItem*>(item))
      if (t->data(0).isValid() && t->data(0).toInt() == static_cast<int>(FemmItemKind::Dimension) && t->data(1).toInt() == index)
        return t;
  return nullptr;
}

} // namespace

void TestSketchUi::clickingADimensionsLabelSelectsIt()
{
  FemmProblem p = twoDimensions();
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setToolMode(GeometryToolMode::Select);
  QGraphicsSimpleTextItem* label = dimensionLabel(h.scene, 1);
  QVERIFY(label);
  const QRectF device = label->deviceTransform(h.view.viewportTransform()).mapRect(label->boundingRect());
  const QPointF at = h.view.mapToScene(device.center().toPoint());
  sendMouse(h.scene, QEvent::GraphicsSceneMousePress, at);
  sendMouse(h.scene, QEvent::GraphicsSceneMouseRelease, at, Qt::NoButton);

  FemmItemKind kind = FemmItemKind::Node;
  QVector<int> indices;
  QVERIFY2(h.scene.selectedEntities(kind, indices), "clicking the label selected nothing");
  QCOMPARE(kind, FemmItemKind::Dimension);
  QCOMPARE(indices, QVector<int> { 1 });
}

void TestSketchUi::deletingALineAndItsLabelRemovesOneDimension()
{
  FemmProblem p = twoDimensions();
  SceneWithView h;
  h.scene.setProblem(&p);
  // Both of dimension 0's items selected, as a rubber band would.
  for (QGraphicsItem* item : h.scene.items()) {
    if (item->data(0).isValid() && item->data(0).toInt() == static_cast<int>(FemmItemKind::Dimension) && item->data(1).toInt() == 0) {
      item->setFlag(QGraphicsItem::ItemIsSelectable, true);
      item->setSelected(true);
    }
  }
  h.scene.deleteSelectedItem();
  QVERIFY2(p.dimensions.size() == 1, qPrintable(QStringLiteral("%1 dimensions left, expected 1").arg(p.dimensions.size())));
  QCOMPARE(p.dimensions[0].value, 60.0); // and it is the OTHER one
}

namespace {

void acceptNextInputDialog()
{
  QTimer::singleShot(0, []() {
    if (auto* d = qobject_cast<QInputDialog*>(QApplication::activeModalWidget()))
      d->accept();
  });
}

} // namespace

void TestSketchUi::theDistanceToolPlacesWhereYouClick()
{
  FemmProblem p;
  FemmProblemEdit::addNode(p, 0, 0);
  FemmProblemEdit::addNode(p, 100, 0);
  FemmProblemEdit::addSegment(p, 0, 1);
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setSnapFlags(SnapEngine::SnapNone);
  h.scene.setToolMode(GeometryToolMode::AddDimensionDistance);
  click(h.scene, QPointF(0, 0));
  click(h.scene, QPointF(100, 0));
  QVERIFY2(p.dimensions.isEmpty(), "the dimension was dropped before a placement click");
  sendMouse(h.scene, QEvent::GraphicsSceneMouseMove, QPointF(50, -30), Qt::NoButton);
  acceptNextInputDialog();
  click(h.scene, QPointF(50, -30));
  QCOMPARE(p.dimensions.size(), 1);
  QCOMPARE(p.dimensions[0].type, DimensionType::Distance);
  QCOMPARE(p.dimensions[0].labelOffsetX, 0.0);
  QCOMPARE(p.dimensions[0].labelOffsetY, -30.0); // below, where clicked
}

void TestSketchUi::theRadiusToolPlacesWhereYouClick()
{
  FemmProblem p;
  FemmProblemEdit::addNode(p, 10, 0);
  FemmProblemEdit::addNode(p, -10, 0);
  FemmProblemEdit::addArcSegment(p, 0, 1, 180.0, 1.0);
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setSnapFlags(SnapEngine::SnapNone);
  h.scene.setToolMode(GeometryToolMode::AddDimensionRadius);
  click(h.scene, QPointF(0, 10)); // on the arc
  QVERIFY(p.dimensions.isEmpty());
  acceptNextInputDialog();
  click(h.scene, QPointF(-20, 20)); // up and to the left
  QCOMPARE(p.dimensions.size(), 1);
  QVERIFY(std::abs(p.dimensions[0].labelOffsetX + M_SQRT1_2) < 1e-9);
  QVERIFY(std::abs(p.dimensions[0].labelOffsetY - M_SQRT1_2) < 1e-9);
}

void TestSketchUi::aPlacedDimensionCanBeDragged()
{
  FemmProblem p = twoDimensions();
  SceneWithView h;
  h.scene.setProblem(&p);
  h.scene.setToolMode(GeometryToolMode::Select);
  QSignalSpy snap(&h.scene, &GeometryScene::aboutToEdit);
  QSignalSpy edited(&h.scene, &GeometryScene::problemEdited);
  QGraphicsSimpleTextItem* label = dimensionLabel(h.scene, 0);
  const QRectF device = label->deviceTransform(h.view.viewportTransform()).mapRect(label->boundingRect());
  const QPointF at = h.view.mapToScene(device.center().toPoint());

  // A plain click selects and costs no undo step.
  sendMouse(h.scene, QEvent::GraphicsSceneMousePress, at);
  sendMouse(h.scene, QEvent::GraphicsSceneMouseRelease, at, Qt::NoButton);
  QCOMPARE(snap.size(), 0);

  sendMouse(h.scene, QEvent::GraphicsSceneMousePress, at);
  sendMouse(h.scene, QEvent::GraphicsSceneMouseMove, QPointF(50, 25));
  sendMouse(h.scene, QEvent::GraphicsSceneMouseMove, QPointF(50, 40));
  sendMouse(h.scene, QEvent::GraphicsSceneMouseRelease, QPointF(50, 40), Qt::NoButton);
  QCOMPARE(snap.size(), 1);    // one undo step for the whole drag
  QCOMPARE(edited.size(), 1);
  QCOMPARE(p.dimensions[0].labelOffsetX, 0.0);
  QCOMPARE(p.dimensions[0].labelOffsetY, 40.0); // above the edge now
  QCOMPARE(p.dimensions[0].value, 100.0);       // a move, not an edit
}

QTEST_MAIN(TestSketchUi)
#include "tst_sketch_ui.moc"
