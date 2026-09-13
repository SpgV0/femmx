// tst_headless_cli.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #85).
//
// femmqt's command-line paths must FAIL, not HANG.
//
// The defect this guards was a modal QMessageBox on a batch code path.
// It is worth being precise about why that shape is worse than an
// ordinary bug. A hang is not a crash, so no exit code reports it. It
// is not slow, so nothing distinguishes it from a large model except a
// clock. And under CI it does not cost one test its budget, it costs
// the job all of it. Measured before the fix: `--render-png` on a file
// extension femmqt did not recognise sat there until killed.
//
// Which forces an unusual shape on these tests. Every assertion here is
// TIME-BOUNDED, because the failure being guarded against is precisely
// the absence of a result -- an untimed "assert it exits non-zero" on a
// process that never exits does not fail, it hangs, and a hanging test
// is indistinguishable from a slow one. So each case runs the real
// femmqt.exe as a child with a hard cap and treats the cap itself as
// the failure.
//
// The positive case matters as much as the negative ones: if
// femmqt.exe were simply broken, every "exits non-zero" assertion here
// would pass for the wrong reason. So one case renders a real file and
// requires exit 0 and a PNG on disk.

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

namespace {

// Generous, because a real render of a real model happens in here on a
// build machine of unknown speed -- but finite, because the whole point
// is that the cap is reachable.
const int kCapMs = 60000;

QString femmqtExe()
{
  const QString path = QCoreApplication::applicationDirPath() + "/femmqt.exe";
  return QFileInfo::exists(path) ? path : QString();
}

QString repoRoot()
{
  return QFileInfo(QFileInfo(QStringLiteral(FEMMQT_SOURCE_DIR)).absoluteFilePath())
      .absolutePath();
}

struct Run {
  bool finished = false; // false means it hit the cap: the defect itself
  int exitCode = -1;
  QString stderrText;
  qint64 elapsedMs = 0;
};

Run runFemmqt(const QStringList& args)
{
  Run r;
  QProcess p;
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  // The CI runners have no desktop, which is the situation this whole
  // file is about. Forcing it here means the test reproduces the
  // headless case on a developer machine that does have one.
  env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
  p.setProcessEnvironment(env);

  QElapsedTimer timer;
  timer.start();
  p.start(femmqtExe(), args);
  if (!p.waitForStarted(15000))
    return r;
  r.finished = p.waitForFinished(kCapMs);
  r.elapsedMs = timer.elapsed();
  r.stderrText = QString::fromLocal8Bit(p.readAllStandardError());
  if (!r.finished) {
    p.kill();
    p.waitForFinished(5000);
    return r;
  }
  r.exitCode = p.exitCode();
  return r;
}

QString readAll(const QString& path)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return QString();
  return QString::fromUtf8(f.readAll());
}

} // namespace

class TestHeadlessCli : public QObject
{
  Q_OBJECT

  private slots:
  void initTestCase();

  void aGoodRenderStillSucceeds();

  void anUnrecognisedExtensionFailsInsteadOfHanging();
  void anUnrecognisedExtensionFailsInsteadOfHanging_data();

  void everyCommandLinePathTerminatesOnBadInput();
  void everyCommandLinePathTerminatesOnBadInput_data();

  void aCorruptSolutionFailsInsteadOfHanging();

  void theWindowsNeverCallQMessageBoxDirectly();
  void mainMarksEveryCommandLinePathHeadless();

  private:
  QTemporaryDir m_tmp;
};

void TestHeadlessCli::initTestCase()
{
  QVERIFY2(!femmqtExe().isEmpty(),
      "femmqt.exe is not beside the test binary -- these cases drive the real "
      "executable, and without it they would all pass vacuously");
  QVERIFY(m_tmp.isValid());
}

// ---------------------------------------------------------------------------
// The anti-vacuity case
// ---------------------------------------------------------------------------

