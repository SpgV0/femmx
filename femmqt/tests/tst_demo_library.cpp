// tst_demo_library.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #91).
//
// "Can be opened and solved, but never modified in its installed
// location."
//
// That reads like a file-permission requirement and is not one. FEMM
// installs to C:\femm42, which a normal user can write, so the OS is
// protecting nothing. And femmqt writes three different things next to
// whatever model it has open: the .femx cache, the .fes sketch sidecar,
// and everything the solve pipeline produces -- triangle.exe's
// .poly/.node/.ele plus the solver's .ans/.anh/.res/.anc -- because
// SolveRunner sets its working directory to the model's own folder.
//
// This was not hypothetical. Rendering the seven magnetics demos once,
// while the corpus was being built, left seven .femx files in demos/.
// They are gitignored, so nothing said a word.
//
// So the assertion here is the requirement itself, stated the only way
// that cannot be satisfied by accident: HASH THE WHOLE TREE, do the
// things that write, hash it again. A guard against one of the three
// paths would pass while the other two leaked; a hash of the tree does
// not care which path did it, or whether someone adds a fourth.

#include <QtTest>

#include "DemoLibrary.h"
#include "FemmProblem.h"
#include "FemxFileIO.h"
#include "ProblemFileIO.h"
#include "ProblemKind.h"
#include "SketchFileIO.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

namespace {

// Every file under `root`, by relative path and content. Names as well
// as bytes: a stray .femx is a new NAME, and a hash of concatenated
// contents alone could in principle miss one.
QString treeFingerprint(const QString& root)
{
  QStringList entries;
  QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
  const QDir base(root);
  while (it.hasNext()) {
    const QString path = it.next();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
      return QStringLiteral("UNREADABLE:") + path;
    const QByteArray digest =
        QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256);
    entries << base.relativeFilePath(path) + " " + QString::fromLatin1(digest.toHex());
  }
  entries.sort(); // directory order is not guaranteed
  return entries.join('\n');
}

} // namespace

class TestDemoLibrary : public QObject
{
  Q_OBJECT

  private slots:
  void initTestCase();

  void theManifestDescribesEveryShippedModel();
  void everyDemoOpens();
  void everyDemoOpens_data();

  void workingWithEveryDemoLeavesTheLibraryByteIdentical();
  void aWorkingCopyIsWritableAndOutsideTheLibrary();
  void twoOpensOfTheSameDemoDoNotShareADirectory();
  void theInstallerShipsEveryProblemTypeTheManifestLists();

  private:
  QString m_root;
  QVector<DemoLibrary::Demo> m_demos;
};

void TestDemoLibrary::initTestCase()
{
  m_root = DemoLibrary::directory();
  QVERIFY2(!m_root.isEmpty(),
      "no demos directory was found from the test binary -- every case here "
      "would otherwise pass by having nothing to check");

  QString error;
  m_demos = DemoLibrary::load(error);
  QVERIFY2(!m_demos.isEmpty(), qPrintable("no demos loaded: " + error));
}

// ---------------------------------------------------------------------------
// The corpus
// ---------------------------------------------------------------------------

void TestDemoLibrary::theManifestDescribesEveryShippedModel()
{
  // A model file with no manifest entry is invisible in the browser; a
  // manifest entry with no file is a row that cannot be opened. Both
  // fail quietly, so both are checked.
  QStringList onDisk;
  QDirIterator it(m_root, { "*.fem", "*.fee", "*.feh", "*.fec" }, QDir::Files,
      QDirIterator::Subdirectories);
  const QDir base(m_root);
  while (it.hasNext())
    onDisk << base.relativeFilePath(it.next());
  onDisk.sort();

  QStringList listed;
  for (const DemoLibrary::Demo& d : m_demos)
    listed << d.file;
  listed.sort();

  QCOMPARE(listed, onDisk);

  for (const DemoLibrary::Demo& d : m_demos) {
    // A demo nobody can interpret is just a file -- the description is
    // what makes the library a library.
    QVERIFY2(!d.description.isEmpty(),
        qPrintable(QStringLiteral("\"%1\" has no description").arg(d.file)));
    QVERIFY2(!d.problemType.isEmpty(),
        qPrintable(QStringLiteral("\"%1\" has no problem type").arg(d.file)));
    // The manifest's problem type must agree with the extension, or the
    // browser would group a model under the wrong physics.
    FemmProblemKind kind = FemmProblemKind::Magnetics;
    QVERIFY2(ProblemKind::kindForPath(d.file, kind),
        qPrintable(QStringLiteral("\"%1\" is not a model extension").arg(d.file)));
    const QString declared = d.problemType.toLower().replace('_', ' ');
    QCOMPARE(declared, ProblemKind::displayName(kind).toLower());
  }
}

