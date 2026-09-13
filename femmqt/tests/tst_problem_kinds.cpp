// tst_problem_kinds.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #80).
//
// Four formats, one reader and one writer. The failure this file is
// written against is not a crash: it is a field that reads back as zero
// because its tag was misspelled, or a column that lands in the wrong
// member because the row shape differs between formats and the code
// assumed it did not. Both produce a file that loads without complaint
// and describes a different problem than the one that was saved.
//
// So every case here sets EVERY field of a record to a distinct non-zero
// value and compares them all after a round trip. A test that fills in
// two fields and checks two fields would pass with the other six silently
// dropped.
//
// The magnetics case additionally cross-checks the new shared driver
// against FemmFileIO, the reader that has always handled .fem: they must
// agree entity for entity. That is the guard on the refactor itself --
// magnetics is the one kind that already worked, so it is the one where
// a regression would be a real loss rather than a missing feature.

#include <QtTest>

#include "FemmFileIO.h"
#include "FemmProblem.h"
#include "FemmProblemEdit.h"
#include "ProblemFileIO.h"
#include "ProblemKind.h"
#include "MaterialLibraryIO.h"
#include "PropertyFields.h"
#include "SolveRunner.h"

#include <QDir>
#include <QFileInfo>
#include <QTemporaryDir>

namespace {

// Geometry common to every kind, with distinct values everywhere so a
// column landing in the wrong member shows up as a wrong number rather
// than as a coincidence.
void addGeometry(FemmProblem& p)
{
  const int a = FemmProblemEdit::addNode(p, 1.5, 2.5);
  const int b = FemmProblemEdit::addNode(p, 11.5, 2.5);
  const int c = FemmProblemEdit::addNode(p, 11.5, 12.5);
  p.nodes[a].pointPropIndex = 1;
  p.nodes[a].inGroup = 3;
  p.nodes[b].inGroup = 4;

  const int s = FemmProblemEdit::addSegment(p, a, b);
  p.segments[s].maxSideLength = 0.75;
  p.segments[s].boundaryMarker = 1;
  p.segments[s].hidden = true;
  p.segments[s].inGroup = 5;

  const int arc = FemmProblemEdit::addArcSegment(p, b, c, 42.5, 2.5);
  p.arcSegments[arc].boundaryMarker = 1;
  p.arcSegments[arc].inGroup = 6;
  p.arcSegments[arc].mySideLength = 1.25;

  const int lbl = FemmProblemEdit::addBlockLabel(p, 5.5, 6.5);
  p.blockLabels[lbl].blockTypeIndex = 1;
  p.blockLabels[lbl].inGroup = 7;
  p.blockLabels[lbl].maxArea = 3.5;

  // A hole is a block label with no material, and rides in the same list.
  const int hole = FemmProblemEdit::addBlockLabel(p, 8.5, 9.5);
  p.blockLabels[hole].blockTypeIndex = -1;
  p.blockLabels[hole].inGroup = 8;
}

void addCommonHeader(FemmProblem& p)
{
  p.precision = 1e-9;
  p.minAngle = 32.5;
  p.smartMesh = false;
  p.depth = 17.5;
  p.lengthUnits = FemmLengthUnits::Centimeters;
  p.problemType = FemmCoordinateType::Axisymmetric;
  p.coordsPolar = true;
  p.extZo = 1.5;
  p.extRo = 2.5;
  p.extRi = 3.5;
  p.comment = "a round trip";
}

QString pathFor(const QTemporaryDir& dir, FemmProblemKind kind)
{
  return dir.path() + "/model." + ProblemKind::extension(kind);
}

} // namespace

class TestProblemKinds : public QObject
{
  Q_OBJECT

  private slots:
  void theCommonHeaderAndGeometrySurviveEveryKind();
  void theCommonHeaderAndGeometrySurviveEveryKind_data();

  void magneticsPropertiesSurviveARoundTrip();
  void electrostaticsPropertiesSurviveARoundTrip();
  void heatFlowPropertiesSurviveARoundTrip();
  void currentFlowPropertiesSurviveARoundTrip();

  void theConductorIndexSurvivesOnNodesSegmentsAndArcs();
  void magneticsKeepsItsCircuitMagDirAndTurnsOnBlockLabels();

  void theNewDriverAndTheOldMagneticsReaderAgree();

  void openRefusesAnExtensionItDoesNotKnow();
  void saveRefusesAnExtensionBelongingToAnotherKind();

  void everyKindMapsToItsOwnExtensionSolverAndSolutionFile();
  void anExtensionNeverResolvesToTheWrongKind();

  void addingAPropertyGivesItAUniqueNameInEveryKind();
  void deletingAMaterialTurnsItsLabelsIntoHolesNotIntoMaterialOne();
  void deletingABoundaryDetachesItsEdgesAndRenumbersTheRest();
  void deletingAConductorFixesNodeSegmentAndArcReferences();
  void deletingACircuitFixesBlockLabelReferences();
  void referenceCountAnswersBeforeTheDelete();

  void everyCategoryOfEveryNewKindHasAnEditableSpec();
  void aSpecWritesBackIntoTheRecordItDescribes();
  void heatFlowBoundaryTypesEnableExactlyWhatTheClassicDialogDoes();
  void electrostaticsAndCurrentFlowBoundaryTypesMatchTheirClassicDialogs();
  void magneticsKeepsItsOwnDialogs();

  void everyShippedMaterialLibraryLoads();
  void everyShippedMaterialLibraryLoads_data();
  void heatLibraryKeepsItsFoldersAndItsConductivityCurve();
  void importingFromALibraryDisambiguatesTheName();

  void heatFlowExitCodesDoNotMatchTheOtherThreeSolvers();
  void theOtherThreeSolversShareOneExitCodeTable();
  void anUnknownExitCodeSaysSoRatherThanGuessing();
};

// ---------------------------------------------------------------------------
// The shared half
// ---------------------------------------------------------------------------

void TestProblemKinds::theCommonHeaderAndGeometrySurviveEveryKind_data()
{
  QTest::addColumn<int>("kindValue");
  QTest::newRow("magnetics") << (int)FemmProblemKind::Magnetics;
  QTest::newRow("electrostatics") << (int)FemmProblemKind::Electrostatics;
  QTest::newRow("heat flow") << (int)FemmProblemKind::HeatFlow;
  QTest::newRow("current flow") << (int)FemmProblemKind::CurrentFlow;
}

