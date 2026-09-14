// tst_density_units.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-14,
// per direct user report: "the density plots in the problems other than
// magnetics have the wrong units".
//
// They did, and on the Density Plot Options dialog you could see both
// answers at once. The quantity combo was hard-coded to magnetics' ten
// entries -- "|B| (Tesla)", "|H| (Amp/m)", "|J| (MA/m^2)" -- whatever
// the physics, while the range group box beside it asks the item for
// its legend title and so correctly said "Heat flux |F|, W/m^2". One
// dialog, one plot, two sets of units.
//
// The renderer itself was right: SolutionAdapter carries each physics'
// own field into the same B1/B2 slots and the numbers are that field in
// SI. Only the naming was wrong -- which is the worse kind of wrong
// here, because a plot labelled in Tesla is one a reader will believe.
//
// The three non-magnetics physics have exactly ONE density quantity
// each: the field their solver stores. |H| and |J| are derived from
// permeability and conductivity, which mean something else or nothing
// at all in those formats.

#include <QtTest>

#include "DensityPlotOptionsDialog.h"
#include "MeshSolution.h"
#include "ProblemKind.h"
#include "SolutionField.h"
#include "SolutionView.h"

#include <QComboBox>
#include <QFileInfo>
#include <QFile>
#include <QLabel>
#include "SolutionFileIO.h"
#include "AnsFileIO.h"

namespace {

// Magnetics' vocabulary. None of it may appear on another physics'
// dialog.
const QStringList kMagneticsUnits = { "Tesla", "Amp/m", "MA/m^2" };

QVector<FemmProblemKind> otherPhysics()
{
  return { FemmProblemKind::Electrostatics, FemmProblemKind::HeatFlow,
    FemmProblemKind::CurrentFlow };
}

QComboBox* quantityCombo(DensityPlotOptionsDialog& dlg)
{
  // The first combo in the dialog is the quantity one.
  return dlg.findChild<QComboBox*>();
}

} // namespace

class TestDensityUnits : public QObject
{
  Q_OBJECT

  private slots:
  void theQuantityIsNamedInThisPhysicsUnits();
  void theQuantityIsNamedInThisPhysicsUnits_data();

  void noOtherPhysicsQuantityIsOffered();
  void noOtherPhysicsQuantityIsOffered_data();

  void magneticsKeepsAllTenQuantities();
  void theComboAndTheLegendAgree();
  void theComboAndTheLegendAgree_data();

  // Issue #93.
  void theHoverReadoutUsesThisPhysicsUnits();
  void theHoverReadoutUsesThisPhysicsUnits_data();

  private:
  MeshSolution m_empty;
};

void TestDensityUnits::theQuantityIsNamedInThisPhysicsUnits_data()
{
  QTest::addColumn<int>("kind");
  QTest::addColumn<QString>("unit");

  QTest::newRow("electrostatics") << (int)FemmProblemKind::Electrostatics << "C/m^2";
  QTest::newRow("heat flow") << (int)FemmProblemKind::HeatFlow << "W/m^2";
  QTest::newRow("current flow") << (int)FemmProblemKind::CurrentFlow << "A/m^2";
}

void TestDensityUnits::theQuantityIsNamedInThisPhysicsUnits()
{
  QFETCH(int, kind);
  QFETCH(QString, unit);
  const FemmProblemKind k = (FemmProblemKind)kind;

  MeshSolutionItem item(&m_empty);
  DensityPlotOptionsDialog dlg(&item, true, false, k);
  QComboBox* combo = quantityCombo(dlg);
  QVERIFY2(combo, "the dialog has no quantity combo");

  QVERIFY2(combo->count() == 1,
      qPrintable(QStringLiteral("%1 offers %2 density quantities; it has one "
                                "field")
                     .arg(ProblemKind::displayName(k)).arg(combo->count())));

  const QString text = combo->itemText(0);
  QVERIFY2(text.contains(unit),
      qPrintable(QStringLiteral("the quantity reads \"%1\" -- expected %2")
                     .arg(text, unit)));

  // And it agrees with the one place that decides what the renderer is
  // actually showing, rather than being a second opinion.
  const SolutionField::Quantity f = SolutionField::fieldQuantity(k);
  QVERIFY2(text.contains(f.name),
      qPrintable(QStringLiteral("\"%1\" does not name %2").arg(text, f.name)));
  QCOMPARE(f.unit, unit);
}

void TestDensityUnits::noOtherPhysicsQuantityIsOffered_data()
{
  theQuantityIsNamedInThisPhysicsUnits_data();
}

