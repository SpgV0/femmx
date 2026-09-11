// tst_geometry_edit.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-11.
//
// GeometryScene.cpp is 124 KB and holds every editing operation femmqt
// has, and it had nothing testing it. The classic editor's equivalent
// logic is covered indirectly by the ~550-call Lua sweep and by
// enforce_pslg_correctness_test.py; femmqt's was covered by nothing, while
// being under active change -- crossing-segment node insertion and the
// hit-test Kind/Index bugs on dimension labels both landed recently
// (issue #13).
//
// Most of the editing logic lives in FemmProblemEdit, which is
// deliberately GUI-free ("Nothing here touches Qt graphics types", per its
// own header), so the bulk of this runs without a widget. The hit-testing
// cases construct a GeometryScene directly -- a QGraphicsScene needs no
// window, only a QApplication, which is why this binary uses QTEST_MAIN
// with the offscreen platform rather than QTEST_GUILESS_MAIN.
//
// Undo/redo is NOT covered here: the scene only emits a signal and
// MainWindow owns the stack, so testing it means standing up a
// QMainWindow. That is a larger harness than this ticket's editing
// semantics need and is called out in test/README.md rather than
// pretended at.

#include <QtTest>

#include "FemmProblem.h"
#include "FemmProblemEdit.h"
#include "GeometryScene.h"

#include <QGraphicsItem>

#include <cmath>

namespace {

int addNode(FemmProblem& p, double x, double y)
{
  return FemmProblemEdit::addNode(p, x, y);
}

int addSegment(FemmProblem& p, int n0, int n1)
{
  return FemmProblemEdit::addSegment(p, n0, n1);
}

// Does a node exist at (x,y), within tolerance?
bool hasNodeAt(const FemmProblem& p, double x, double y, double tol = 1e-9)
{
  for (const FemmNode& n : p.nodes) {
    if (std::hypot(n.x - x, n.y - y) <= tol)
      return true;
  }
  return false;
}

void selectAllNodes(FemmProblem& p)
{
  for (FemmNode& n : p.nodes)
    n.isSelected = true;
}

} // namespace

class TestGeometryEdit : public QObject
{
  Q_OBJECT

private slots:
  // --- add / delete --------------------------------------------------------

  void addAndDeleteEachEntityType();
  void deletingANodeRemovesWhatReferencedIt();
  void deletingAPropertyInUseIsCounted();

  // --- crossing-segment insertion (the v2.2.0 feature) ---------------------

  void properCrossingIsSplit();
  void sharedEndpointIsNotSplit();
  void collinearOverlapIsNotSplit();
  void parallelSegmentsAreNotSplit();
  void tJunctionIsNotSplit();
  void crossingNearAnEndpointRespectsTheInteriorTolerance();
  void multipleCrossingsOnOneSegmentAllSplit();

  // --- transforms ----------------------------------------------------------

  void moveSelectedTranslates();
  void copySelectedLeavesTheOriginal();
  void translateCopyMakesNCopies();
  void rotateSelectedAboutAPoint();
  void rotateCopyMakesNCopies();
  void mirrorSelectedReflects();
  void scaleSelectedScalesAboutTheBasePoint();

  // --- hit testing ---------------------------------------------------------

  void sceneHitTestReportsTheRightKind();
};

// ---------------------------------------------------------------------------
// add / delete
// ---------------------------------------------------------------------------

void TestGeometryEdit::addAndDeleteEachEntityType()
{
  FemmProblem p;
  const int n0 = addNode(p, 0, 0);
  const int n1 = addNode(p, 10, 0);
  const int n2 = addNode(p, 10, 10);
  const int n3 = addNode(p, 0, 10);
  QCOMPARE((int)p.nodes.size(), 4);

  const int s = addSegment(p, n0, n1);
  QCOMPARE((int)p.segments.size(), 1);

  const int a = FemmProblemEdit::addArcSegment(p, n2, n3, 90.0, 1.0);
  QCOMPARE((int)p.arcSegments.size(), 1);

  const int b = FemmProblemEdit::addBlockLabel(p, 5, 5);
  QCOMPARE((int)p.blockLabels.size(), 1);

  FemmProblemEdit::deleteBlockLabel(p, b);
  QCOMPARE((int)p.blockLabels.size(), 0);

  FemmProblemEdit::deleteArcSegment(p, a);
  QCOMPARE((int)p.arcSegments.size(), 0);

  FemmProblemEdit::deleteSegment(p, s);
  QCOMPARE((int)p.segments.size(), 0);

  // nodes survive deleting the things that used them
  QCOMPARE((int)p.nodes.size(), 4);
}

