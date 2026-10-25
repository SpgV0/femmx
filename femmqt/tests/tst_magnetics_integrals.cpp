// tst_magnetics_integrals.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-10-25
// (issue #108).
//
// femmqt's magnetics block integrals against the classic post-processor,
// number for number, on the same solution files.
//
// test/fixtures/integrals/ holds classic-solved .ans files for six
// models and reference.json, which records classic's own
// mo_blockintegral(k) for k = 0..24 over several selections in each.
// make_reference.py beside them regenerates both. Nothing here is a
// hand derivation: an integral that is wrong by a constant -- a missing
// 1e6, a depth not converted, a 1/2 dropped from an energy -- still
// looks like a number, and only the original program knows the right
// one.
//
// One deliberate exception: classic computes the weighted-stress-tensor
// integrals (18..23) for ANY selection, but when its MakeMask rejects
// the selection -- because it touches a region that is not free space --
// it ignores the failure and integrates the previous selection's mask.
// The reference file shows it: coax "dielectric" repeats "inner"
// exactly. femmqt refuses those selections instead, and this test
// checks that it does, rather than comparing against a stale number.

#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "AnsFileIO.h"
#include "FemmProblem.h"
#include "MagneticsIntegrals.h"
#include "MeshSolution.h"

#include <cmath>

using MagneticsIntegrals::Complex;

namespace {

QString fixture(const QString& name)
{
  return QStringLiteral(FEMMQT_TEST_FIXTURE_DIR "/integrals/") + name;
}

const char* integralName(int k)
{
  static const char* names[25] = { "A.J", "A", "energy", "hysteresis/lam eddy", "resistive",
    "area", "total losses", "total current", "B x/r", "B y/z", "volume", "Lorentz x",
    "Lorentz y", "Lorentz x 2x", "Lorentz y 2x", "Lorentz torque", "Lorentz torque 2x",
    "coenergy", "WST x", "WST y", "WST x 2x", "WST y 2x", "WST torque", "WST torque 2x", "R^2" };
  return (k >= 0 && k < 25) ? names[k] : "?";
}

// The block label whose own (x, y) is this point -- the selections in the
// reference file are given by label position, as mo_selectblock takes.
int labelAt(const FemmProblem& p, double x, double y)
{
  for (int i = 0; i < p.blockLabels.size(); i++)
    if (std::abs(p.blockLabels[i].x - x) < 1e-9 && std::abs(p.blockLabels[i].y - y) < 1e-9)
      return i;
  return -1;
}

} // namespace

class TestMagneticsIntegrals : public QObject
{
  Q_OBJECT

  QJsonObject m_reference;

  private slots:
  void initTestCase();
  void everyIntegralMatchesClassic_data();
  void everyIntegralMatchesClassic();
  void aStressTensorSelectionTouchingIronIsRefused();
  void thePolynomialIntegralsAreExactForLinearFields();
};

void TestMagneticsIntegrals::initTestCase()
{
  QFile f(fixture("reference.json"));
  QVERIFY2(f.open(QIODevice::ReadOnly), "reference.json is missing");
  m_reference = QJsonDocument::fromJson(f.readAll()).object();
  QVERIFY2(m_reference.size() >= 6, "reference.json has fewer models than it should");
}

void TestMagneticsIntegrals::everyIntegralMatchesClassic_data()
{
  QTest::addColumn<QString>("model");
  QTest::addColumn<QString>("selection");
  QFile f(fixture("reference.json"));
  if (!f.open(QIODevice::ReadOnly))
    return;
  const QJsonObject ref = QJsonDocument::fromJson(f.readAll()).object();
  for (auto m = ref.begin(); m != ref.end(); ++m)
    for (const QString& s : m.value().toObject().keys())
      QTest::newRow(qPrintable(m.key() + "/" + s)) << m.key() << s;
}

