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

QTEST_MAIN(TestDensityUnits)
#include "tst_density_units.moc"