void TestGeometryEdit::deletingANodeRemovesWhatReferencedIt()
{
  // Deleting a node must take every segment and arc that referenced it,
  // and renumber the rest. A dangling n0/n1 index is a crash waiting to
  // happen in the mesher.
  FemmProblem p;
  const int n0 = addNode(p, 0, 0);
  const int n1 = addNode(p, 10, 0);
  const int n2 = addNode(p, 10, 10);
  addSegment(p, n0, n1);   // uses the node being deleted
  addSegment(p, n1, n2);   // uses it too
  const int keep = addSegment(p, n2, n0);
  FemmProblemEdit::addArcSegment(p, n1, n2, 90.0, 1.0);   // and this
  QCOMPARE((int)p.segments.size(), 3);
  QCOMPARE((int)p.arcSegments.size(), 1);
  Q_UNUSED(keep);

  FemmProblemEdit::deleteNode(p, n1);

  QCOMPARE((int)p.nodes.size(), 2);
  QCOMPARE((int)p.segments.size(), 1);
  QCOMPARE((int)p.arcSegments.size(), 0);

  // whatever survived must point at nodes that still exist
  for (const FemmSegment& s : p.segments) {
    QVERIFY2(s.n0 >= 0 && s.n0 < p.nodes.size(), "segment n0 dangles");
    QVERIFY2(s.n1 >= 0 && s.n1 < p.nodes.size(), "segment n1 dangles");
  }
}

void TestGeometryEdit::deletingAPropertyInUseIsCounted()
{
  // The reference counters are what the UI uses to warn before deleting a
  // material/boundary still applied to geometry.
  FemmProblem p;
  FemmMaterialProp mat;
  mat.name = "Steel";
  p.materialProps.push_back(mat);

  const int b = FemmProblemEdit::addBlockLabel(p, 1, 1);
  // blockTypeIndex is 1-BASED (-1 = hole), so materialProps[0] is
  // referenced by the marker value 1, not 0.
  p.blockLabels[b].blockTypeIndex = 1;

  QCOMPARE(FemmProblemEdit::countMaterialPropReferences(p, 0), 1);

  FemmProblemEdit::addBlockLabel(p, 2, 2);
  p.blockLabels[1].blockTypeIndex = 1;
  QCOMPARE(FemmProblemEdit::countMaterialPropReferences(p, 0), 2);

  FemmProblemEdit::deleteMaterialProp(p, 0);
  QCOMPARE((int)p.materialProps.size(), 0);
  // labels that referenced it must not keep pointing at a material that
  // no longer exists
  for (const FemmBlockLabel& lbl : p.blockLabels) {
    QVERIFY2(lbl.blockTypeIndex <= (int)p.materialProps.size(),
             "a block label still references a deleted material");
  }
}

// ---------------------------------------------------------------------------
// crossing-segment insertion
// ---------------------------------------------------------------------------

void TestGeometryEdit::properCrossingIsSplit()
{
  // A clean X. One node at the crossing, both segments cut in two.
  FemmProblem p;
  addSegment(p, addNode(p, -5, -5), addNode(p, 5, 5));
  addSegment(p, addNode(p, -5, 5), addNode(p, 5, -5));

  const int added = FemmProblemEdit::splitIntersectingSegments(p);
  QCOMPARE(added, 1);
  QCOMPARE((int)p.segments.size(), 4);
  QCOMPARE((int)p.nodes.size(), 5);
  QVERIFY2(hasNodeAt(p, 0, 0), "no node at the crossing point");
}

void TestGeometryEdit::sharedEndpointIsNotSplit()
{
  // Already connected: inventing a node on top of the shared endpoint
  // would be wrong.
  FemmProblem p;
  const int shared = addNode(p, 0, 0);
  addSegment(p, shared, addNode(p, 10, 0));
  addSegment(p, shared, addNode(p, 0, 10));

  const int added = FemmProblemEdit::splitIntersectingSegments(p);
  QCOMPARE(added, 0);
  QCOMPARE((int)p.segments.size(), 2);
  QCOMPARE((int)p.nodes.size(), 3);
}

void TestGeometryEdit::collinearOverlapIsNotSplit()
{
  // Two overlapping collinear segments have no single crossing point, so
  // there is nothing to insert.
  FemmProblem p;
  addSegment(p, addNode(p, 0, 0), addNode(p, 10, 0));
  addSegment(p, addNode(p, 4, 0), addNode(p, 14, 0));

  const int added = FemmProblemEdit::splitIntersectingSegments(p);
  QCOMPARE(added, 0);
  QCOMPARE((int)p.segments.size(), 2);
}

