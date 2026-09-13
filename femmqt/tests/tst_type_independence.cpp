// tst_type_independence.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #84).
//
// The risk #79 actually runs: not that one problem type is wrong, but
// that adding types quietly breaks the type-independence the whole
// design rests on. The CAD layer -- geometry, constraints, dimensions,
// snapping, trim, offset, chamfer -- is shared by all four physics
// precisely because it touches nodes, segments, arcs and block labels
// and never touches a property. The moment one per-type branch appears
// in it, that stops being true, and nothing about the symptom would
// point at the cause.
//
// Two guards, because one alone is not enough:
//
//   STATIC  -- no file in the CAD layer may mention the problem kind.
//              Catches the branch on the day it is written, including in
//              code no test happens to exercise.
//
//   BEHAVIOURAL -- the same sketch built in all four document types must
//              produce identical geometry. Catches a leak that arrives
//              indirectly, through a shared helper that started
//              consulting the kind.
//
// A static check alone would pass a leak routed through another file. A
// behavioural check alone would pass a branch in a path these cases do
// not reach. Together they are hard to slip past.

#include <QtTest>

#include "ConstraintSolver.h"
#include "FemmProblem.h"
#include "FemmProblemEdit.h"
#include "OffsetChamfer.h"
#include "ProblemKind.h"
#include "PropertyFields.h"
#include "SketchTransform.h"
#include "TrimExtend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace {

const QStringList kAllKindNames = { "Magnetics", "Electrostatics", "Heat Flow", "Current Flow" };

QVector<FemmProblemKind> allKinds()
{
  return { FemmProblemKind::Magnetics, FemmProblemKind::Electrostatics,
    FemmProblemKind::HeatFlow, FemmProblemKind::CurrentFlow };
}

QString femmqtDir()
{
  return QFileInfo(QStringLiteral(FEMMQT_SOURCE_DIR)).absoluteFilePath();
}

QString readAll(const QString& path)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return QString();
  return QString::fromUtf8(f.readAll());
}

// A representative sketch: nodes, a segment, an arc, a block label, a
// constraint and a dimension. Identical in every kind -- that is the
// point.
FemmProblem buildSketch(FemmProblemKind kind)
{
  FemmProblem p;
  p.kind = kind;
  p.problemType = FemmCoordinateType::Planar;
  p.lengthUnits = FemmLengthUnits::Millimeters;

  const int a = FemmProblemEdit::addNode(p, 0, 0);
  const int b = FemmProblemEdit::addNode(p, 10, 0);
  const int c = FemmProblemEdit::addNode(p, 10, 10);
  const int d = FemmProblemEdit::addNode(p, 0, 10);
  FemmProblemEdit::addSegment(p, a, b);
  FemmProblemEdit::addSegment(p, b, c);
  FemmProblemEdit::addArcSegment(p, c, d, 90.0, 1.0);
  FemmProblemEdit::addBlockLabel(p, 5, 5);

  FemmConstraint con;
  con.type = ConstraintType::Horizontal;
  con.refA = 0;
  p.constraints << con;

  FemmDimension dim;
  dim.type = DimensionType::Distance;
  dim.refA = a;
  dim.refB = b;
  dim.value = 10.0;
  p.dimensions << dim;
  return p;
}

// Everything about the geometry that a physics must not be able to
// change, as one comparable string.
QString geometryFingerprint(const FemmProblem& p)
{
  QString s;
  for (const FemmNode& n : p.nodes)
    s += QStringLiteral("N %1 %2 %3;").arg(n.x, 0, 'g', 17).arg(n.y, 0, 'g', 17).arg(n.inGroup);
  for (const FemmSegment& g : p.segments)
    s += QStringLiteral("S %1 %2 %3;").arg(g.n0).arg(g.n1).arg(g.maxSideLength, 0, 'g', 17);
  for (const FemmArcSegment& g : p.arcSegments)
    s += QStringLiteral("A %1 %2 %3 %4;").arg(g.n0).arg(g.n1)
             .arg(g.arcLength, 0, 'g', 17).arg(g.maxSideLength, 0, 'g', 17);
  for (const FemmBlockLabel& g : p.blockLabels)
    s += QStringLiteral("L %1 %2;").arg(g.x, 0, 'g', 17).arg(g.y, 0, 'g', 17);
  for (const FemmConstraint& c : p.constraints)
    s += QStringLiteral("C %1 %2 %3 %4;").arg((int)c.type).arg(c.refA).arg(c.refB).arg(c.refC);
  for (const FemmDimension& d : p.dimensions)
    s += QStringLiteral("D %1 %2 %3 %4;").arg((int)d.type).arg(d.refA).arg(d.refB)
             .arg(d.value, 0, 'g', 17);
  return s;
}

} // namespace