void TestDemoLibrary::everyDemoOpens_data()
{
  QTest::addColumn<QString>("file");
  QString error;
  for (const DemoLibrary::Demo& d : DemoLibrary::load(error))
    QTest::newRow(qPrintable(d.file)) << d.file;
}

void TestDemoLibrary::everyDemoOpens()
{
  QFETCH(QString, file);
  const QString path = QDir(m_root).absoluteFilePath(file);

  FemmProblemKind kind = FemmProblemKind::Magnetics;
  QVERIFY(ProblemKind::kindForPath(path, kind));

  FemmProblem problem;
  QString error;
  QVERIFY2(ProblemFileIO::readAs(path, kind, problem, error),
      qPrintable(QStringLiteral("%1 did not read: %2").arg(file, error)));

  // A demo that opens as an empty sketch is a broken demo, and would
  // let every other assertion here pass over nothing.
  QVERIFY2(!problem.nodes.isEmpty(),
      qPrintable(QStringLiteral("%1 has no geometry").arg(file)));
  QVERIFY2(!problem.blockLabels.isEmpty(),
      qPrintable(QStringLiteral("%1 has no block labels, so it cannot be solved")
                     .arg(file)));
}

// ---------------------------------------------------------------------------
// The guarantee
// ---------------------------------------------------------------------------

void TestDemoLibrary::workingWithEveryDemoLeavesTheLibraryByteIdentical()
{
  const QString before = treeFingerprint(m_root);
  QVERIFY2(!before.isEmpty(), "the demos directory appears to be empty");

  QStringList inside;

  for (const DemoLibrary::Demo& d : m_demos) {
    QString error;
    const QString copy = DemoLibrary::makeWorkingCopy(d.absolutePath(), error);
    QVERIFY2(!copy.isEmpty(), qPrintable(error));

    FemmProblemKind kind = FemmProblemKind::Magnetics;
    QVERIFY(ProblemKind::kindForPath(copy, kind));

    FemmProblem problem;
    QVERIFY2(ProblemFileIO::readAs(copy, kind, problem, error), qPrintable(error));

    // The three writers, run deliberately rather than hoped about. The
    // solve pipeline is not run here -- the solver binaries are not
    // beside a test binary -- but it derives its working directory from
    // the open file exactly as these do, and the copy is asserted below
    // to be outside the library.
    QVERIFY2(ProblemFileIO::write(copy, problem, error), qPrintable(error));
    if (kind == FemmProblemKind::Magnetics) {
      const QFileInfo fi(copy);
      const QString femx = fi.absolutePath() + "/" + fi.completeBaseName() + ".femx";
      QString femxError;
      FemxFileIO::writeFemx(femx, copy, problem, femxError);
    }
    QString sketchError;
    SketchFileIO::writeSketch(copy, problem, sketchError);

    // Each of those must have landed beside the COPY. Collected rather
    // than asserted here: an immediate QVERIFY2 would abort the loop on
    // the first offender and the tree comparison below -- the assertion
    // that actually states the requirement -- would never run at all.
    const QDir copyDir(QFileInfo(copy).absolutePath());
    if (copyDir.absolutePath().startsWith(QDir(m_root).absolutePath()))
      inside << QStringLiteral("%1 -> %2").arg(d.file, copy);
  }

  const QString after = treeFingerprint(m_root);
  if (before != after) {
    // Say WHICH files appeared or changed -- "the tree differs" over a
    // 13-model library is not a diagnosis.
    // Named locals, not two calls to split(): begin() and end() taken
    // from two different temporaries is undefined behaviour, and it
    // segfaulted here -- which only showed up the first time this
    // branch actually ran, i.e. the first time the test had a real
    // failure to report. A diagnostic that crashes instead of
    // explaining is worse than no diagnostic.
    const QStringList beforeLines = before.split('\n');
    const QStringList afterLines = after.split('\n');
    const QSet<QString> b(beforeLines.begin(), beforeLines.end());
    QStringList added;
    for (const QString& line : afterLines) {
      if (!b.contains(line))
        added << line.section(' ', 0, 0);
    }
    QFAIL(qPrintable(QStringLiteral("opening and saving the demos changed the "
                                    "installed library. Appeared or changed: %1")
                         .arg(added.join(", "))));
  }

  QVERIFY2(inside.isEmpty(),
      qPrintable(QStringLiteral("working copies were placed inside the demos "
                                "directory: %1").arg(inside.join("; "))));
}

