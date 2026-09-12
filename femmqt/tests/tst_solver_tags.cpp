// tst_solver_tags.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12.
//
// A tag the solver reads and acts on, that no writer ever emits, is a
// field the user cannot set and that a hand-authored file loses on its
// next save. <voltgradient_re>/<voltgradient_im> were exactly that: fkn
// has parsed them into CCircuit::dVolts since before the fork and drives
// a circuit at a prescribed voltage gradient with them, while both GUIs
// wrote a circuit as name, amps and type and nothing else.
//
// That was found by accident. The first test here is the systematic
// version of the accident -- it sweeps every tag every solver parses and
// fails on any that no writer emits, so the next one is found on purpose.
//
// The second is about a different hazard that this same fix ran into.
// .femx exists as two byte-for-byte independent implementations, one per
// GUI, and adding a field to a record means adding it to both. Getting
// that wrong is not a build error: it is one reader confidently parsing
// the other's bytes at the wrong offsets. This is the #77 shape again --
// a fix that reaches one copy of duplicated code and not the rest -- and
// it gets a guard rather than a comment asking people to be careful.

#include <QtTest>

#include "FemmFileIO.h"
#include "FemmProblem.h"
#include "FemxFileIO.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>

namespace {

QString repoRoot()
{
  // FEMMQT_SOURCE_DIR is femmqt/; the repo is its parent.
  return QFileInfo(QStringLiteral(FEMMQT_SOURCE_DIR)).absolutePath();
}

QString readAll(const QString& path)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return QString();
  return QString::fromUtf8(f.readAll());
}

QStringList sourcesIn(const QString& dir)
{
  QStringList out;
  QDir d(repoRoot() + "/" + dir);
  for (const QString& name : d.entryList(QStringList() << "*.cpp", QDir::Files)) {
    // tmp_* are scratch copies kept beside the real files; they are not
    // built and must not count as evidence either way.
    if (name.startsWith("tmp_"))
      continue;
    out << d.filePath(name);
  }
  return out;
}

// Tags a solver parses: _strnicmp(q, "<tag>", n).
QSet<QString> tagsParsedIn(const QString& dir)
{
  QSet<QString> tags;
  const QRegularExpression re(
      QStringLiteral("_strnicmp\\s*\\(\\s*\\w+\\s*,\\s*\"<([a-z_0-9]+)>\""),
      QRegularExpression::CaseInsensitiveOption);
  for (const QString& path : sourcesIn(dir)) {
    QRegularExpressionMatchIterator it = re.globalMatch(readAll(path));
    while (it.hasNext())
      tags.insert(it.next().captured(1).toLower());
  }
  return tags;
}

// Tags a GUI actually WRITES, taken only from lines that are write
// statements -- fprintf(...) in the MFC editor, `out << ...` in the Qt
// one.
//
// Scanning for the tag anywhere in the file instead was the obvious
// thing and is wrong, because femm/FemmeDoc.cpp holds both the .fem
// parser and the .fem writer. Every tag it parses would then count as
// one it writes, and this test would pass for exactly the files it most
// needs to check. Verified by mutation: with the loose scan, deleting
// both VoltGradient writes still passed.
QSet<QString> tagsEmittedByTheGuis()
{
  QSet<QString> tags;
  const QRegularExpression tagRe(QStringLiteral("<([A-Za-z_0-9]+)>"));
  for (const QString& dir : { QStringLiteral("femm"), QStringLiteral("femmqt") }) {
    for (const QString& path : sourcesIn(dir)) {
      const QStringList lines = readAll(path).split('\n');
      for (const QString& line : lines) {
        if (!line.contains(QStringLiteral("fprintf")) && !line.contains(QStringLiteral("out <<")))
          continue;
        QRegularExpressionMatchIterator it = tagRe.globalMatch(line);
        while (it.hasNext())
          tags.insert(it.next().captured(1).toLower());
      }
    }
  }
  return tags;
}

} // namespace

class TestSolverTags : public QObject
{
  Q_OBJECT

  private slots:
  void everyTagASolverReadsIsWrittenBySomeGui();
  void everyTagASolverReadsIsWrittenBySomeGui_data();

  void theTwoFemxImplementationsAgreeOnTheirRecordLayouts();
  void theTwoFemxImplementationsAgreeOnTheFormatVersion();

  void aVoltageGradientSurvivesAFemRoundTrip();
  void aVoltageGradientSurvivesTheFemxCache();
  void aCircuitWithoutOneWritesExactlyWhatItUsedTo();
};