void TestProblemKinds::theCommonHeaderAndGeometrySurviveEveryKind()
{
  QFETCH(int, kindValue);
  const FemmProblemKind kind = (FemmProblemKind)kindValue;

  FemmProblem p;
  p.kind = kind;
  addCommonHeader(p);
  addGeometry(p);

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = pathFor(dir, kind);
  QString error;
  QVERIFY2(ProblemFileIO::write(path, p, error), qPrintable(error));

  FemmProblem back;
  QVERIFY2(ProblemFileIO::read(path, back, error), qPrintable(error));

  // The kind must come from the extension, with no help from the caller.
  QCOMPARE((int)back.kind, (int)kind);

  QCOMPARE(back.precision, 1e-9);
  QCOMPARE(back.minAngle, 32.5);
  QCOMPARE(back.smartMesh, false);
  QCOMPARE(back.depth, 17.5);
  QCOMPARE((int)back.lengthUnits, (int)FemmLengthUnits::Centimeters);
  QCOMPARE((int)back.problemType, (int)FemmCoordinateType::Axisymmetric);
  QCOMPARE(back.coordsPolar, true);
  QCOMPARE(back.extZo, 1.5);
  QCOMPARE(back.extRo, 2.5);
  QCOMPARE(back.extRi, 3.5);
  QCOMPARE(back.comment, QStringLiteral("a round trip"));

  QCOMPARE(back.nodes.size(), 3);
  QCOMPARE(back.nodes[0].x, 1.5);
  QCOMPARE(back.nodes[0].y, 2.5);
  QCOMPARE(back.nodes[0].pointPropIndex, 1);
  QCOMPARE(back.nodes[0].inGroup, 3);
  QCOMPARE(back.nodes[1].inGroup, 4);

  QCOMPARE(back.segments.size(), 1);
  QCOMPARE(back.segments[0].n0, 0);
  QCOMPARE(back.segments[0].n1, 1);
  QCOMPARE(back.segments[0].maxSideLength, 0.75);
  QCOMPARE(back.segments[0].boundaryMarker, 1);
  QCOMPARE(back.segments[0].hidden, true);
  QCOMPARE(back.segments[0].inGroup, 5);

  QCOMPARE(back.arcSegments.size(), 1);
  QCOMPARE(back.arcSegments[0].arcLength, 42.5);
  QCOMPARE(back.arcSegments[0].maxSideLength, 2.5);
  QCOMPARE(back.arcSegments[0].boundaryMarker, 1);
  QCOMPARE(back.arcSegments[0].inGroup, 6);
  // mySideLength is the column the conductor index shifts past in the
  // three formats that have one -- if that offset is wrong this is where
  // it shows, as mySideLength picking up a group number.
  QCOMPARE(back.arcSegments[0].mySideLength, 1.25);

  // One real label and one hole, in either order.
  QCOMPARE(back.blockLabels.size(), 2);
  int labels = 0, holes = 0;
  for (const FemmBlockLabel& b : back.blockLabels) {
    if (b.blockTypeIndex < 0) {
      holes++;
      QCOMPARE(b.x, 8.5);
      QCOMPARE(b.inGroup, 8);
    } else {
      labels++;
      QCOMPARE(b.x, 5.5);
      QCOMPARE(b.blockTypeIndex, 1);
      QCOMPARE(b.inGroup, 7);
      QVERIFY(std::abs(b.maxArea - 3.5) < 1e-9);
    }
  }
  QCOMPARE(labels, 1);
  QCOMPARE(holes, 1);
}

// ---------------------------------------------------------------------------
// Per-kind properties: every field, not a sample
// ---------------------------------------------------------------------------

void TestProblemKinds::magneticsPropertiesSurviveARoundTrip()
{
  FemmProblem p;
  p.kind = FemmProblemKind::Magnetics;
  p.frequency = 60.5;
  p.acSolver = 1;

  FemmPointProp pt;
  pt.name = "pp";
  pt.Jr = 1.5; pt.Ji = 2.5; pt.Ar = 3.5; pt.Ai = 4.5;
  p.pointProps.push_back(pt);

  FemmBoundaryProp bd;
  bd.name = "bd";
  bd.bdryFormat = 2;
  bd.A0 = 1.5; bd.A1 = 2.5; bd.A2 = 3.5; bd.phi = 4.5;
  bd.c0re = 5.5; bd.c0im = 6.5; bd.c1re = 7.5; bd.c1im = 8.5;
  bd.muSsd = 9.5; bd.sigmaSsd = 10.5; bd.innerAngle = 11.5; bd.outerAngle = 12.5;
  p.boundaryProps.push_back(bd);

  FemmMaterialProp m;
  m.name = "mat";
  m.muX = 1.5; m.muY = 2.5; m.Hc = 3.5; m.HcAngle = 4.5;
  m.JsrcRe = 5.5; m.JsrcIm = 6.5; m.sigma = 7.5; m.dLam = 8.5;
  m.phiH = 9.5; m.phiHx = 10.5; m.phiHy = 11.5;
  m.lamType = 3; m.lamFill = 0.5; m.nStrands = 7; m.wireD = 0.25;
  m.bhData.push_back({ 1.5, 2.5 });
  m.bhData.push_back({ 3.5, 4.5 });
  p.materialProps.push_back(m);

  FemmCircuitProp c;
  c.name = "coil";
  c.ampsRe = 1.5; c.ampsIm = 2.5; c.circType = 1;
  c.voltGradientRe = 3.5; c.voltGradientIm = 4.5;
  p.circuitProps.push_back(c);

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = pathFor(dir, p.kind);
  QString error;
  QVERIFY2(ProblemFileIO::write(path, p, error), qPrintable(error));
  FemmProblem b;
  QVERIFY2(ProblemFileIO::read(path, b, error), qPrintable(error));

  QCOMPARE(b.frequency, 60.5);
  QCOMPARE(b.acSolver, 1);

  QCOMPARE(b.pointProps.size(), 1);
  QCOMPARE(b.pointProps[0].name, QStringLiteral("pp"));
  QCOMPARE(b.pointProps[0].Jr, 1.5);
  QCOMPARE(b.pointProps[0].Ji, 2.5);
  QCOMPARE(b.pointProps[0].Ar, 3.5);
  QCOMPARE(b.pointProps[0].Ai, 4.5);

  QCOMPARE(b.boundaryProps.size(), 1);
  const FemmBoundaryProp& rb = b.boundaryProps[0];
  QCOMPARE(rb.bdryFormat, 2);
  QCOMPARE(rb.A0, 1.5); QCOMPARE(rb.A1, 2.5); QCOMPARE(rb.A2, 3.5); QCOMPARE(rb.phi, 4.5);
  QCOMPARE(rb.c0re, 5.5); QCOMPARE(rb.c0im, 6.5);
  QCOMPARE(rb.c1re, 7.5); QCOMPARE(rb.c1im, 8.5);
  QCOMPARE(rb.muSsd, 9.5); QCOMPARE(rb.sigmaSsd, 10.5);
  QCOMPARE(rb.innerAngle, 11.5); QCOMPARE(rb.outerAngle, 12.5);

  QCOMPARE(b.materialProps.size(), 1);
  const FemmMaterialProp& rm = b.materialProps[0];
  QCOMPARE(rm.muX, 1.5); QCOMPARE(rm.muY, 2.5);
  QCOMPARE(rm.Hc, 3.5); QCOMPARE(rm.HcAngle, 4.5);
  QCOMPARE(rm.JsrcRe, 5.5); QCOMPARE(rm.JsrcIm, 6.5);
  QCOMPARE(rm.sigma, 7.5); QCOMPARE(rm.dLam, 8.5);
  QCOMPARE(rm.phiH, 9.5); QCOMPARE(rm.phiHx, 10.5); QCOMPARE(rm.phiHy, 11.5);
  QCOMPARE(rm.lamType, 3); QCOMPARE(rm.lamFill, 0.5);
  QCOMPARE(rm.nStrands, 7); QCOMPARE(rm.wireD, 0.25);
  QCOMPARE(rm.bhData.size(), 2);
  QCOMPARE(rm.bhData[1].second, 4.5);

  QCOMPARE(b.circuitProps.size(), 1);
  QCOMPARE(b.circuitProps[0].ampsRe, 1.5);
  QCOMPARE(b.circuitProps[0].circType, 1);
  QCOMPARE(b.circuitProps[0].voltGradientIm, 4.5);
}