void TestDemoLibrary::aWorkingCopyIsWritableAndOutsideTheLibrary()
{
  QVERIFY(!m_demos.isEmpty());
  const DemoLibrary::Demo& d = m_demos.first();

  QString error;
  const QString copy = DemoLibrary::makeWorkingCopy(d.absolutePath(), error);
  QVERIFY2(!copy.isEmpty(), qPrintable(error));

  QVERIFY2(!QDir(QFileInfo(copy).absolutePath())
                .absolutePath()
                .startsWith(QDir(m_root).absolutePath()),
      "a working copy was placed inside the demos directory");

  // A copy that inherited a read-only attribute would defeat the whole
  // arrangement: the user would be told to save elsewhere by an error
  // rather than by a prompt.
  QFile f(copy);
  QVERIFY2(f.open(QIODevice::Append), qPrintable("the working copy is not writable: "
                                                 + f.errorString()));
  f.close();

  // And it is the same model, not an empty file.
  QVERIFY2(QFileInfo(copy).size() == QFileInfo(d.absolutePath()).size(),
      "the working copy is not the same size as the demo");
}

void TestDemoLibrary::twoOpensOfTheSameDemoDoNotShareADirectory()
{
  // The solve pipeline writes by base name, so two copies of one demo
  // in one directory would have the second solve overwrite the first's
  // mesh and solution -- silently, and only when a user happens to open
  // the same demo twice.
  QVERIFY(!m_demos.isEmpty());
  const QString demo = m_demos.first().absolutePath();

  QString e1, e2;
  const QString a = DemoLibrary::makeWorkingCopy(demo, e1);
  const QString b = DemoLibrary::makeWorkingCopy(demo, e2);
  QVERIFY2(!a.isEmpty(), qPrintable(e1));
  QVERIFY2(!b.isEmpty(), qPrintable(e2));
  QVERIFY2(QFileInfo(a).absolutePath() != QFileInfo(b).absolutePath(),
      "two opens of the same demo shared a working directory");
}

void TestDemoLibrary::theInstallerShipsEveryProblemTypeTheManifestLists()
{
  // An installed copy with no demos directory has nothing to list, and
  // the browser would be permanently disabled for reasons no one could
  // see from the code.
  const QString repo = QFileInfo(QFileInfo(QStringLiteral(FEMMQT_SOURCE_DIR))
                                     .absoluteFilePath())
                           .absolutePath();
  QFile f(repo + "/script.nsi");
  QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text), "script.nsi did not read");
  const QString nsi = QString::fromUtf8(f.readAll());

  QVERIFY2(nsi.contains(QStringLiteral("$INSTDIR\\demos")),
      "the installer ships no demos directory");
  QVERIFY2(nsi.contains(QStringLiteral("demos\\demos.json")),
      "the installer ships no manifest, so the browser would find nothing");
  QVERIFY2(nsi.contains(QStringLiteral("RMDir /r \"$INSTDIR\\demos\"")),
      "the uninstaller leaves the demos directory behind");

  QSet<QString> groups;
  for (const DemoLibrary::Demo& d : m_demos)
    groups << d.file.section('/', 0, 0);
  QVERIFY(!groups.isEmpty());

  QStringList missing;
  for (const QString& g : groups) {
    if (!nsi.contains(QStringLiteral("$INSTDIR\\demos\\%1").arg(g)))
      missing << g;
  }
  QVERIFY2(missing.isEmpty(),
      qPrintable(QStringLiteral("the installer does not ship demos/%1 -- those "
                                "demos would be listed by the manifest and "
                                "absent from disk").arg(missing.join(", "))));
}

QTEST_GUILESS_MAIN(TestDemoLibrary)
#include "tst_demo_library.moc"