void TestDensityUnits::noOtherPhysicsQuantityIsOffered()
{
  QFETCH(int, kind);
  const FemmProblemKind k = (FemmProblemKind)kind;

  MeshSolutionItem item(&m_empty);
  DensityPlotOptionsDialog dlg(&item, true, false, k);
  QComboBox* combo = quantityCombo(dlg);
  QVERIFY(combo);

  for (int i = 0; i < combo->count(); i++) {
    const QString text = combo->itemText(i);
    for (const QString& magnetic : kMagneticsUnits) {
      QVERIFY2(!text.contains(magnetic),
          qPrintable(QStringLiteral("a %1 dialog offers \"%2\" -- that is "
                                    "magnetics' unit, and this is exactly the "
                                    "reported defect")
                         .arg(ProblemKind::displayName(k), text)));
    }
  }

  // An AC solution must not reopen the door: the ten-entry list is
  // selected by frequency for magnetics, and that branch has to stay
  // behind the physics check.
  DensityPlotOptionsDialog ac(&item, true, true, k);
  QComboBox* acCombo = quantityCombo(ac);
  QVERIFY(acCombo);
  QCOMPARE(acCombo->count(), 1);
}

void TestDensityUnits::magneticsKeepsAllTenQuantities()
{
  // The fix must not cost magnetics the choice it has always had --
  // narrowing every physics to one quantity would "fix" the units by
  // removing the feature.
  MeshSolutionItem item(&m_empty);

  DensityPlotOptionsDialog dc(&item, true, false, FemmProblemKind::Magnetics);
  QComboBox* dcCombo = quantityCombo(dc);
  QVERIFY(dcCombo);
  QCOMPARE(dcCombo->count(), 4); // |B|, |H|, |J|, log10(|B|)
  QVERIFY(dcCombo->isEnabled());

  DensityPlotOptionsDialog ac(&item, true, true, FemmProblemKind::Magnetics);
  QComboBox* acCombo = quantityCombo(ac);
  QVERIFY(acCombo);
  QCOMPARE(acCombo->count(), MeshSolutionItem::kDensityQuantityCount);
  QVERIFY2(acCombo->itemText(0).contains("Tesla"),
      qPrintable(acCombo->itemText(0)));
}

void TestDensityUnits::theComboAndTheLegendAgree_data()
{
  theQuantityIsNamedInThisPhysicsUnits_data();
}

void TestDensityUnits::theComboAndTheLegendAgree()
{
  // The defect was visible as a disagreement between two widgets on one
  // dialog: the combo said Tesla and the range box, which asks the item
  // for its legend title, said W/m^2. Whatever else changes, those two
  // must not be able to say different things again.
  QFETCH(int, kind);
  QFETCH(QString, unit);
  const FemmProblemKind k = (FemmProblemKind)kind;

  MeshSolutionItem item(&m_empty);
  const SolutionField::Quantity f = SolutionField::fieldQuantity(k);
  item.setFieldLabelOverride(QStringLiteral("%1, %2").arg(f.name, f.unit));

  DensityPlotOptionsDialog dlg(&item, true, false, k);
  QComboBox* combo = quantityCombo(dlg);
  QVERIFY(combo);

  const QString legend = item.legendTitle(MeshSolutionItem::DensityQuantity::BMag);
  QVERIFY2(legend.contains(unit), qPrintable(legend));
  QVERIFY2(combo->itemText(0).contains(unit), qPrintable(combo->itemText(0)));
}

// ---------------------------------------------------------------------------
// The point readouts (issue #93)
// ---------------------------------------------------------------------------
//
// The density legend was fixed first, and the hover readout, Point
// Properties and Plot X-Y kept reporting magnetics' letters and units
// over the other three physics' data -- tesla and webers on top of
// temperatures in kelvin, updating on every mouse move, right next to a
// legend that said W/m^2 correctly.
//
// Driven through the real window rather than by reading the format
// strings: the labels are assembled from several pieces, and a scan
// that saw the right pieces in the source would still miss them being
// put together for the wrong branch.