void TestHeadlessCli::aGoodRenderStillSucceeds()
{
  // Without this, a femmqt.exe that failed on everything would make
  // every other case in this file pass.
  const QString model = repoRoot() + "/manual_qt/images/example.feh";
  QVERIFY2(QFile::exists(model), qPrintable(model + " is missing"));

  const QString png = m_tmp.filePath("good.png");
  const Run r = runFemmqt({ "--render-png", model, png, "300", "220" });

  QVERIFY2(r.finished, "a valid render hit the time cap");
  QVERIFY2(r.exitCode == 0,
      qPrintable(QStringLiteral("rendering a valid model exited %1: %2")
                     .arg(r.exitCode).arg(r.stderrText)));
  QVERIFY2(QFileInfo(png).size() > 0, "exit 0 but no PNG was written");
}

// ---------------------------------------------------------------------------
// The defect
// ---------------------------------------------------------------------------

void TestHeadlessCli::anUnrecognisedExtensionFailsInsteadOfHanging_data()
{
  QTest::addColumn<QString>("fileName");
  QTest::addColumn<QString>("expectInMessage");

  QTest::newRow("unknown suffix") << "bogus.xyz" << "xyz";
  QTest::newRow("no suffix at all") << "bogus" << "(none)";
  // .fes is the sketch sidecar and .ansx/.femx are caches -- all real
  // extensions in this repo, none of them something to render alone.
  QTest::newRow("sketch sidecar") << "bogus.fes" << "fes";
  QTest::newRow("a solver log") << "bogus.log" << "log";
}

void TestHeadlessCli::anUnrecognisedExtensionFailsInsteadOfHanging()
{
  QFETCH(QString, fileName);
  QFETCH(QString, expectInMessage);

  const QString in = m_tmp.filePath(fileName);
  QFile f(in);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("not a FEMM file of any kind\n");
  f.close();

  const QString png = m_tmp.filePath("out.png");
  const Run r = runFemmqt({ "--render-png", in, png, "200", "150" });

  QVERIFY2(r.finished,
      qPrintable(QStringLiteral("--render-png on \"%1\" did not terminate within "
                                "%2 ms -- this is the modal-dialog hang, not a "
                                "slow render")
                     .arg(fileName).arg(kCapMs)));
  QVERIFY2(r.exitCode != 0,
      qPrintable(QStringLiteral("--render-png on \"%1\" exited 0. A batch caller "
                                "cannot tell that apart from a successful render.")
                     .arg(fileName)));

  // The message has to name the file and the extension: the usual cause
  // is a typo or a solver that wrote its output elsewhere, and neither
  // is diagnosable from "failed".
  QVERIFY2(r.stderrText.contains(QFileInfo(in).fileName()),
      qPrintable("stderr did not name the file: " + r.stderrText));
  QVERIFY2(r.stderrText.contains(expectInMessage),
      qPrintable(QStringLiteral("stderr did not name the extension \"%1\": %2")
                     .arg(expectInMessage, r.stderrText)));
}

void TestHeadlessCli::everyCommandLinePathTerminatesOnBadInput_data()
{
  QTest::addColumn<QStringList>("args");

  // Every batch entry point, each given input it must reject. The point
  // is not the exit code -- it is that none of them stops to ask.
  QTest::newRow("--render-png missing") << QStringList{ "--render-png", "nope.fem", "o.png" };
  QTest::newRow("--convert-ansx missing") << QStringList{ "--convert-ansx", "nope.ans" };
  QTest::newRow("--import-dxf missing") << QStringList{ "--import-dxf", "nope.dxf", "o.fem" };
  QTest::newRow("--probe-ans missing") << QStringList{ "--probe-ans", "nope.ans", "o.csv" };
}

void TestHeadlessCli::everyCommandLinePathTerminatesOnBadInput()
{
  QFETCH(QStringList, args);

  QStringList full = args;
  // Route any output file into the temp dir so a pass leaves nothing
  // behind and a partial write cannot collide between rows.
  for (QString& a : full) {
    if (a.endsWith(".png") || a.endsWith(".csv")
        || (a.endsWith(".fem") && a.startsWith("o.")))
      a = m_tmp.filePath(a);
  }

  const Run r = runFemmqt(full);
  QVERIFY2(r.finished,
      qPrintable(QStringLiteral("%1 did not terminate within %2 ms")
                     .arg(full.join(' ')).arg(kCapMs)));
  QVERIFY2(r.exitCode != 0,
      qPrintable(QStringLiteral("%1 accepted input that does not exist")
                     .arg(full.join(' '))));
}