class TestTypeIndependence : public QObject
{
  Q_OBJECT

  private slots:
  void theCadLayerNeverMentionsTheProblemKind();
  void theSameSketchIsIdenticalInAllFourTypes();
  void trimExtendAndSplitBehaveIdenticallyInAllFourTypes();
  void offsetAndChamferBehaveIdenticallyInAllFourTypes();
  void constraintsSolveIdenticallyInAllFourTypes();
  void transformsCarryTheSketchIdenticallyInAllFourTypes();

  void noTypesPropertyDialogOffersAnotherTypesFields();
  void everyKindHasItsOwnSolverBinary();

  void femmqtWritesEveryTagTheClassicEditorWrites();
  void femmqtWritesEveryTagTheClassicEditorWrites_data();
};

// ---------------------------------------------------------------------------
// The static guard
// ---------------------------------------------------------------------------

void TestTypeIndependence::theCadLayerNeverMentionsTheProblemKind()
{
  // The files that make up the CAD layer. If one of these starts
  // consulting the problem kind, the sketch layer is no longer shared
  // and the four-types design has quietly become four editors.
  const QStringList cadFiles = {
    "GeometryScene.cpp", "GeometryScene.h",
    "GeometryView.cpp", "GeometryView.h",
    "ConstraintSolver.cpp", "ConstraintSolver.h",
    "SnapEngine.cpp", "SnapEngine.h",
    "TrimExtend.cpp", "TrimExtend.h",
    "OffsetChamfer.cpp", "OffsetChamfer.h",
    "SketchTransform.cpp", "SketchTransform.h",
    "ConstructionGeometry.cpp", "ConstructionGeometry.h",
  };

  const QRegularExpression kindRef(
      QStringLiteral("FemmProblemKind|ProblemKind::|PropertyCodec::"));

  QStringList offenders;
  for (const QString& name : cadFiles) {
    const QString path = femmqtDir() + "/" + name;
    QVERIFY2(QFile::exists(path),
        qPrintable(QStringLiteral("%1 is gone -- update this list, do not delete "
                                  "the check").arg(name)));
    const QString text = readAll(path);
    // Strip comments: the files are allowed to EXPLAIN that they are
    // type-blind, and several deliberately do.
    QString code = text;
    code.remove(QRegularExpression(QStringLiteral("//[^\\n]*")));
    if (kindRef.match(code).hasMatch())
      offenders << name;
  }

  QVERIFY2(offenders.isEmpty(),
      qPrintable(QStringLiteral("the CAD layer references the problem kind in %1. "
                                "The sketch layer is shared by all four physics "
                                "precisely because it never does this.")
                     .arg(offenders.join(", "))));
}

// ---------------------------------------------------------------------------
// The behavioural guard
// ---------------------------------------------------------------------------

void TestTypeIndependence::theSameSketchIsIdenticalInAllFourTypes()
{
  QString reference;
  for (FemmProblemKind kind : allKinds()) {
    const QString fp = geometryFingerprint(buildSketch(kind));
    if (reference.isEmpty())
      reference = fp;
    QVERIFY2(fp == reference,
        qPrintable(QStringLiteral("building the same sketch as %1 produced different "
                                  "geometry than as Magnetics")
                       .arg(ProblemKind::displayName(kind))));
  }
}