void TestProblemKinds::electrostaticsPropertiesSurviveARoundTrip()
{
  FemmProblem p;
  p.kind = FemmProblemKind::Electrostatics;

  FemmEsPointProp pt;
  pt.name = "pp"; pt.Vp = 1.5; pt.qp = 2.5;
  p.esPointProps.push_back(pt);

  FemmEsBoundaryProp bd;
  bd.name = "bd"; bd.bdryFormat = 1;
  bd.Vs = 1.5; bd.qs = 2.5; bd.c0 = 3.5; bd.c1 = 4.5;
  p.esBoundaryProps.push_back(bd);

  FemmEsMaterialProp m;
  m.name = "mat"; m.ex = 1.5; m.ey = 2.5; m.qv = 3.5;
  p.esMaterialProps.push_back(m);

  FemmConductorProp c;
  c.name = "cond"; c.valueRe = 1.5; c.fluxRe = 2.5; c.conductorType = 1;
  p.conductorProps.push_back(c);

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = pathFor(dir, p.kind);
  QString error;
  QVERIFY2(ProblemFileIO::write(path, p, error), qPrintable(error));
  FemmProblem b;
  QVERIFY2(ProblemFileIO::read(path, b, error), qPrintable(error));

  QCOMPARE((int)b.kind, (int)FemmProblemKind::Electrostatics);
  QCOMPARE(b.esPointProps.size(), 1);
  QCOMPARE(b.esPointProps[0].Vp, 1.5);
  QCOMPARE(b.esPointProps[0].qp, 2.5);
  QCOMPARE(b.esBoundaryProps.size(), 1);
  QCOMPARE(b.esBoundaryProps[0].bdryFormat, 1);
  QCOMPARE(b.esBoundaryProps[0].Vs, 1.5);
  QCOMPARE(b.esBoundaryProps[0].qs, 2.5);
  QCOMPARE(b.esBoundaryProps[0].c0, 3.5);
  QCOMPARE(b.esBoundaryProps[0].c1, 4.5);
  QCOMPARE(b.esMaterialProps.size(), 1);
  QCOMPARE(b.esMaterialProps[0].ex, 1.5);
  QCOMPARE(b.esMaterialProps[0].ey, 2.5);
  QCOMPARE(b.esMaterialProps[0].qv, 3.5);
  QCOMPARE(b.conductorProps.size(), 1);
  QCOMPARE(b.conductorProps[0].valueRe, 1.5);
  QCOMPARE(b.conductorProps[0].fluxRe, 2.5);
  QCOMPARE(b.conductorProps[0].conductorType, 1);

  // Magnetics' lists must stay empty -- a kind reading another kind's
  // records into its own lists is exactly the silent-wrong-model failure.
  QVERIFY(b.pointProps.isEmpty());
  QVERIFY(b.materialProps.isEmpty());
  QVERIFY(b.circuitProps.isEmpty());
}

void TestProblemKinds::heatFlowPropertiesSurviveARoundTrip()
{
  FemmProblem p;
  p.kind = FemmProblemKind::HeatFlow;
  p.heatTimeStep = 0.125;

  FemmHtPointProp pt;
  pt.name = "pp"; pt.Tp = 1.5; pt.qp = 2.5;
  p.htPointProps.push_back(pt);

  FemmHtBoundaryProp bd;
  bd.name = "bd"; bd.bdryFormat = 2;
  bd.Tset = 1.5; bd.qs = 2.5; bd.beta = 3.5; bd.h = 4.5;
  bd.Tinf = 5.5; bd.TinfRad = 6.5;
  p.htBoundaryProps.push_back(bd);

  FemmHtMaterialProp m;
  m.name = "mat"; m.Kx = 1.5; m.Ky = 2.5; m.Kt = 3.5; m.qv = 4.5;
  m.tkData.push_back({ 300.0, 15.5 });
  m.tkData.push_back({ 400.0, 16.5 });
  p.htMaterialProps.push_back(m);

  FemmConductorProp c;
  c.name = "cond"; c.valueRe = 1.5; c.fluxRe = 2.5; c.conductorType = 1;
  p.conductorProps.push_back(c);

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = pathFor(dir, p.kind);
  QString error;
  QVERIFY2(ProblemFileIO::write(path, p, error), qPrintable(error));
  FemmProblem b;
  QVERIFY2(ProblemFileIO::read(path, b, error), qPrintable(error));

  QCOMPARE(b.heatTimeStep, 0.125);
  QCOMPARE(b.htPointProps.size(), 1);
  QCOMPARE(b.htPointProps[0].Tp, 1.5);
  QCOMPARE(b.htPointProps[0].qp, 2.5);
  QCOMPARE(b.htBoundaryProps.size(), 1);
  const FemmHtBoundaryProp& rb = b.htBoundaryProps[0];
  QCOMPARE(rb.Tset, 1.5); QCOMPARE(rb.qs, 2.5);
  QCOMPARE(rb.beta, 3.5); QCOMPARE(rb.h, 4.5);
  QCOMPARE(rb.Tinf, 5.5); QCOMPARE(rb.TinfRad, 6.5);
  QCOMPARE(b.htMaterialProps.size(), 1);
  QCOMPARE(b.htMaterialProps[0].Kx, 1.5);
  QCOMPARE(b.htMaterialProps[0].Ky, 2.5);
  QCOMPARE(b.htMaterialProps[0].Kt, 3.5);
  QCOMPARE(b.htMaterialProps[0].qv, 4.5);
  QCOMPARE(b.htMaterialProps[0].tkData.size(), 2);
  QCOMPARE(b.htMaterialProps[0].tkData[1].second, 16.5);
  QCOMPARE(b.conductorProps.size(), 1);
  QCOMPARE(b.conductorProps[0].valueRe, 1.5);
}

void TestProblemKinds::currentFlowPropertiesSurviveARoundTrip()
{
  FemmProblem p;
  p.kind = FemmProblemKind::CurrentFlow;
  p.frequency = 50.5;

  FemmCfPointProp pt;
  pt.name = "pp"; pt.vpr = 1.5; pt.vpi = 2.5; pt.qpr = 3.5; pt.qpi = 4.5;
  p.cfPointProps.push_back(pt);

  FemmCfBoundaryProp bd;
  bd.name = "bd"; bd.bdryFormat = 2;
  bd.vsr = 1.5; bd.vsi = 2.5; bd.qsr = 3.5; bd.qsi = 4.5;
  bd.c0r = 5.5; bd.c0i = 6.5; bd.c1r = 7.5; bd.c1i = 8.5;
  p.cfBoundaryProps.push_back(bd);

  FemmCfMaterialProp m;
  m.name = "mat"; m.ox = 1.5; m.oy = 2.5; m.ex = 3.5; m.ey = 4.5;
  m.ltx = 5.5; m.lty = 6.5;
  p.cfMaterialProps.push_back(m);

  FemmConductorProp c;
  c.name = "cond";
  c.valueRe = 1.5; c.valueIm = 2.5; c.fluxRe = 3.5; c.fluxIm = 4.5;
  c.conductorType = 1;
  p.conductorProps.push_back(c);

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = pathFor(dir, p.kind);
  QString error;
  QVERIFY2(ProblemFileIO::write(path, p, error), qPrintable(error));
  FemmProblem b;
  QVERIFY2(ProblemFileIO::read(path, b, error), qPrintable(error));

  QCOMPARE(b.frequency, 50.5);
  QCOMPARE(b.cfPointProps.size(), 1);
  QCOMPARE(b.cfPointProps[0].vpr, 1.5);
  QCOMPARE(b.cfPointProps[0].vpi, 2.5);
  QCOMPARE(b.cfPointProps[0].qpr, 3.5);
  QCOMPARE(b.cfPointProps[0].qpi, 4.5);
  const FemmCfBoundaryProp& rb = b.cfBoundaryProps[0];
  QCOMPARE(rb.vsr, 1.5); QCOMPARE(rb.vsi, 2.5);
  QCOMPARE(rb.qsr, 3.5); QCOMPARE(rb.qsi, 4.5);
  QCOMPARE(rb.c0r, 5.5); QCOMPARE(rb.c0i, 6.5);
  QCOMPARE(rb.c1r, 7.5); QCOMPARE(rb.c1i, 8.5);
  const FemmCfMaterialProp& rm = b.cfMaterialProps[0];
  QCOMPARE(rm.ox, 1.5); QCOMPARE(rm.oy, 2.5);
  QCOMPARE(rm.ex, 3.5); QCOMPARE(rm.ey, 4.5);
  QCOMPARE(rm.ltx, 5.5); QCOMPARE(rm.lty, 6.5);
  // The only kind whose conductor uses all four numbers.
  QCOMPARE(b.conductorProps[0].valueIm, 2.5);
  QCOMPARE(b.conductorProps[0].fluxIm, 4.5);
}