void TestSolverTags::everyTagASolverReadsIsWrittenBySomeGui_data()
{
  QTest::addColumn<QString>("solverDir");
  QTest::newRow("fkn (magnetics)") << QStringLiteral("fkn");
  QTest::newRow("belasolv (electrostatics)") << QStringLiteral("belasolv");
  QTest::newRow("hsolv (heat flow)") << QStringLiteral("hsolv");
  QTest::newRow("csolv (current flow)") << QStringLiteral("csolv");
}

void TestSolverTags::everyTagASolverReadsIsWrittenBySomeGui()
{
  QFETCH(QString, solverDir);

  const QSet<QString> parsed = tagsParsedIn(solverDir);
  QVERIFY2(!parsed.isEmpty(),
      qPrintable(QStringLiteral("found no parsed tags in %1 at all -- the scan is "
                                "broken, which would make this test pass for the "
                                "wrong reason").arg(solverDir)));

  const QSet<QString> emitted = tagsEmittedByTheGuis();
  QVERIFY2(emitted.size() > 50, "the writer scan found implausibly few tags");

  QStringList orphans;
  for (const QString& tag : parsed) {
    if (!emitted.contains(tag))
      orphans << tag;
  }
  orphans.sort();

  QVERIFY2(orphans.isEmpty(),
      qPrintable(QStringLiteral("%1 parses these tags and acts on them, but neither "
                                "GUI ever writes them -- so a user cannot set them "
                                "and a file that has them loses them on the next "
                                "save, silently: <%2>")
                     .arg(solverDir, orphans.join(QStringLiteral(">, <")))));
}

void TestSolverTags::theTwoFemxImplementationsAgreeOnTheirRecordLayouts()
{
  const QString a = readAll(repoRoot() + "/femmqt/FemxFileIO.cpp");
  const QString b = readAll(repoRoot() + "/femm/FemxFileIO.cpp");
  QVERIFY2(!a.isEmpty() && !b.isEmpty(), "could not read both copies of FemxFileIO.cpp");

  // Every "struct Femx...Record { ... };" body, compared verbatim.
  const QRegularExpression re(
      QStringLiteral("struct (Femx\\w+)\\s*\\{([^}]*)\\};"));

  auto collect = [&](const QString& text) {
    QMap<QString, QString> out;
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    while (it.hasNext()) {
      const QRegularExpressionMatch m = it.next();
      // Comments and whitespace differ freely between the two; the
      // FIELDS are what has to match, since they are what the bytes are.
      QString body = m.captured(2);
      body.remove(QRegularExpression(QStringLiteral("//[^\\n]*")));
      body.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
      out.insert(m.captured(1), body.trimmed());
    }
    return out;
  };

  const QMap<QString, QString> sa = collect(a);
  const QMap<QString, QString> sb = collect(b);
  QVERIFY2(sa.size() >= 5,
      qPrintable(QStringLiteral("only found %1 record structs in femmqt's copy -- the "
                                "scan is broken").arg(sa.size())));

  QStringList problems;
  for (auto it = sa.constBegin(); it != sa.constEnd(); ++it) {
    if (!sb.contains(it.key())) {
      problems << QStringLiteral("%1 exists only in femmqt's copy").arg(it.key());
      continue;
    }
    if (sb.value(it.key()) != it.value()) {
      problems << QStringLiteral("%1 differs:\n    femmqt: %2\n    femm  : %3")
                      .arg(it.key(), it.value(), sb.value(it.key()));
    }
  }
  for (auto it = sb.constBegin(); it != sb.constEnd(); ++it) {
    if (!sa.contains(it.key()))
      problems << QStringLiteral("%1 exists only in femm's copy").arg(it.key());
  }

  QVERIFY2(problems.isEmpty(),
      qPrintable(QStringLiteral("the two .femx implementations have drifted. This is "
                                "not a build error -- each compiles fine and then "
                                "reads the other's bytes at the wrong offsets:\n  %1")
                     .arg(problems.join(QStringLiteral("\n  ")))));
}