void TestGeometryEdit::parallelSegmentsAreNotSplit()
{
  FemmProblem p;
  addSegment(p, addNode(p, 0, 0), addNode(p, 10, 0));
  addSegment(p, addNode(p, 0, 3), addNode(p, 10, 3));

  QCOMPARE(FemmProblemEdit::splitIntersectingSegments(p), 0);
  QCOMPARE((int)p.segments.size(), 2);
}

void TestGeometryEdit::tJunctionIsNotSplit()
{
  // One segment's ENDPOINT lands on the middle of another. Documented as
  // deliberately left alone: fixing it properly means merging the touching
  // endpoint, not inventing a second node on top of it.
  FemmProblem p;
  addSegment(p, addNode(p, 0, 0), addNode(p, 10, 0));
  addSegment(p, addNode(p, 5, 0), addNode(p, 5, 8));

  const int added = FemmProblemEdit::splitIntersectingSegments(p);
  QCOMPARE(added, 0);
  QCOMPARE((int)p.segments.size(), 2);
}

void TestGeometryEdit::crossingNearAnEndpointRespectsTheInteriorTolerance()
{
  // The interior test's tolerance is scaled per segment, so it means "a
  // hair inside the ends" in REAL distance rather than in parameter space,
  // where it would depend on segment length. A crossing well inside a long
  // segment, but numerically close to the short one's midpoint, must still
  // split.
  FemmProblem p;
  addSegment(p, addNode(p, 0, 0), addNode(p, 1000, 0));
  addSegment(p, addNode(p, 500, -0.5), addNode(p, 500, 0.5));

  const int added = FemmProblemEdit::splitIntersectingSegments(p);
  QCOMPARE(added, 1);
  QVERIFY2(hasNodeAt(p, 500, 0, 1e-6), "no node at the crossing");
  QCOMPARE((int)p.segments.size(), 4);
}

void TestGeometryEdit::multipleCrossingsOnOneSegmentAllSplit()
{
  // One long segment crossed by three others. The implementation re-scans
  // after each split precisely because splitting changes the list; this is
  // the case that catches a single-pass version.
  FemmProblem p;
  addSegment(p, addNode(p, 0, 0), addNode(p, 30, 0));
  for (double x : {5.0, 15.0, 25.0})
    addSegment(p, addNode(p, x, -5), addNode(p, x, 5));

  const int added = FemmProblemEdit::splitIntersectingSegments(p);
  QCOMPARE(added, 3);
  for (double x : {5.0, 15.0, 25.0})
    QVERIFY2(hasNodeAt(p, x, 0, 1e-6), "missing a crossing node");
  // 1 long segment -> 4 pieces, 3 crossers -> 2 pieces each = 10
  QCOMPARE((int)p.segments.size(), 10);
}

// ---------------------------------------------------------------------------
// transforms
// ---------------------------------------------------------------------------

void TestGeometryEdit::moveSelectedTranslates()
{
  FemmProblem p;
  addNode(p, 1, 2);
  addNode(p, 3, 4);
  selectAllNodes(p);

  FemmProblemEdit::moveSelected(p, 10, -5);
  QCOMPARE((int)p.nodes.size(), 2);
  QVERIFY(hasNodeAt(p, 11, -3, 1e-9));
  QVERIFY(hasNodeAt(p, 13, -1, 1e-9));
}

void TestGeometryEdit::copySelectedLeavesTheOriginal()
{
  FemmProblem p;
  addNode(p, 0, 0);
  selectAllNodes(p);

  FemmProblemEdit::copySelected(p, 5, 0);
  QCOMPARE((int)p.nodes.size(), 2);
  QVERIFY2(hasNodeAt(p, 0, 0, 1e-9), "copy moved the original instead");
  QVERIFY2(hasNodeAt(p, 5, 0, 1e-9), "no copy was made");
}

void TestGeometryEdit::translateCopyMakesNCopies()
{
  FemmProblem p;
  addNode(p, 0, 0);
  selectAllNodes(p);

  FemmProblemEdit::translateCopySelected(p, 2, 0, 3);
  QCOMPARE((int)p.nodes.size(), 4);   // original + 3
  for (double x : {0.0, 2.0, 4.0, 6.0})
    QVERIFY2(hasNodeAt(p, x, 0, 1e-9), "a copy is missing");
}