// ---------------------------------------------------------------------------
// The places the formats genuinely differ
// ---------------------------------------------------------------------------

void TestProblemKinds::theConductorIndexSurvivesOnNodesSegmentsAndArcs()
{
  // #79 assumed the geometry rows were identical across the four. They
  // are not: .fee/.feh/.fec carry a conductor column on nodes, segments
  // and arcs that .fem has no room for. Losing it loses every
  // conductor assignment in the model.
  for (FemmProblemKind kind : { FemmProblemKind::Electrostatics,
           FemmProblemKind::HeatFlow, FemmProblemKind::CurrentFlow }) {
    FemmProblem p;
    p.kind = kind;
    addGeometry(p);
    p.nodes[0].conductorIndex = 2;
    p.segments[0].conductorIndex = 3;
    p.arcSegments[0].conductorIndex = 4;

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = pathFor(dir, kind);
    QString error;
    QVERIFY2(ProblemFileIO::write(path, p, error), qPrintable(error));
    FemmProblem b;
    QVERIFY2(ProblemFileIO::read(path, b, error), qPrintable(error));

    QCOMPARE(b.nodes[0].conductorIndex, 2);
    QCOMPARE(b.segments[0].conductorIndex, 3);
    QCOMPARE(b.arcSegments[0].conductorIndex, 4);
    // And the column after it must not have been eaten.
    QCOMPARE(b.arcSegments[0].mySideLength, 1.25);
  }
}

void TestProblemKinds::magneticsKeepsItsCircuitMagDirAndTurnsOnBlockLabels()
{
  // The other half of the same asymmetry: the .fem block-label row has
  // circuit, MagDir and turns where the other three have nothing.
  FemmProblem p;
  p.kind = FemmProblemKind::Magnetics;
  addGeometry(p);
  for (FemmBlockLabel& b : p.blockLabels) {
    if (b.blockTypeIndex < 0)
      continue;
    b.circuitIndex = 2;
    b.magDir = 37.5;
    b.turns = 11;
    b.isExternal = true;
    b.magDirFctn = "theta + 90";
  }

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = pathFor(dir, p.kind);
  QString error;
  QVERIFY2(ProblemFileIO::write(path, p, error), qPrintable(error));
  FemmProblem b;
  QVERIFY2(ProblemFileIO::read(path, b, error), qPrintable(error));

  bool checked = false;
  for (const FemmBlockLabel& l : b.blockLabels) {
    if (l.blockTypeIndex < 0)
      continue;
    QCOMPARE(l.circuitIndex, 2);
    QCOMPARE(l.magDir, 37.5);
    QCOMPARE(l.turns, 11);
    QCOMPARE(l.isExternal, true);
    // A function with a space in it must survive: it is taken from the
    // raw line rather than the whitespace-split columns for that reason.
    QCOMPARE(l.magDirFctn, QStringLiteral("theta + 90"));
    checked = true;
  }
  QVERIFY(checked);
}

// ---------------------------------------------------------------------------
// The refactor guard
// ---------------------------------------------------------------------------

void TestProblemKinds::theNewDriverAndTheOldMagneticsReaderAgree()
{
  // Magnetics is the one kind that already worked. If the shared driver
  // and FemmFileIO disagree about a .fem, that is a regression in
  // something users already rely on, not a missing feature.
  FemmProblem p;
  p.kind = FemmProblemKind::Magnetics;
  addCommonHeader(p);
  addGeometry(p);
  p.frequency = 60.5;

  FemmMaterialProp m;
  m.name = "steel";
  m.muX = 2000; m.muY = 2100; m.sigma = 5.8;
  m.bhData.push_back({ 0.5, 100.0 });
  p.materialProps.push_back(m);

  FemmCircuitProp c;
  c.name = "coil";
  c.ampsRe = 3.5;
  p.circuitProps.push_back(c);

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.path() + "/agree.fem";
  QString error;
  QVERIFY2(ProblemFileIO::write(path, p, error), qPrintable(error));

  FemmProblem viaNew, viaOld;
  QVERIFY2(ProblemFileIO::read(path, viaNew, error), qPrintable(error));
  QVERIFY2(FemmFileIO::readFem(path, viaOld, error), qPrintable(error));

  QCOMPARE(viaOld.nodes.size(), viaNew.nodes.size());
  QCOMPARE(viaOld.segments.size(), viaNew.segments.size());
  QCOMPARE(viaOld.arcSegments.size(), viaNew.arcSegments.size());
  QCOMPARE(viaOld.blockLabels.size(), viaNew.blockLabels.size());
  QCOMPARE(viaOld.materialProps.size(), viaNew.materialProps.size());
  QCOMPARE(viaOld.circuitProps.size(), viaNew.circuitProps.size());

  QCOMPARE(viaOld.frequency, viaNew.frequency);
  QCOMPARE(viaOld.depth, viaNew.depth);
  QCOMPARE((int)viaOld.lengthUnits, (int)viaNew.lengthUnits);
  QCOMPARE(viaOld.coordsPolar, viaNew.coordsPolar);
  QCOMPARE(viaOld.comment, viaNew.comment);

  QCOMPARE(viaOld.arcSegments[0].arcLength, viaNew.arcSegments[0].arcLength);
  QCOMPARE(viaOld.arcSegments[0].mySideLength, viaNew.arcSegments[0].mySideLength);
  QCOMPARE(viaOld.materialProps[0].muX, viaNew.materialProps[0].muX);
  QCOMPARE(viaOld.materialProps[0].bhData.size(), viaNew.materialProps[0].bhData.size());
  QCOMPARE(viaOld.circuitProps[0].ampsRe, viaNew.circuitProps[0].ampsRe);
}

// ---------------------------------------------------------------------------
// Refusals
// ---------------------------------------------------------------------------

void TestProblemKinds::openRefusesAnExtensionItDoesNotKnow()
{
  // Reading a .fee as magnetics would parse none of its tags and hand
  // back a model that is silently empty of properties, which looks like
  // a model the user had not finished rather than a file we misread.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.path() + "/model.txt";
  QFile f(path);
  QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
  f.write("[Format] = 4.0\n");
  f.close();

  FemmProblem p;
  QString error;
  QVERIFY2(!ProblemFileIO::read(path, p, error), "an unknown extension was read anyway");
  QVERIFY(!error.isEmpty());
  QVERIFY2(error.contains(".fem") && error.contains(".fec"),
      "the refusal should say which extensions are understood");
}