void TestHeadlessCli::aCorruptSolutionFailsInsteadOfHanging()
{
  // A recognised solution extension holding something that is not one.
  // This reaches deeper than the extension check -- into the reader,
  // which reports through the same dialog the extension check used to.
  const QString in = m_tmp.filePath("truncated.anh");
  QFile f(in);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write("[Format] = 4.0\n[Frequency] = 0\n");
  f.close();

  const QString png = m_tmp.filePath("corrupt.png");
  const Run r = runFemmqt({ "--render-png", in, png, "200", "150" });

  QVERIFY2(r.finished, "a corrupt .anh did not terminate -- the reader's failure "
                       "report is still a modal dialog");
  QVERIFY2(r.exitCode != 0, "a corrupt .anh rendered successfully");
  QVERIFY2(!r.stderrText.isEmpty(), "failed silently: nothing on stderr");
}

// ---------------------------------------------------------------------------
// The durable half
// ---------------------------------------------------------------------------
//
// The cases above catch today's paths. They cannot catch the next
// QMessageBox added to a function that a batch path happens to reach --
// and that is how this defect arrived in the first place. So the rule
// is enforced at the source level instead.

void TestHeadlessCli::theWindowsNeverCallQMessageBoxDirectly()
{
  // Both windows are reachable from --render-png, so a modal dialog
  // anywhere in them can hang a batch run. Notify::warning/information
  // are drop-in replacements that become a stderr line when there is
  // nobody to click OK.
  //
  // about() is exempt: it is a menu item, unreachable without a user
  // already driving the GUI.
  const QStringList files = { "MainWindow.cpp", "SolutionView.cpp" };
  const QRegularExpression direct(
      QStringLiteral("QMessageBox::(warning|information|question)\\s*\\("));

  QStringList offenders;
  for (const QString& name : files) {
    const QString path = QStringLiteral(FEMMQT_SOURCE_DIR) + "/" + name;
    QVERIFY2(QFile::exists(path), qPrintable(name + " is gone -- update this list"));
    QString code = readAll(path);
    QVERIFY2(!code.isEmpty(), qPrintable(name + " read as empty"));
    code.remove(QRegularExpression(QStringLiteral("//[^\\n]*")));

    QRegularExpressionMatchIterator it = direct.globalMatch(code);
    while (it.hasNext()) {
      const int at = it.next().capturedStart();
      offenders << QStringLiteral("%1:%2").arg(name).arg(code.left(at).count('\n') + 1);
    }
  }

  QVERIFY2(offenders.isEmpty(),
      qPrintable(QStringLiteral("a modal QMessageBox is called directly at %1. "
                                "Both windows are reachable from --render-png, "
                                "where a modal dialog is a hang -- use "
                                "Notify::warning/information/question instead "
                                "(see Notify.h).")
                     .arg(offenders.join(", "))));
}

void TestHeadlessCli::mainMarksEveryCommandLinePathHeadless()
{
  // The flag is set once, keyed on argv[1] starting with "--", so an
  // option added later is headless without anyone remembering to say
  // so. If that ever becomes a per-flag list, this fails and says why.
  const QString code = readAll(QStringLiteral(FEMMQT_SOURCE_DIR) + "/main.cpp");
  QVERIFY2(!code.isEmpty(), "main.cpp read as empty");

  QVERIFY2(code.contains(QStringLiteral("Notify::setHeadless(true)")),
      "main() no longer marks the command-line paths headless, so every batch "
      "entry point can stop on a modal dialog again");

  const int flagAt = code.indexOf(QStringLiteral("Notify::setHeadless(true)"));
  const int firstCli = code.indexOf(QStringLiteral("\"--convert-ansx\""));
  QVERIFY2(firstCli > 0 && flagAt < firstCli,
      "setHeadless() runs after a command-line path has already been taken -- it "
      "has to be decided before any of them can report anything");
}

QTEST_GUILESS_MAIN(TestHeadlessCli)
#include "tst_headless_cli.moc"