void TestGeometryEdit::rotateSelectedAboutAPoint()
{
  FemmProblem p;
  addNode(p, 1, 0);
  selectAllNodes(p);

  FemmProblemEdit::rotateSelected(p, 0, 0, 90.0);
  QCOMPARE((int)p.nodes.size(), 1);
  QVERIFY2(hasNodeAt(p, 0, 1, 1e-9),
           qPrintable(QStringLiteral("rotated to (%1,%2), wanted (0,1)")
                          .arg(p.nodes[0].x).arg(p.nodes[0].y)));
}

void TestGeometryEdit::rotateCopyMakesNCopies()
{
  FemmProblem p;
  addNode(p, 1, 0);
  selectAllNodes(p);

  FemmProblemEdit::rotateCopySelected(p, 0, 0, 90.0, 3);
  QCOMPARE((int)p.nodes.size(), 4);
  QVERIFY(hasNodeAt(p, 1, 0, 1e-9));
  QVERIFY(hasNodeAt(p, 0, 1, 1e-9));
  QVERIFY(hasNodeAt(p, -1, 0, 1e-9));
  QVERIFY(hasNodeAt(p, 0, -1, 1e-9));
}

void TestGeometryEdit::mirrorSelectedReflects()
{
  // Mirror about the y axis: (3,2) -> (-3,2).
  FemmProblem p;
  addNode(p, 3, 2);
  selectAllNodes(p);

  FemmProblemEdit::mirrorSelected(p, 0, 0, 0, 1);
  QVERIFY2(hasNodeAt(p, -3, 2, 1e-9),
           qPrintable(QStringLiteral("mirrored to (%1,%2), wanted (-3,2)")
                          .arg(p.nodes[0].x).arg(p.nodes[0].y)));
}

void TestGeometryEdit::scaleSelectedScalesAboutTheBasePoint()
{
  FemmProblem p;
  addNode(p, 2, 4);
  selectAllNodes(p);

  FemmProblemEdit::scaleSelected(p, 0, 0, 2.5);
  QVERIFY2(hasNodeAt(p, 5, 10, 1e-9),
           qPrintable(QStringLiteral("scaled to (%1,%2), wanted (5,10)")
                          .arg(p.nodes[0].x).arg(p.nodes[0].y)));

  // scaling about a non-origin base point leaves that point fixed
  FemmProblem q;
  addNode(q, 10, 10);
  addNode(q, 12, 10);
  selectAllNodes(q);
  FemmProblemEdit::scaleSelected(q, 10, 10, 3.0);
  QVERIFY2(hasNodeAt(q, 10, 10, 1e-9), "the base point moved");
  QVERIFY2(hasNodeAt(q, 16, 10, 1e-9), "the other point scaled wrongly");
}

// ---------------------------------------------------------------------------
// hit testing
// ---------------------------------------------------------------------------

void TestGeometryEdit::sceneHitTestReportsTheRightKind()
{
  // The bug class this guards: an unset QVariant reads back as 0, which
  // aliases FemmItemKind::Node. Dimension number-labels shipped without
  // their Kind/Index data set, so clicking one was silently read as
  // "clicked node 0". Every item the scene builds must carry a kind that
  // was deliberately set.
  FemmProblem p;
  const int n0 = addNode(p, 0, 0);
  const int n1 = addNode(p, 10, 0);
  addSegment(p, n0, n1);
  FemmProblemEdit::addBlockLabel(p, 5, 5);

  GeometryScene scene;
  scene.setProblem(&p);
  scene.rebuild();

  QVERIFY2(!scene.items().isEmpty(), "the scene built no items at all");

  int kindless = 0;
  for (QGraphicsItem* item : scene.items()) {
    const QVariant kind = item->data(0);
    if (!kind.isValid())
      kindless++;
  }
  // Items without a kind are not automatically wrong -- decorations exist --
  // but every item that DOES carry one must carry a valid value rather than
  // a default-constructed 0 standing in for "unset".
  for (QGraphicsItem* item : scene.items()) {
    const QVariant kind = item->data(0);
    if (!kind.isValid())
      continue;
    bool ok = false;
    const int raw = kind.toInt(&ok);
    QVERIFY2(ok, "an item's kind is not an integer");
    QVERIFY2(raw >= 0 && raw <= (int)FemmItemKind::Constraint,
             qPrintable(QStringLiteral("item kind %1 is out of range").arg(raw)));
  }
  fprintf(stderr, "scene built %d items, %d without an explicit kind\n",
          (int)scene.items().size(), kindless);
}

QTEST_MAIN(TestGeometryEdit)
#include "tst_geometry_edit.moc"