void TestProblemKinds::saveRefusesAnExtensionBelongingToAnotherKind()
{
  // Writing heat flow under a .fem name produces a file that opens as
  // magnetics and loses every property in it.
  FemmProblem p;
  p.kind = FemmProblemKind::HeatFlow;
  addGeometry(p);

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString wrong = dir.path() + "/model.fem";
  QString error;
  QVERIFY2(!ProblemFileIO::write(wrong, p, error),
      "a heat-flow problem was written under a magnetics extension");
  QVERIFY2(!QFile::exists(wrong), "the refused save left a file behind anyway");
  QVERIFY(error.contains("Heat Flow"));
  QVERIFY(error.contains("Magnetics"));

  // The right extension still works.
  const QString right = dir.path() + "/model.feh";
  QVERIFY2(ProblemFileIO::write(right, p, error), qPrintable(error));
}

// ---------------------------------------------------------------------------
// The mapping everything else routes on
// ---------------------------------------------------------------------------

void TestProblemKinds::everyKindMapsToItsOwnExtensionSolverAndSolutionFile()
{
  // Exhaustive over the four rather than a sample. This table is what
  // Open routes on, what Save names a file with, and what the solve step
  // will launch (#82) -- one wrong row sends a heat-flow model to the
  // magnetics solver, which reads it, finds none of its own tags, and
  // solves an empty problem rather than failing.
  const struct {
    FemmProblemKind kind;
    const char* display;
    const char* ext;
    const char* solver;
    const char* solution;
  } expected[] = {
    { FemmProblemKind::Magnetics, "Magnetics", "fem", "fkn.exe", "ans" },
    { FemmProblemKind::Electrostatics, "Electrostatics", "fee", "belasolv.exe", "res" },
    { FemmProblemKind::HeatFlow, "Heat Flow", "feh", "hsolv.exe", "anh" },
    { FemmProblemKind::CurrentFlow, "Current Flow", "fec", "csolv.exe", "anc" },
  };

  for (const auto& e : expected) {
    QCOMPARE(ProblemKind::displayName(e.kind), QString::fromLatin1(e.display));
    QCOMPARE(ProblemKind::extension(e.kind), QString::fromLatin1(e.ext));
    QCOMPARE(ProblemKind::solverExecutable(e.kind), QString::fromLatin1(e.solver));
    QCOMPARE(ProblemKind::solutionExtension(e.kind), QString::fromLatin1(e.solution));
  }

  // The one label that differs between the physics: magnetics drives a
  // region with a circuit, the other three hold a surface at a potential
  // with a conductor.
  QCOMPARE(ProblemKind::categoryLabel(FemmProblemKind::Magnetics, ProblemKind::Category::Source),
      QStringLiteral("Circuits"));
  QCOMPARE(ProblemKind::categoryLabel(FemmProblemKind::HeatFlow, ProblemKind::Category::Source),
      QStringLiteral("Conductors"));
}

void TestProblemKinds::anExtensionNeverResolvesToTheWrongKind()
{
  const struct {
    const char* path;
    bool recognised;
    FemmProblemKind kind;
  } cases[] = {
    { "c:/models/motor.fem", true, FemmProblemKind::Magnetics },
    { "c:/models/motor.FEM", true, FemmProblemKind::Magnetics },
    { "c:/models/cap.fee", true, FemmProblemKind::Electrostatics },
    { "c:/models/sink.feh", true, FemmProblemKind::HeatFlow },
    { "c:/models/trace.fec", true, FemmProblemKind::CurrentFlow },
    // .femx is a CACHE, not a model format. Resolving it to a kind here
    // is what would let a stale .femx beside a .fee be opened as the
    // model -- the wrong geometry, silently.
    { "c:/models/motor.femx", false, FemmProblemKind::Magnetics },
    { "c:/models/motor.ans", false, FemmProblemKind::Magnetics },
    { "c:/models/notes.txt", false, FemmProblemKind::Magnetics },
    { "c:/models/noextension", false, FemmProblemKind::Magnetics },
  };

  for (const auto& c : cases) {
    FemmProblemKind got = FemmProblemKind::CurrentFlow; // deliberately not the default
    const bool ok = ProblemKind::kindForPath(QString::fromLatin1(c.path), got);
    QVERIFY2(ok == c.recognised,
        qPrintable(QStringLiteral("%1: expected recognised=%2")
                       .arg(c.path).arg(c.recognised)));
    if (c.recognised)
      QCOMPARE((int)got, (int)c.kind);
  }
}

// ---------------------------------------------------------------------------
// List editing, and the renumbering that makes it dangerous (issue #81)
// ---------------------------------------------------------------------------
//
// Deleting a property is the operation with teeth. Every reference is
// 1-based with 0 meaning none, so removing entry i has to turn every
// reference equal to i+1 into none and drop every higher one by one.
// Getting it wrong does not throw: the geometry silently re-points at
// the neighbouring material, and the model still solves.

void TestProblemKinds::addingAPropertyGivesItAUniqueNameInEveryKind()
{
  for (FemmProblemKind kind : { FemmProblemKind::Magnetics,
           FemmProblemKind::Electrostatics, FemmProblemKind::HeatFlow,
           FemmProblemKind::CurrentFlow }) {
    FemmProblem p;
    p.kind = kind;
    for (ProblemKind::Category cat : { ProblemKind::Category::Point,
             ProblemKind::Category::Boundary, ProblemKind::Category::Material,
             ProblemKind::Category::Source }) {
      const int a = ProblemKind::addDefault(p, cat);
      const int b = ProblemKind::addDefault(p, cat);
      const int c = ProblemKind::addDefault(p, cat);
      QCOMPARE(ProblemKind::count(p, cat), 3);
      const QString na = ProblemKind::name(p, cat, a);
      const QString nb = ProblemKind::name(p, cat, b);
      const QString nc = ProblemKind::name(p, cat, c);
      QVERIFY2(!na.isEmpty(), "a new property came back nameless");
      // Duplicate names are not cosmetic: the classic writer identifies
      // a property BY NAME, so two entries sharing one makes the second
      // unreachable -- the defect #34 fixed in the shipped libraries.
      QVERIFY2(na != nb && nb != nc && na != nc,
          qPrintable(QStringLiteral("duplicate names: %1 / %2 / %3").arg(na, nb, nc)));
    }
  }
}

void TestProblemKinds::deletingAMaterialTurnsItsLabelsIntoHolesNotIntoMaterialOne()
{
  // Materials are the one category whose "none" is -1 rather than 0,
  // because a block label with no material IS a hole. Using 0 here would
  // turn every label that used the deleted material into a meshed region
  // of the FIRST material -- a solved model, with the wrong physics in
  // part of it.
  FemmProblem p;
  p.kind = FemmProblemKind::Electrostatics;
  ProblemKind::addDefault(p, ProblemKind::Category::Material); // 1
  ProblemKind::addDefault(p, ProblemKind::Category::Material); // 2
  ProblemKind::addDefault(p, ProblemKind::Category::Material); // 3

  FemmProblemEdit::addBlockLabel(p, 0, 0);
  FemmProblemEdit::addBlockLabel(p, 1, 1);
  FemmProblemEdit::addBlockLabel(p, 2, 2);
  p.blockLabels[0].blockTypeIndex = 1; // the one being deleted
  p.blockLabels[1].blockTypeIndex = 2; // must shift down to 1
  p.blockLabels[2].blockTypeIndex = 3; // must shift down to 2

  ProblemKind::remove(p, ProblemKind::Category::Material, 0);

  QCOMPARE(ProblemKind::count(p, ProblemKind::Category::Material), 2);
  QCOMPARE(p.blockLabels[0].blockTypeIndex, -1);
  QCOMPARE(p.blockLabels[1].blockTypeIndex, 1);
  QCOMPARE(p.blockLabels[2].blockTypeIndex, 2);
}