namespace {

// A point guaranteed to be inside the mesh: the centroid of its first
// element, read from the solution file itself. Picking coordinates by
// hand would silently start missing if a fixture were regenerated.
bool firstElementCentroid(const QString& path, FemmProblemKind kind, QPointF& out)
{
  if (kind == FemmProblemKind::Magnetics) {
    FemmProblem problem;
    MeshSolution mesh;
    QString error;
    if (!AnsFileIO::readAns(path, problem, mesh, error) || mesh.elements.isEmpty())
      return false;
    const MeshSolutionElement& e = mesh.elements.first();
    out = QPointF((mesh.nodes[e.p0].x + mesh.nodes[e.p1].x + mesh.nodes[e.p2].x) / 3.0,
        (mesh.nodes[e.p0].y + mesh.nodes[e.p1].y + mesh.nodes[e.p2].y) / 3.0);
    return true;
  }

  FemmProblem problem;
  SolvedMesh mesh;
  QString error;
  if (!SolutionFileIO::read(path, problem, mesh, error) || mesh.elements.isEmpty())
    return false;
  const SolutionElement& e = mesh.elements.first();
  out = QPointF((mesh.nodes[e.p0].x + mesh.nodes[e.p1].x + mesh.nodes[e.p2].x) / 3.0,
      (mesh.nodes[e.p0].y + mesh.nodes[e.p1].y + mesh.nodes[e.p2].y) / 3.0);
  return true;
}

QString repoRoot()
{
  return QFileInfo(QFileInfo(QStringLiteral(FEMMQT_SOURCE_DIR)).absoluteFilePath())
      .absolutePath();
}

} // namespace

void TestDensityUnits::theHoverReadoutUsesThisPhysicsUnits_data()
{
  QTest::addColumn<int>("kind");
  QTest::addColumn<QString>("fixture");
  QTest::addColumn<QStringList>("expected");
  QTest::addColumn<QStringList>("forbidden");

  QTest::newRow("magnetics")
      << (int)FemmProblemKind::Magnetics
      << "manual_qt/images/example.ans"
      << QStringList{ "|B|", "T", "|H|", "A/m" }
      << QStringList{ "W/m^2", "C/m^2", " K" };

  QTest::newRow("heat flow")
      << (int)FemmProblemKind::HeatFlow
      << "manual_qt/images/example.anh"
      << QStringList{ "|F|", "W/m^2", "T =", "K" }
      // Magnetics' vocabulary, none of which describes a temperature.
      << QStringList{ "Wb", "MA/m^2", "|B|", "|H|", "Js+Je" };

  QTest::newRow("electrostatics")
      << (int)FemmProblemKind::Electrostatics
      << "test/fixtures/solutions/parallel_plate.res"
      << QStringList{ "|D|", "C/m^2", "V =" }
      << QStringList{ "Wb", "MA/m^2", "|B|", "|H|", "Js+Je" };

  QTest::newRow("current flow")
      << (int)FemmProblemKind::CurrentFlow
      << "test/fixtures/solutions/current_bar.anc"
      << QStringList{ "|J|", "A/m^2", "V =" }
      << QStringList{ "Wb", "MA/m^2", "|B|", "|H|", "Js+Je" };
}

void TestDensityUnits::theHoverReadoutUsesThisPhysicsUnits()
{
  QFETCH(int, kind);
  QFETCH(QString, fixture);
  QFETCH(QStringList, expected);
  QFETCH(QStringList, forbidden);
  const FemmProblemKind k = (FemmProblemKind)kind;

  const QString path = repoRoot() + "/" + fixture;
  QVERIFY2(QFile::exists(path), qPrintable(path + " is missing"));

  QPointF inside;
  QVERIFY2(firstElementCentroid(path, k, inside),
      qPrintable(QStringLiteral("could not find a mesh element in %1").arg(fixture)));

  SolutionWindow window;
  QVERIFY2(window.openSolutionFile(path), qPrintable("could not open " + fixture));

  // The slot the canvas calls on mouse move.
  QVERIFY2(QMetaObject::invokeMethod(&window, "onCanvasHovered",
               Q_ARG(QPointF, inside)),
      "onCanvasHovered is no longer an invokable slot");

  // The status bar's readout: the label that starts with the coordinates.
  QString readout;
  for (QLabel* l : window.findChildren<QLabel*>()) {
    if (l->text().startsWith(QStringLiteral("x =")))
      readout = l->text();
  }
  QVERIFY2(!readout.isEmpty(),
      qPrintable(QStringLiteral("no hover readout appeared for %1 -- the point "
                                "%2,%3 may be outside the mesh, which would make "
                                "this case pass by checking nothing")
                     .arg(fixture).arg(inside.x()).arg(inside.y())));

  for (const QString& want : expected) {
    QVERIFY2(readout.contains(want),
        qPrintable(QStringLiteral("%1: the readout does not mention \"%2\": %3")
                       .arg(fixture, want, readout)));
  }
  for (const QString& nope : forbidden) {
    QVERIFY2(!readout.contains(nope),
        qPrintable(QStringLiteral("%1: the readout says \"%2\", which is not this "
                                  "physics' unit: %3")
                       .arg(fixture, nope, readout)));
  }
}

QTEST_MAIN(TestDensityUnits)
#include "tst_density_units.moc"