void TestTypeIndependence::trimExtendAndSplitBehaveIdenticallyInAllFourTypes()
{
  // #29's operations, run per type rather than in a parallel suite.
  QString reference;
  for (FemmProblemKind kind : allKinds()) {
    FemmProblem p;
    p.kind = kind;
    const int a = FemmProblemEdit::addNode(p, 0, 0);
    const int b = FemmProblemEdit::addNode(p, 10, 0);
    FemmProblemEdit::addSegment(p, a, b);
    const int c = FemmProblemEdit::addNode(p, 3, -2);
    const int d = FemmProblemEdit::addNode(p, 3, 2);
    FemmProblemEdit::addSegment(p, c, d);

    QVERIFY(TrimExtend::split(p, TrimExtend::EntityKind::Segment, 0, 6, 0).ok);
    QVERIFY(TrimExtend::trim(p, TrimExtend::EntityKind::Segment, 0, 1, 0).ok);

    const QString fp = geometryFingerprint(p);
    if (reference.isEmpty())
      reference = fp;
    QVERIFY2(fp == reference,
        qPrintable(QStringLiteral("trim/split gave a different result in %1")
                       .arg(ProblemKind::displayName(kind))));
  }
}

void TestTypeIndependence::offsetAndChamferBehaveIdenticallyInAllFourTypes()
{
  // #30's operations. Offset deliberately does NOT copy the boundary
  // marker, which is a property concept -- so if it ever started
  // consulting the kind to decide what to copy, this would diverge.
  QString reference;
  for (FemmProblemKind kind : allKinds()) {
    FemmProblem p;
    p.kind = kind;
    const int a = FemmProblemEdit::addNode(p, 0, 0);
    const int b = FemmProblemEdit::addNode(p, 10, 0);
    const int c = FemmProblemEdit::addNode(p, 10, 10);
    FemmProblemEdit::addSegment(p, a, b);
    FemmProblemEdit::addSegment(p, b, c);
    for (FemmSegment& s : p.segments)
      s.isSelected = true;

    QVERIFY(OffsetChamfer::offsetSelection(p, 1.0, OffsetChamfer::CornerStyle::Miter).ok);

    const QString fp = geometryFingerprint(p);
    if (reference.isEmpty())
      reference = fp;
    QVERIFY2(fp == reference,
        qPrintable(QStringLiteral("offset gave a different result in %1")
                       .arg(ProblemKind::displayName(kind))));
  }
}

void TestTypeIndependence::constraintsSolveIdenticallyInAllFourTypes()
{
  // #12's solver, per type. The constraint solver moves NODES to satisfy
  // relations; nothing about that is physics.
  QString reference;
  for (FemmProblemKind kind : allKinds()) {
    FemmProblem p = buildSketch(kind);
    // Pull a node off-axis, then let the Horizontal constraint pull it
    // back -- a real solve, not a no-op.
    p.nodes[1].y = 2.5;
    ConstraintSolver::solve(p);

    const QString fp = geometryFingerprint(p);
    if (reference.isEmpty())
      reference = fp;
    QVERIFY2(fp == reference,
        qPrintable(QStringLiteral("the constraint solve landed elsewhere in %1")
                       .arg(ProblemKind::displayName(kind))));
  }
}

void TestTypeIndependence::transformsCarryTheSketchIdenticallyInAllFourTypes()
{
  // #32's work: copies carry their constraints, and the orientation
  // rules are geometric rather than physical.
  QString reference;
  for (FemmProblemKind kind : allKinds()) {
    FemmProblem p = buildSketch(kind);
    for (FemmNode& n : p.nodes)
      n.isSelected = true;
    for (FemmSegment& s : p.segments)
      s.isSelected = true;

    SketchTransform::Report report;
    FemmProblemEdit::rotateCopySelected(p, 0, 0, 90.0, 1, &report);

    const QString fp = geometryFingerprint(p);
    if (reference.isEmpty())
      reference = fp;
    QVERIFY2(fp == reference,
        qPrintable(QStringLiteral("rotate-copy gave a different result in %1")
                       .arg(ProblemKind::displayName(kind))));
  }
}

// ---------------------------------------------------------------------------
// Negative cases
// ---------------------------------------------------------------------------