void TestProblemKinds::deletingABoundaryDetachesItsEdgesAndRenumbersTheRest()
{
  FemmProblem p;
  p.kind = FemmProblemKind::HeatFlow;
  ProblemKind::addDefault(p, ProblemKind::Category::Boundary);
  ProblemKind::addDefault(p, ProblemKind::Category::Boundary);

  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 1, 0);
  const int c = FemmProblemEdit::addNode(p, 1, 1);
  FemmProblemEdit::addSegment(p, a, b);
  FemmProblemEdit::addArcSegment(p, b, c, 90.0, 1.0);
  p.segments[0].boundaryMarker = 1;
  p.arcSegments[0].boundaryMarker = 2;

  ProblemKind::remove(p, ProblemKind::Category::Boundary, 0);

  QCOMPARE(p.segments[0].boundaryMarker, 0); // detached
  QCOMPARE(p.arcSegments[0].boundaryMarker, 1); // shifted down
}

void TestProblemKinds::deletingAConductorFixesNodeSegmentAndArcReferences()
{
  // Conductors hang off nodes, segments AND arcs in the three
  // non-magnetics formats. Fixing up only one of the three leaves the
  // others pointing at whatever moved into the slot.
  for (FemmProblemKind kind : { FemmProblemKind::Electrostatics,
           FemmProblemKind::HeatFlow, FemmProblemKind::CurrentFlow }) {
    FemmProblem p;
    p.kind = kind;
    ProblemKind::addDefault(p, ProblemKind::Category::Source);
    ProblemKind::addDefault(p, ProblemKind::Category::Source);

    const int a = FemmProblemEdit::addNode(p, 0, 0);
    const int b = FemmProblemEdit::addNode(p, 1, 0);
    const int c = FemmProblemEdit::addNode(p, 1, 1);
    FemmProblemEdit::addSegment(p, a, b);
    FemmProblemEdit::addArcSegment(p, b, c, 90.0, 1.0);
    p.nodes[a].conductorIndex = 1;
    p.segments[0].conductorIndex = 2;
    p.arcSegments[0].conductorIndex = 2;

    ProblemKind::remove(p, ProblemKind::Category::Source, 0);

    QCOMPARE(p.nodes[a].conductorIndex, 0);
    QCOMPARE(p.segments[0].conductorIndex, 1);
    QCOMPARE(p.arcSegments[0].conductorIndex, 1);
  }
}

void TestProblemKinds::deletingACircuitFixesBlockLabelReferences()
{
  // The magnetics half of the same asymmetry: a circuit hangs off a
  // block label, not off edges.
  FemmProblem p;
  p.kind = FemmProblemKind::Magnetics;
  ProblemKind::addDefault(p, ProblemKind::Category::Source);
  ProblemKind::addDefault(p, ProblemKind::Category::Source);

  FemmProblemEdit::addBlockLabel(p, 0, 0);
  FemmProblemEdit::addBlockLabel(p, 1, 1);
  p.blockLabels[0].circuitIndex = 1;
  p.blockLabels[1].circuitIndex = 2;

  ProblemKind::remove(p, ProblemKind::Category::Source, 0);

  // A circuit's "none" is 0, unlike a material's -1.
  QCOMPARE(p.blockLabels[0].circuitIndex, 0);
  QCOMPARE(p.blockLabels[1].circuitIndex, 1);
}

void TestProblemKinds::referenceCountAnswersBeforeTheDelete()
{
  FemmProblem p;
  p.kind = FemmProblemKind::CurrentFlow;
  ProblemKind::addDefault(p, ProblemKind::Category::Material);
  ProblemKind::addDefault(p, ProblemKind::Category::Material);

  for (int i = 0; i < 3; i++)
    FemmProblemEdit::addBlockLabel(p, i, i);
  p.blockLabels[0].blockTypeIndex = 1;
  p.blockLabels[1].blockTypeIndex = 1;
  p.blockLabels[2].blockTypeIndex = 2;

  QCOMPARE(ProblemKind::referenceCount(p, ProblemKind::Category::Material, 0), 2);
  QCOMPARE(ProblemKind::referenceCount(p, ProblemKind::Category::Material, 1), 1);
  // Out of range answers zero rather than reading past the end -- this
  // is called from a list whose selection can go stale.
  QCOMPARE(ProblemKind::referenceCount(p, ProblemKind::Category::Material, 7), 0);
}

// ---------------------------------------------------------------------------
// The per-kind field specs (issue #81)
// ---------------------------------------------------------------------------
//
// The dialog itself needs widgets and is checked by eye; the SPEC is
// data and is checked here. What matters is that every category of every
// kind produces something editable, that writing through a spec lands in
// the right member, and that each boundary-condition type enables
// exactly the fields the classic dialog enables -- the last one being
// the requirement #81 states outright.

void TestProblemKinds::everyCategoryOfEveryNewKindHasAnEditableSpec()
{
  for (FemmProblemKind kind : { FemmProblemKind::Electrostatics,
           FemmProblemKind::HeatFlow, FemmProblemKind::CurrentFlow }) {
    FemmProblem p;
    p.kind = kind;
    for (ProblemKind::Category cat : { ProblemKind::Category::Point,
             ProblemKind::Category::Boundary, ProblemKind::Category::Material,
             ProblemKind::Category::Source }) {
      QVERIFY2(PropertyFields::hasSpec(p, cat), "a new kind reported no spec");
      const int i = ProblemKind::addDefault(p, cat);
      const PropertyFields::Spec spec = PropertyFields::specFor(p, cat, i);

      QVERIFY2(!spec.title.isEmpty(), "a spec came back with no title");
      QVERIFY2(spec.getName && spec.setName, "a spec cannot read or write its name");
      QVERIFY2(!spec.fields.isEmpty(),
          qPrintable(QStringLiteral("kind %1 category %2 produced a spec with no "
                                    "fields, so the dialog would be an empty box")
                         .arg((int)kind).arg((int)cat)));
      for (const PropertyFields::Field& f : spec.fields) {
        QVERIFY2(!f.label.isEmpty(), "a field has no label");
        QVERIFY2(f.get && f.set, "a field cannot read or write its value");
      }
    }
  }
}

void TestProblemKinds::aSpecWritesBackIntoTheRecordItDescribes()
{
  // A spec whose setter writes the wrong member is the silent failure
  // here: the dialog closes, the value is gone, and nothing says so.
  FemmProblem p;
  p.kind = FemmProblemKind::HeatFlow;
  const int i = ProblemKind::addDefault(p, ProblemKind::Category::Material);
  PropertyFields::Spec spec = PropertyFields::specFor(p, ProblemKind::Category::Material, i);

  spec.setName("copper");
  QCOMPARE(p.htMaterialProps[i].name, QStringLiteral("copper"));

  // Four fields, in the order the classic dialog lists them: kx, ky,
  // volumetric heat capacity, volume heat generation.
  QCOMPARE(spec.fields.size(), 4);
  spec.fields[0].set("401");
  spec.fields[1].set("402");
  spec.fields[2].set("3.45");
  spec.fields[3].set("1000");
  QCOMPARE(p.htMaterialProps[i].Kx, 401.0);
  QCOMPARE(p.htMaterialProps[i].Ky, 402.0);
  QCOMPARE(p.htMaterialProps[i].Kt, 3.45);
  QCOMPARE(p.htMaterialProps[i].qv, 1000.0);

  // And reading comes back from the same members.
  QCOMPARE(spec.fields[0].get().toDouble(), 401.0);
  QCOMPARE(spec.fields[3].get().toDouble(), 1000.0);
}