void TestSolverTags::theTwoFemxImplementationsAgreeOnTheFormatVersion()
{
  // A layout change without a version bump is worse than the drift
  // above: the file still validates and is silently misread.
  const QRegularExpression re(
      QStringLiteral("constexpr uint32_t kFemxVersion = (\\d+);"));

  const QRegularExpressionMatch ma = re.match(readAll(repoRoot() + "/femmqt/FemxFileIO.cpp"));
  const QRegularExpressionMatch mb = re.match(readAll(repoRoot() + "/femm/FemxFileIO.cpp"));
  QVERIFY2(ma.hasMatch() && mb.hasMatch(), "could not find kFemxVersion in both copies");
  QVERIFY2(ma.captured(1) == mb.captured(1),
      qPrintable(QStringLiteral("the two .femx implementations disagree on the format "
                                "version: femmqt says %1, femm says %2")
                     .arg(ma.captured(1), mb.captured(1))));
}

// ---------------------------------------------------------------------------
// The round trip itself
// ---------------------------------------------------------------------------

namespace {

FemmProblem modelWithAVoltageDrivenCircuit()
{
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  FemmCircuitProp c;
  c.name = "Primary";
  c.ampsRe = 1.5;
  c.ampsIm = -0.25;
  c.circType = 1;
  c.voltGradientRe = 12.5;
  c.voltGradientIm = -3.75;
  p.circuitProps.push_back(c);
  return p;
}

} // namespace

void TestSolverTags::aVoltageGradientSurvivesAFemRoundTrip()
{
  FemmProblem p = modelWithAVoltageDrivenCircuit();

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.path() + "/vg.fem";
  QString error;
  QVERIFY2(FemmFileIO::writeFem(path, p, error), qPrintable(error));

  FemmProblem back;
  QVERIFY2(FemmFileIO::readFem(path, back, error), qPrintable(error));
  QCOMPARE(back.circuitProps.size(), 1);
  QCOMPARE(back.circuitProps[0].voltGradientRe, 12.5);
  QCOMPARE(back.circuitProps[0].voltGradientIm, -3.75);
  // The fields either side of it must be unharmed -- an inserted tag in
  // the wrong place is a plausible way to break the ones around it.
  QCOMPARE(back.circuitProps[0].ampsRe, 1.5);
  QCOMPARE(back.circuitProps[0].ampsIm, -0.25);
  QCOMPARE(back.circuitProps[0].circType, 1);
  QCOMPARE(back.circuitProps[0].name, QStringLiteral("Primary"));
}

void TestSolverTags::aVoltageGradientSurvivesTheFemxCache()
{
  // .femx holds what re-parsing the .fem would give. Leaving the field
  // out of the cache would lose it again on every cache hit -- the same
  // silent loss, one level down.
  FemmProblem p = modelWithAVoltageDrivenCircuit();

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString femPath = dir.path() + "/vg.fem";
  const QString femxPath = dir.path() + "/vg.femx";
  QString error;
  QVERIFY(FemmFileIO::writeFem(femPath, p, error));
  QVERIFY2(FemxFileIO::writeFemx(femxPath, femPath, p, error), qPrintable(error));

  FemmProblem cached;
  QVERIFY2(FemxFileIO::readFemx(femxPath, cached, error), qPrintable(error));
  QCOMPARE(cached.circuitProps.size(), 1);
  QCOMPARE(cached.circuitProps[0].voltGradientRe, 12.5);
  QCOMPARE(cached.circuitProps[0].voltGradientIm, -3.75);

  // And the cache must agree with the text file, which is the contract
  // that makes a cache safe to use at all.
  FemmProblem parsed;
  QVERIFY(FemmFileIO::readFem(femPath, parsed, error));
  QCOMPARE(cached.circuitProps[0].voltGradientRe, parsed.circuitProps[0].voltGradientRe);
  QCOMPARE(cached.circuitProps[0].voltGradientIm, parsed.circuitProps[0].voltGradientIm);
}

void TestSolverTags::aCircuitWithoutOneWritesExactlyWhatItUsedTo()
{
  // The tags are written only when set. Every model in existence has
  // them at zero -- the solver path is unreachable -- so this keeps the
  // output of every existing model byte-identical, which is worth more
  // than the symmetry of always writing them.
  FemmProblem p;
  p.problemType = FemmCoordinateType::Planar;
  FemmCircuitProp c;
  c.name = "Plain";
  c.ampsRe = 2.0;
  p.circuitProps.push_back(c);

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString path = dir.path() + "/plain.fem";
  QString error;
  QVERIFY(FemmFileIO::writeFem(path, p, error));

  const QString text = readAll(path);
  QVERIFY2(!text.contains("VoltGradient", Qt::CaseInsensitive),
      "a circuit with no voltage gradient wrote the tags anyway, changing the "
      "output of every existing model for no reason");
}

QTEST_GUILESS_MAIN(TestSolverTags)
#include "tst_solver_tags.moc"