void TestTypeIndependence::noTypesPropertyDialogOffersAnotherTypesFields()
{
  // Each physics' field labels are distinctive. A spec that leaked
  // another type's fields would show, say, a permeability box on an
  // electrostatics material -- which a user could fill in, and which
  // would go nowhere.
  const struct {
    FemmProblemKind kind;
    QStringList mustNotAppear;
  } cases[] = {
    { FemmProblemKind::Electrostatics,
        { "permeability", "Thermal", "conductivity, S/m", "Kelvin" } },
    { FemmProblemKind::HeatFlow,
        { "permeability", "permittivity", "Conductivity" } },
    { FemmProblemKind::CurrentFlow,
        { "permeability", "Thermal", "temperature" } },
  };

  for (const auto& c : cases) {
    FemmProblem p;
    p.kind = c.kind;
    for (ProblemKind::Category cat : { ProblemKind::Category::Point,
             ProblemKind::Category::Boundary, ProblemKind::Category::Material,
             ProblemKind::Category::Source }) {
      const int i = ProblemKind::addDefault(p, cat);
      const PropertyFields::Spec spec = PropertyFields::specFor(p, cat, i);
      for (const PropertyFields::Field& f : spec.fields) {
        for (const QString& banned : c.mustNotAppear) {
          QVERIFY2(!f.label.contains(banned, Qt::CaseSensitive),
              qPrintable(QStringLiteral("%1's %2 dialog offers \"%3\", which belongs "
                                        "to another physics")
                             .arg(ProblemKind::displayName(c.kind))
                             .arg((int)cat)
                             .arg(f.label)));
        }
      }
    }
  }
}

void TestTypeIndependence::everyKindHasItsOwnSolverBinary()
{
  // A wrong-solver regression does not produce an error: the solver
  // reads the file, finds none of its own tags, and solves an empty
  // problem. So the mapping is asserted to be a bijection rather than
  // just non-empty.
  QSet<QString> seen;
  for (FemmProblemKind kind : allKinds()) {
    const QString exe = ProblemKind::solverExecutable(kind);
    QVERIFY2(!exe.isEmpty(), "a problem kind has no solver");
    QVERIFY2(!seen.contains(exe),
        qPrintable(QStringLiteral("%1 shares a solver binary (%2) with another type")
                       .arg(ProblemKind::displayName(kind), exe)));
    seen.insert(exe);
  }
  QCOMPARE(seen.size(), 4);
}

// ---------------------------------------------------------------------------
// Cross-GUI parity, per type (issue #84)
// ---------------------------------------------------------------------------
//
// The ticket asks that a file femmqt writes be readable by the classic
// editor with every property surviving. Driving both GUIs against each
// other is not practical here -- they are modal Windows applications,
// and the solvers already demonstrated that running one headless blocks
// on a message box.
//
// What IS practical, and catches the failure that matters, is comparing
// the TAG SETS. A property only survives a round trip if both writers
// agree on what it is called; a tag femmqt omits is a property the
// classic editor will not find, and a tag it spells differently is a
// property silently read back as zero.
//
// This is the same technique that found the <voltgradient> defect: a
// tag one side wrote and the other did not. It runs as a text scan, so
// it needs neither GUI.