void TestProblemKinds::heatFlowBoundaryTypesEnableExactlyWhatTheClassicDialogDoes()
{
  // Straight from femm/hd_BdryDlg.cpp's OnSelchangeBdryformat:
  //   0 Fixed Temperature -> Tset
  //   1 Heat Flux         -> qs
  //   2 Convection        -> qs, h, Tinf
  //   3 Radiation         -> qs, h, Tinf, beta, TinfRad
  FemmProblem p;
  p.kind = FemmProblemKind::HeatFlow;
  const int i = ProblemKind::addDefault(p, ProblemKind::Category::Boundary);
  const PropertyFields::Spec spec =
      PropertyFields::specFor(p, ProblemKind::Category::Boundary, i);

  QCOMPARE(spec.selector.options.size(), 4);
  QCOMPARE(spec.fields.size(), 6);

  const QVector<QVector<int>> expected = {
    { 0 },          // Tset
    { 1, 2, 3 },    // qs
    { 2, 3 },       // h
    { 2, 3 },       // Tinf
    { 3 },          // beta
    { 3 },          // TinfRad
  };
  for (int f = 0; f < expected.size(); f++) {
    QVERIFY2(spec.fields[f].enabledForOptions == expected[f],
        qPrintable(QStringLiteral("field %1 (%2) is enabled for the wrong "
                                  "boundary types")
                       .arg(f).arg(spec.fields[f].label)));
  }
}

void TestProblemKinds::electrostaticsAndCurrentFlowBoundaryTypesMatchTheirClassicDialogs()
{
  // femm/bd_BdryDlg.cpp and cd_BdryDlg.cpp have the identical switch:
  //   0 Fixed Voltage -> the voltage field(s)
  //   1 Mixed         -> c0, c1
  //   2 Surface       -> the surface density field(s)
  {
    FemmProblem p;
    p.kind = FemmProblemKind::Electrostatics;
    const int i = ProblemKind::addDefault(p, ProblemKind::Category::Boundary);
    const PropertyFields::Spec spec =
        PropertyFields::specFor(p, ProblemKind::Category::Boundary, i);
    QCOMPARE(spec.selector.options.size(), 3);
    QCOMPARE(spec.fields.size(), 4);
    QCOMPARE(spec.fields[0].enabledForOptions, QVector<int>({ 0 })); // Vs
    QCOMPARE(spec.fields[1].enabledForOptions, QVector<int>({ 2 })); // qs
    QCOMPARE(spec.fields[2].enabledForOptions, QVector<int>({ 1 })); // c0
    QCOMPARE(spec.fields[3].enabledForOptions, QVector<int>({ 1 })); // c1
  }
  {
    FemmProblem p;
    p.kind = FemmProblemKind::CurrentFlow;
    const int i = ProblemKind::addDefault(p, ProblemKind::Category::Boundary);
    const PropertyFields::Spec spec =
        PropertyFields::specFor(p, ProblemKind::Category::Boundary, i);
    QCOMPARE(spec.selector.options.size(), 3);
    // Current flow is complex everywhere, so each of the three groups is
    // a real/imaginary pair.
    QCOMPARE(spec.fields.size(), 8);
    QCOMPARE(spec.fields[0].enabledForOptions, QVector<int>({ 0 }));
    QCOMPARE(spec.fields[1].enabledForOptions, QVector<int>({ 0 }));
    QCOMPARE(spec.fields[2].enabledForOptions, QVector<int>({ 2 }));
    QCOMPARE(spec.fields[3].enabledForOptions, QVector<int>({ 2 }));
    QCOMPARE(spec.fields[4].enabledForOptions, QVector<int>({ 1 }));
    QCOMPARE(spec.fields[7].enabledForOptions, QVector<int>({ 1 }));
  }
}

void TestProblemKinds::magneticsKeepsItsOwnDialogs()
{
  // Deliberate, and worth asserting so it is a decision rather than an
  // oversight: the magnetics dialogs already match the classic ones
  // field for field, and two of them do things a flat field list cannot
  // describe -- the BH curve editor, and the point property's radio pair
  // that ZEROES the other pair rather than merely disabling it.
  FemmProblem p;
  p.kind = FemmProblemKind::Magnetics;
  for (ProblemKind::Category cat : { ProblemKind::Category::Point,
           ProblemKind::Category::Boundary, ProblemKind::Category::Material,
           ProblemKind::Category::Source }) {
    QVERIFY2(!PropertyFields::hasSpec(p, cat),
        "magnetics reported a field spec, which would route it away from its "
        "own dialogs and lose the BH curve editor");
  }
}

// ---------------------------------------------------------------------------
// The shipped material libraries (issue #81)
// ---------------------------------------------------------------------------
//
// These read the REAL files this repo ships -- matlib.dat, statlib.dat,
// heatlib.dat, condlib.dat -- rather than a fixture. They are the actual
// input, they are large, and a loader that silently produces an empty
// tree looks exactly like a library with nothing in it.

namespace {

// The libraries live beside the built executable at run time and in
// bin/ in the source tree; tests run from the build directory, so the
// source copy is the reliable one.
QString libraryPath(FemmProblemKind kind)
{
  const QString repo = QFileInfo(QStringLiteral(FEMMQT_SOURCE_DIR)).absolutePath();
  return repo + "/bin/" + MaterialLibraryIO::defaultFileName(kind);
}

int countLeaves(const MaterialLibraryNode& node)
{
  if (!node.isFolder)
    return 1;
  int n = 0;
  for (const MaterialLibraryNode& c : node.children)
    n += countLeaves(c);
  return n;
}

bool findLeaf(const MaterialLibraryNode& node, const QString& name, MaterialLibraryNode& out)
{
  if (!node.isFolder) {
    if (node.name == name) {
      out = node;
      return true;
    }
    return false;
  }
  for (const MaterialLibraryNode& c : node.children) {
    if (findLeaf(c, name, out))
      return true;
  }
  return false;
}

} // namespace

void TestProblemKinds::everyShippedMaterialLibraryLoads_data()
{
  QTest::addColumn<int>("kindValue");
  QTest::addColumn<QString>("knownMaterial");
  QTest::addColumn<int>("expectedMaterials");

  // The exact count of <BeginBlock> records in each shipped file, not a
  // lower bound. A bound of "more than a few" passed while condlib.dat
  // -- which really does contain just two materials -- looked suspicious
  // and every other library could have been quietly losing entries.
  // Pinning the real number catches a loader that drops a folder's
  // contents, which is the failure that matters for the two nested ones.
  QTest::newRow("matlib.dat") << (int)FemmProblemKind::Magnetics << "Air" << 246;
  QTest::newRow("statlib.dat") << (int)FemmProblemKind::Electrostatics << "Air" << 26;
  QTest::newRow("heatlib.dat") << (int)FemmProblemKind::HeatFlow << "Aluminum, Pure" << 136;
  QTest::newRow("condlib.dat") << (int)FemmProblemKind::CurrentFlow << "Copper" << 2;
}

void TestProblemKinds::everyShippedMaterialLibraryLoads()
{
  QFETCH(int, kindValue);
  QFETCH(QString, knownMaterial);
  QFETCH(int, expectedMaterials);
  const FemmProblemKind kind = (FemmProblemKind)kindValue;

  const QString path = libraryPath(kind);
  QVERIFY2(QFile::exists(path),
      qPrintable(QStringLiteral("the shipped library %1 is missing").arg(path)));

  MaterialLibraryNode root;
  QString error;
  QVERIFY2(MaterialLibraryIO::load(path, kind, root, error), qPrintable(error));

  const int leaves = countLeaves(root);
  QVERIFY2(leaves == expectedMaterials,
      qPrintable(QStringLiteral("%1 holds %2 materials but the loader produced %3 -- "
                                "a tree that is short by a folder looks exactly like "
                                "a smaller library")
                     .arg(MaterialLibraryIO::defaultFileName(kind))
                     .arg(expectedMaterials)
                     .arg(leaves)));

  MaterialLibraryNode found;
  QVERIFY2(findLeaf(root, knownMaterial, found),
      qPrintable(QStringLiteral("%1 does not contain \"%2\"")
                     .arg(MaterialLibraryIO::defaultFileName(kind), knownMaterial)));
  QVERIFY(!found.isFolder);
}