void TestMagneticsIntegrals::everyIntegralMatchesClassic()
{
  QFETCH(QString, model);
  QFETCH(QString, selection);

  FemmProblem p;
  MeshSolution s;
  QString err;
  QVERIFY2(AnsFileIO::readAns(fixture(model + ".ans"), p, s, err), qPrintable(err));

  const QJsonObject entry = m_reference.value(model).toObject().value(selection).toObject();
  QSet<int> labels;
  for (const QJsonValue& pt : entry.value("points").toArray()) {
    const QJsonArray xy = pt.toArray();
    const int l = labelAt(p, xy[0].toDouble(), xy[1].toDouble());
    QVERIFY2(l >= 0, "a selection point is not a block label");
    labels.insert(l);
  }
  const QJsonObject values = entry.value("integrals").toObject();

  MagneticsIntegrals::BlockIntegrator integrator(s, p, labels);
  QStringList mismatches;
  for (int k = 0; k <= 24; k++) {
    const QJsonArray want = values.value(QString::number(k)).toArray();
    const Complex expect(want[0].toDouble(), want[1].toDouble());
    Complex got;
    QString why;
    if (!integrator.integral(k, got, why)) {
      if (k >= 18 && k <= 23 && why.contains("invalid"))
        continue; // classic's number here is the previous mask's -- see the header
      mismatches << QStringLiteral("%1 (%2): refused -- %3").arg(k).arg(integralName(k), why);
      continue;
    }
    // Relative to the larger of the two, with a floor at a millionth of
    // the integral's own scale so a quantity that should be ~0 (a net
    // force on a symmetric conductor) is judged against round-off.
    const double scale = std::max({ std::abs(expect), std::abs(got) });
    const double tol = 1e-6 * scale + 1e-30;
    const double diff = std::abs(got - expect);
    // Components that classic leaves at round-off level (|z| tiny next
    // to the selection's own energy scale) are compared loosely: both
    // sides are summing noise.
    if (diff > tol && diff > 1e-9 * std::max(1e-30, std::abs(expect)) + 1e-18) {
      mismatches << QStringLiteral("%1 (%2): femmqt %3%4%5j, classic %6%7%8j")
                        .arg(k)
                        .arg(integralName(k))
                        .arg(got.real(), 0, 'g', 10)
                        .arg(got.imag() >= 0 ? "+" : "")
                        .arg(got.imag(), 0, 'g', 6)
                        .arg(expect.real(), 0, 'g', 10)
                        .arg(expect.imag() >= 0 ? "+" : "")
                        .arg(expect.imag(), 0, 'g', 6);
    }
  }
  QVERIFY2(mismatches.isEmpty(), qPrintable("\n  " + mismatches.join("\n  ")));
}

void TestMagneticsIntegrals::aStressTensorSelectionTouchingIronIsRefused()
{
  // The coax's dielectric touches both conductors. Classic would hand
  // back the previous selection's force; femmqt says why it will not.
  FemmProblem p;
  MeshSolution s;
  QString err;
  QVERIFY2(AnsFileIO::readAns(fixture("coax_cable.ans"), p, s, err), qPrintable(err));
  MagneticsIntegrals::BlockIntegrator integrator(s, p, { labelAt(p, 3, 0) });
  Complex z;
  QString why;
  QVERIFY(!integrator.integral(18, z, why));
  QVERIFY2(why.contains("cannot abut"), qPrintable(why));
  // The other integrals of the same selection are unaffected.
  QVERIFY(integrator.integral(2, z, why));
  QVERIFY(z.real() > 0);
}

void TestMagneticsIntegrals::thePolynomialIntegralsAreExactForLinearFields()
{
  // PlnInt of u = 1 against v = 1 is the area; of a linear field against
  // 1 is the area times its mean. AxiInt of 1 against 1 is the revolved
  // volume 2*pi*Rbar*a (exact for a triangle: Pappus).
  const Complex one[3] = { 1., 1., 1. };
  const Complex lin[3] = { 1., 2., 6. };
  QCOMPARE(MagneticsIntegrals::plnInt(2.0, one, one).real(), 2.0);
  QCOMPARE(MagneticsIntegrals::plnInt(2.0, one, lin).real(), 2.0 * 3.0);
  const double r[3] = { 1., 2., 3. };
  QVERIFY(std::abs(MagneticsIntegrals::axiInt(0.5, one, one, r).real() - 2. * M_PI * 2. * 0.5) < 1e-12);
}

QTEST_GUILESS_MAIN(TestMagneticsIntegrals)
#include "tst_magnetics_integrals.moc"