namespace {

QString repoRoot()
{
  return QFileInfo(femmqtDir()).absolutePath();
}

// Every tag written inside the four property sections of one classic
// document writer.
QSet<QString> classicPropertyTags(const QString& docFile)
{
  QSet<QString> tags;
  QFile f(repoRoot() + "/femm/" + docFile);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return tags;
  const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');

  bool inSave = false, inProps = false;
  const QRegularExpression tagRe(QStringLiteral("<([A-Za-z_0-9]+)>"));
  for (const QString& line : lines) {
    if (line.contains(QStringLiteral("OnSaveDocument")))
      inSave = true;
    if (!inSave)
      continue;
    if (line.contains(QStringLiteral("[PointProps]")))
      inProps = true;
    // Geometry begins after the property sections; stop there.
    if (line.contains(QStringLiteral("[NumPoints]")))
      break;
    if (!inProps || !line.contains(QStringLiteral("fprintf")))
      continue;
    QRegularExpressionMatchIterator it = tagRe.globalMatch(line);
    while (it.hasNext())
      tags.insert(it.next().captured(1).toLower());
  }
  return tags;
}

// Every tag femmqt's codec writes for one kind. The codec is one
// function per kind in a single file, so the kind's section is bounded
// by its own case label.
QSet<QString> femmqtPropertyTags(const QString& caseLabel)
{
  QSet<QString> tags;
  QFile f(femmqtDir() + "/PropertyCodec.cpp");
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return tags;
  const QString text = QString::fromUtf8(f.readAll());

  // The writing half only: reading uses the same tag names, and
  // including it would mask a tag that is parsed but never written --
  // exactly the <voltgradient> shape.
  const int writeStart = text.indexOf(QStringLiteral("void PropertyCodec::writeSections"));
  if (writeStart < 0)
    return tags;
  const QString writeHalf = text.mid(writeStart);

  const int start = writeHalf.indexOf(caseLabel);
  if (start < 0)
    return tags;
  int end = writeHalf.indexOf(QStringLiteral("  case FemmProblemKind::"), start + caseLabel.size());
  if (end < 0)
    end = writeHalf.size();

  const QString section = writeHalf.mid(start, end - start);
  // tagQ/tagD/tagI("Name", ...) and the section headers.
  const QRegularExpression re(QStringLiteral("tag[QDI]?\\s*\\(\\s*out\\s*,\\s*\"([A-Za-z_0-9]+)\""));
  QRegularExpressionMatchIterator it = re.globalMatch(section);
  while (it.hasNext())
    tags.insert(it.next().captured(1).toLower());
  // The Begin/End delimiters are written literally.
  const QRegularExpression lit(QStringLiteral("\"\\s*<([A-Za-z_0-9]+)>"));
  it = lit.globalMatch(section);
  while (it.hasNext())
    tags.insert(it.next().captured(1).toLower());
  return tags;
}

} // namespace

void TestTypeIndependence::femmqtWritesEveryTagTheClassicEditorWrites_data()
{
  QTest::addColumn<QString>("classicDoc");
  QTest::addColumn<QString>("caseLabel");

  QTest::newRow("magnetics") << "FemmeDoc.cpp" << "case FemmProblemKind::Magnetics:";
  QTest::newRow("electrostatics") << "beladrawDoc.cpp" << "case FemmProblemKind::Electrostatics:";
  QTest::newRow("heat flow") << "hdrawDoc.cpp" << "case FemmProblemKind::HeatFlow:";
  QTest::newRow("current flow") << "cdrawDoc.cpp" << "case FemmProblemKind::CurrentFlow:";
}

void TestTypeIndependence::femmqtWritesEveryTagTheClassicEditorWrites()
{
  QFETCH(QString, classicDoc);
  QFETCH(QString, caseLabel);

  const QSet<QString> classic = classicPropertyTags(classicDoc);
  QVERIFY2(classic.size() > 5,
      qPrintable(QStringLiteral("found only %1 tags in femm/%2 -- the scan is broken, "
                                "which would make this pass for the wrong reason")
                     .arg(classic.size(), 0).arg(classicDoc)));

  const QSet<QString> ours = femmqtPropertyTags(caseLabel);
  QVERIFY2(ours.size() > 5,
      qPrintable(QStringLiteral("found only %1 tags for %2 in PropertyCodec.cpp")
                     .arg(ours.size(), 0).arg(caseLabel)));

  QStringList missing;
  for (const QString& t : classic) {
    if (!ours.contains(t))
      missing << t;
  }
  missing.sort();

  QVERIFY2(missing.isEmpty(),
      qPrintable(QStringLiteral("the classic editor writes these property tags for "
                                "%1 and femmqt does not, so a file round-tripped "
                                "through femmqt loses them: <%2>")
                     .arg(classicDoc, missing.join(QStringLiteral(">, <")))));
}

QTEST_GUILESS_MAIN(TestTypeIndependence)
#include "tst_type_independence.moc"