void TestProblemKinds::heatLibraryKeepsItsFoldersAndItsConductivityCurve()
{
  // heatlib.dat is the one with real structure: two levels of folders,
  // and materials carrying a temperature/conductivity curve. Flattening
  // the folders or dropping the curve would still "load".
  MaterialLibraryNode root;
  QString error;
  QVERIFY2(MaterialLibraryIO::load(libraryPath(FemmProblemKind::HeatFlow),
               FemmProblemKind::HeatFlow, root, error),
      qPrintable(error));

  bool sawNestedFolder = false;
  for (const MaterialLibraryNode& top : root.children) {
    if (!top.isFolder)
      continue;
    for (const MaterialLibraryNode& mid : top.children) {
      if (mid.isFolder)
        sawNestedFolder = true;
    }
  }
  QVERIFY2(sawNestedFolder, "the folder nesting was flattened");

  MaterialLibraryNode alu;
  QVERIFY(findLeaf(root, QStringLiteral("Aluminum, Pure"), alu));
  QCOMPARE(alu.htMaterial.Kx, 236.0);
  QCOMPARE(alu.htMaterial.Ky, 236.0);
  QVERIFY2(alu.htMaterial.tkData.size() == 4,
      qPrintable(QStringLiteral("expected a 4-point conductivity curve, got %1")
                     .arg(alu.htMaterial.tkData.size())));
  QCOMPARE(alu.htMaterial.tkData[0].first, 273.0);
  QCOMPARE(alu.htMaterial.tkData[0].second, 236.0);
  QCOMPARE(alu.htMaterial.tkData[3].first, 873.0);
  QCOMPARE(alu.htMaterial.tkData[3].second, 215.0);
}

void TestProblemKinds::importingFromALibraryDisambiguatesTheName()
{
  // Importing the same material twice must not produce two entries with
  // one name: the writer identifies a material BY NAME, so the second
  // would be unreachable -- the defect #34 fixed in these very files.
  MaterialLibraryNode root;
  QString error;
  QVERIFY(MaterialLibraryIO::load(libraryPath(FemmProblemKind::CurrentFlow),
      FemmProblemKind::CurrentFlow, root, error));

  MaterialLibraryNode copper;
  QVERIFY(findLeaf(root, QStringLiteral("Copper"), copper));

  FemmProblem p;
  p.kind = FemmProblemKind::CurrentFlow;
  const int first = MaterialLibraryIO::appendTo(p, copper);
  const int second = MaterialLibraryIO::appendTo(p, copper);
  QVERIFY(first >= 0 && second >= 0);
  QCOMPARE(ProblemKind::count(p, ProblemKind::Category::Material), 2);

  const QString n1 = ProblemKind::name(p, ProblemKind::Category::Material, first);
  const QString n2 = ProblemKind::name(p, ProblemKind::Category::Material, second);
  QCOMPARE(n1, QStringLiteral("Copper"));
  QVERIFY2(n1 != n2, "importing the same material twice produced two entries "
                     "sharing one name");

  // And the values actually came across, into the right list.
  QVERIFY(p.cfMaterialProps[first].ox > 0);
  QCOMPARE(p.cfMaterialProps[first].ox, p.cfMaterialProps[second].ox);
}

// ---------------------------------------------------------------------------
// Solver exit codes (issue #82)
// ---------------------------------------------------------------------------
//
// #82 said to verify each solver's codes against its own main.cpp rather
// than assume they match fkn's. They do not, and these cases pin the
// difference down so it cannot quietly be "simplified" back into one
// table later.

void TestProblemKinds::heatFlowExitCodesDoNotMatchTheOtherThreeSolvers()
{
  // From hsolv/main.cpp: 2 mesh, 3 previous-solution load (silent there),
  // 4 renumber, 5 allocate, 6 solve, 7 input file OR write failure.
  // Every code from 3 up means something different than it does in the
  // other three solvers.
  const QString mesh = SolveRunner::exitMessage(FemmProblemKind::HeatFlow, 2);
  QVERIFY2(mesh.contains("mesh"), qPrintable(mesh));

  const QString prev = SolveRunner::exitMessage(FemmProblemKind::HeatFlow, 3);
  QVERIFY2(prev.contains("previous solution"),
      qPrintable(QStringLiteral("hsolv's 3 is a failed previous-solution load, "
                                "not a renumbering failure; got: %1").arg(prev)));

  const QString renumber = SolveRunner::exitMessage(FemmProblemKind::HeatFlow, 4);
  QVERIFY2(renumber.contains("renumber"), qPrintable(renumber));

  const QString allocate = SolveRunner::exitMessage(FemmProblemKind::HeatFlow, 5);
  QVERIFY2(allocate.contains("allocate"), qPrintable(allocate));

  const QString solve = SolveRunner::exitMessage(FemmProblemKind::HeatFlow, 6);
  QVERIFY2(solve.contains("solve"), qPrintable(solve));

  // The one genuinely ambiguous code in any of the four: hsolv exits 7
  // both for a bad .feh and for a failed write. Saying so is better than
  // picking one and being wrong half the time.
  const QString seven = SolveRunner::exitMessage(FemmProblemKind::HeatFlow, 7);
  QVERIFY2(seven.contains("feh") && seven.contains("write"),
      qPrintable(QStringLiteral("hsolv's 7 has two meanings and the message should "
                                "carry both; got: %1").arg(seven)));

  // And the contrast that makes the whole case worth having: the same
  // code means something else everywhere else.
  QVERIFY2(SolveRunner::exitMessage(FemmProblemKind::HeatFlow, 5)
          != SolveRunner::exitMessage(FemmProblemKind::Magnetics, 5),
      "heat flow and magnetics were given the same meaning for code 5, which "
      "is exactly the mistake #82 warned about");
}

void TestProblemKinds::theOtherThreeSolversShareOneExitCodeTable()
{
  // fkn, belasolv and csolv really do agree, so they share a table --
  // asserted rather than assumed, since that is the half of the claim
  // that justifies not writing three more tables.
  for (int code = 1; code <= 7; code++) {
    const QString mag = SolveRunner::exitMessage(FemmProblemKind::Magnetics, code);
    QCOMPARE(SolveRunner::exitMessage(FemmProblemKind::Electrostatics, code), mag);
    QCOMPARE(SolveRunner::exitMessage(FemmProblemKind::CurrentFlow, code), mag);
    QVERIFY2(!mag.isEmpty(), "an exit code came back with no explanation at all");
  }
}

void TestProblemKinds::anUnknownExitCodeSaysSoRatherThanGuessing()
{
  for (FemmProblemKind kind : { FemmProblemKind::Magnetics,
           FemmProblemKind::Electrostatics, FemmProblemKind::HeatFlow,
           FemmProblemKind::CurrentFlow }) {
    const QString msg = SolveRunner::exitMessage(kind, 99);
    QVERIFY2(msg.contains("99"),
        qPrintable(QStringLiteral("an unrecognised code should be reported with its "
                                  "number rather than mapped to the nearest known "
                                  "failure; got: %1").arg(msg)));
  }
}

QTEST_GUILESS_MAIN(TestProblemKinds)
#include "tst_problem_kinds.moc"
