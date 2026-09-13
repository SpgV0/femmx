// tst_gui_switch.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-11.
//
// femm.cfg has three writers now -- the classic GUI's Preferences dialog
// (femm/GeneralPrefs.cpp), GuiSwitch's <PreferredGUI> and
// AppPreferences' <QtDarkTheme> -- and each knows only its own keys.
// The rule that makes that safe is that every writer must preserve the
// lines it does not recognize. Two of the three already did; the classic
// GUI's did not, and would silently reset which GUI the user had chosen
// every time they opened Preferences and pressed OK (issue #19).
//
// This file pins the Qt half of that rule. The classic half lives in
// unelevated MFC dialog code that cannot be driven headlessly, so its
// counterpart is the femm.cfg seeding check in
// test/fork_scripting_test.py.
//
// Both units read and write QCoreApplication::applicationDirPath() +
// "/femm.cfg", i.e. next to THIS binary -- so each test saves whatever
// is there and puts it back, rather than leaving a stray config beside
// the test executable.

#include <QtTest>

#include "AppPreferences.h"
#include "FemmFileIO.h"
#include "FemmProblem.h"
#include "AnsFileIO.h"
#include "MeshSolution.h"
#include "FileRouting.h"
#include "SolutionFileIO.h"
#include "GuiSwitch.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

class TestGuiSwitch : public QObject
{
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void writePreferredGuiKeepsForeignKeys();
  void writePreferredGuiReplacesRatherThanAppends();
  void readPreferredGuiRoundTrips();
  void readPreferredGuiDefaultsToTheClassicGuiWhenTheKeyIsAbsent();
  void savingQtPreferencesKeepsThePreferredGui();
  void settingThePreferredGuiKeepsTheQtPreferences();

  void solutionFilesRouteToTheSolutionViewer();
  void solutionFilesRouteToTheSolutionViewer_data();
  void aHandedOverClassicFileSurvivesTheRoundTrip();

  // Issue #88.
  void everyProblemTypeCanReachTheQtGui();
  void theHandoffReadsSolutionsWithTheSharedReader();
  void theMagneticsReaderCannotStandInForTheOthers();

private:
  static QString cfgPath()
  {
    return QCoreApplication::applicationDirPath() + "/femm.cfg";
  }
  static void writeCfg(const QString& text)
  {
    QFile f(cfgPath());
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
    QTextStream out(&f);
    out << text;
  }
  static QString readCfg()
  {
    QFile f(cfgPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
      return QString();
    return QTextStream(&f).readAll();
  }
  static QStringList cfgLines()
  {
    QStringList out;
    const QStringList raw = readCfg().split('\n');
    for (const QString& line : raw) {
      if (!line.trimmed().isEmpty())
        out << line.trimmed();
    }
    return out;
  }
  static int countLinesStartingWith(const QString& tag)
  {
    int n = 0;
    for (const QString& line : cfgLines()) {
      if (line.startsWith(tag, Qt::CaseInsensitive))
        n++;
    }
    return n;
  }

  bool m_hadCfg = false;
  QString m_saved;
};

void TestGuiSwitch::init()
{
  m_hadCfg = QFile::exists(cfgPath());
  m_saved = m_hadCfg ? readCfg() : QString();
}

void TestGuiSwitch::cleanup()
{
  if (m_hadCfg)
    writeCfg(m_saved);
  else
    QFile::remove(cfgPath());
}

// The classic GUI's five keys, exactly as CGeneralPrefs::WritePrefs emits
// them, plus a key nobody recognizes at all.
static const char* kClassicCfg =
    "<ShowConsole>      = 1\n"
    "<SeparatePlots>    = 0\n"
    "<ShowOutputWindow> = 1\n"
    "<SmartMesh>        = 1\n"
    "<DefaultType>      = 2\n"
    "<SomeFutureKey>    = 7\n";

void TestGuiSwitch::writePreferredGuiKeepsForeignKeys()
{
  writeCfg(QString::fromLatin1(kClassicCfg));

  QVERIFY(GuiSwitch::writePreferredGui(GuiSwitch::PreferredGui::Qt));

  const QStringList lines = cfgLines();
  // Every original key must survive, including one this build has never
  // heard of -- a future key, or one written by a newer version.
  for (const char* tag : { "<ShowConsole>", "<SeparatePlots>",
           "<ShowOutputWindow>", "<SmartMesh>", "<DefaultType>",
           "<SomeFutureKey>" }) {
    QVERIFY2(countLinesStartingWith(QString::fromLatin1(tag)) == 1,
        qPrintable(QStringLiteral("writePreferredGui lost or duplicated %1; "
                                  "femm.cfg is now:\n%2")
                       .arg(QString::fromLatin1(tag), readCfg())));
  }
  QCOMPARE(countLinesStartingWith(QStringLiteral("<PreferredGUI>")), 1);
  QCOMPARE(lines.size(), 7);

  // and the value the classic GUI would read back is unchanged
  QVERIFY(lines.contains(QStringLiteral("<DefaultType>      = 2")));
  QVERIFY(lines.contains(QStringLiteral("<SomeFutureKey>    = 7")));
}

void TestGuiSwitch::writePreferredGuiReplacesRatherThanAppends()
{
  writeCfg(QStringLiteral("<PreferredGUI>    = 0\n<ShowConsole>      = 1\n"));

  QVERIFY(GuiSwitch::writePreferredGui(GuiSwitch::PreferredGui::Qt));
  QVERIFY(GuiSwitch::writePreferredGui(GuiSwitch::PreferredGui::Classic));
  QVERIFY(GuiSwitch::writePreferredGui(GuiSwitch::PreferredGui::Qt));

  // Repeated switching must not grow the file: an appended duplicate would
  // leave two <PreferredGUI> lines and the first one read would win, so
  // the setting would appear to stop taking effect.
  QCOMPARE(countLinesStartingWith(QStringLiteral("<PreferredGUI>")), 1);
  QCOMPARE(cfgLines().size(), 2);
  QCOMPARE((int)GuiSwitch::readPreferredGui(), (int)GuiSwitch::PreferredGui::Qt);
}

void TestGuiSwitch::readPreferredGuiRoundTrips()
{
  writeCfg(QString::fromLatin1(kClassicCfg));

  QVERIFY(GuiSwitch::writePreferredGui(GuiSwitch::PreferredGui::Classic));
  QCOMPARE((int)GuiSwitch::readPreferredGui(),
      (int)GuiSwitch::PreferredGui::Classic);

  QVERIFY(GuiSwitch::writePreferredGui(GuiSwitch::PreferredGui::Qt));
  QCOMPARE((int)GuiSwitch::readPreferredGui(),
      (int)GuiSwitch::PreferredGui::Qt);
}

void TestGuiSwitch::readPreferredGuiDefaultsToTheClassicGuiWhenTheKeyIsAbsent()
{
  // A femm.cfg predating the key, or a fresh install. The answer has to
  // match femm/ScriptGui.cpp's ReadPreferredGuiFromCfg(), which reads the
  // same key for the same purpose and defaults to Classic -- and to match
  // script.nsi, whose FEMMX.lnk launches femmx.exe. Two readers of one key
  // that disagree about what "absent" means is a bug waiting for its first
  // caller; until issue #19 these two did.
  writeCfg(QStringLiteral("<ShowConsole>      = 1\n"));
  QCOMPARE((int)GuiSwitch::readPreferredGui(),
      (int)GuiSwitch::PreferredGui::Classic);

  QFile::remove(cfgPath());
  QCOMPARE((int)GuiSwitch::readPreferredGui(),
      (int)GuiSwitch::PreferredGui::Classic);

  // an explicit 0 and an explicit 1 still mean exactly what they say
  writeCfg(QStringLiteral("<PreferredGUI>    = 0\n"));
  QCOMPARE((int)GuiSwitch::readPreferredGui(),
      (int)GuiSwitch::PreferredGui::Classic);
  writeCfg(QStringLiteral("<PreferredGUI>    = 1\n"));
  QCOMPARE((int)GuiSwitch::readPreferredGui(), (int)GuiSwitch::PreferredGui::Qt);
}

void TestGuiSwitch::savingQtPreferencesKeepsThePreferredGui()
{
  writeCfg(QString::fromLatin1(kClassicCfg));
  QVERIFY(GuiSwitch::writePreferredGui(GuiSwitch::PreferredGui::Qt));

  AppPreferences prefs = AppPreferences::load();
  prefs.darkTheme = true;
  prefs.smartMesh = false;
  QVERIFY(prefs.save());

  QCOMPARE((int)GuiSwitch::readPreferredGui(), (int)GuiSwitch::PreferredGui::Qt);
  QCOMPARE(countLinesStartingWith(QStringLiteral("<SomeFutureKey>")), 1);

  AppPreferences back = AppPreferences::load();
  QCOMPARE(back.darkTheme, true);
  QCOMPARE(back.smartMesh, false);
  // the classic GUI's own value it never touched
  QCOMPARE(back.defaultDocType, 2);
}

void TestGuiSwitch::settingThePreferredGuiKeepsTheQtPreferences()
{
  writeCfg(QString::fromLatin1(kClassicCfg));

  AppPreferences prefs = AppPreferences::load();
  prefs.darkTheme = true;
  QVERIFY(prefs.save());

  QVERIFY(GuiSwitch::writePreferredGui(GuiSwitch::PreferredGui::Classic));

  // the reverse direction of the same rule
  QCOMPARE(AppPreferences::load().darkTheme, true);
  QCOMPARE((int)GuiSwitch::readPreferredGui(),
      (int)GuiSwitch::PreferredGui::Classic);
}

void TestGuiSwitch::solutionFilesRouteToTheSolutionViewer_data()
{
  QTest::addColumn<QString>("path");
  QTest::addColumn<bool>("solution");

  QTest::newRow("ans") << "model.ans" << true;
  QTest::newRow("ansx") << "model.ansx" << true;
  // The classic GUI hands off whatever was open, and Windows file
  // associations are case-insensitive, so an .ANS from an older tool or a
  // shell drag-and-drop has to route the same way.
  QTest::newRow("ANS uppercase") << "MODEL.ANS" << true;
  QTest::newRow("AnsX mixed") << "Model.AnsX" << true;
  QTest::newRow("with a path") << "C:/some dir/model.ans" << true;

  QTest::newRow("fem") << "model.fem" << false;
  QTest::newRow("femx") << "model.femx" << false;
  QTest::newRow("dxf") << "model.dxf" << false;
  QTest::newRow("no suffix") << "model" << false;
  QTest::newRow("empty") << QString() << false;
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
  // (issue #83). These three were false until the viewer could read
  // them: routing a .anh to the editor instead is not a clean failure,
  // because a solution file IS its input file with a section appended,
  // so it opens as a perfectly valid UNSOLVED heat-flow model with the
  // solution silently dropped.
  QTest::newRow("res") << "model.res" << true;
  QTest::newRow("anh") << "model.anh" << true;
  QTest::newRow("anc") << "model.anc" << true;
  QTest::newRow("ANH uppercase") << "MODEL.ANH" << true;
  // A near-miss that must not match a startsWith-style check
  QTest::newRow("answers") << "model.answers" << false;
}

void TestGuiSwitch::solutionFilesRouteToTheSolutionViewer()
{
  QFETCH(QString, path);
  QFETCH(bool, solution);
  QCOMPARE(FileRouting::isSolutionFile(path), solution);
}

void TestGuiSwitch::aHandedOverClassicFileSurvivesTheRoundTrip()
{
  // The handoff is only useful if it is lossless in both directions. This
  // reads a .fem the CLASSIC GUI wrote -- not one femmqt wrote itself,
  // which is what tst_femmqt_core.cpp's round trip already covers -- and
  // writes it back, then reads the result. Anything femmqt failed to
  // understand would quietly disappear at the moment the user switched
  // GUIs, which is the worst possible moment for it.
  // FEMMQT_TEST_FIXTURE_DIR is set by CMake rather than using
  // QFINDTESTDATA, which searches the build tree and the app directory
  // and would not find a fixture that lives under test/ in the source
  // tree two levels up.
  const QString fixture = QStringLiteral(FEMMQT_TEST_FIXTURE_DIR)
      + QStringLiteral("/render/render_fixture.fem");
  if (!QFile::exists(fixture))
    QSKIP(qPrintable(QStringLiteral("no fixture at ") + fixture));

  FemmProblem original;
  QString error;
  QVERIFY2(FemmFileIO::readFem(fixture, original, error), qPrintable(error));

  QVERIFY(!original.nodes.isEmpty());
  QVERIFY(!original.segments.isEmpty());
  QVERIFY(!original.arcSegments.isEmpty());
  QVERIFY(!original.blockLabels.isEmpty());

  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString out = dir.filePath("handed_back.fem");
  QVERIFY2(FemmFileIO::writeFem(out, original, error), qPrintable(error));

  FemmProblem back;
  QVERIFY2(FemmFileIO::readFem(out, back, error), qPrintable(error));

  QCOMPARE(back.nodes.size(), original.nodes.size());
  QCOMPARE(back.segments.size(), original.segments.size());
  QCOMPARE(back.arcSegments.size(), original.arcSegments.size());
  QCOMPARE(back.blockLabels.size(), original.blockLabels.size());
  QCOMPARE(back.materialProps.size(), original.materialProps.size());
  QCOMPARE(back.boundaryProps.size(), original.boundaryProps.size());
  QCOMPARE(back.circuitProps.size(), original.circuitProps.size());
  QCOMPARE(back.pointProps.size(), original.pointProps.size());

  QCOMPARE((int)back.problemType, (int)original.problemType);
  QCOMPARE((int)back.lengthUnits, (int)original.lengthUnits);
  QCOMPARE(back.depth, original.depth);
  QCOMPARE(back.frequency, original.frequency);

  for (int i = 0; i < original.nodes.size(); i++) {
    QCOMPARE(back.nodes[i].x, original.nodes[i].x);
    QCOMPARE(back.nodes[i].y, original.nodes[i].y);
  }
  for (int i = 0; i < original.arcSegments.size(); i++) {
    QCOMPARE(back.arcSegments[i].n0, original.arcSegments[i].n0);
    QCOMPARE(back.arcSegments[i].n1, original.arcSegments[i].n1);
    QCOMPARE(back.arcSegments[i].arcLength, original.arcSegments[i].arcLength);
  }
  for (int i = 0; i < original.blockLabels.size(); i++) {
    // 1-based into the material table, with -1 meaning a hole -- a
    // rebasing bug here would silently reassign every material in the
    // model, so it is compared exactly rather than just counted.
    QCOMPARE(back.blockLabels[i].blockTypeIndex,
        original.blockLabels[i].blockTypeIndex);
    QCOMPARE(back.blockLabels[i].circuitIndex,
        original.blockLabels[i].circuitIndex);
    QCOMPARE(back.blockLabels[i].turns, original.blockLabels[i].turns);
    QCOMPARE(back.blockLabels[i].magDir, original.blockLabels[i].magDir);
  }
  for (int i = 0; i < original.materialProps.size(); i++) {
    QCOMPARE(back.materialProps[i].name, original.materialProps[i].name);
    QCOMPARE(back.materialProps[i].muX, original.materialProps[i].muX);
    QCOMPARE(back.materialProps[i].sigma, original.materialProps[i].sigma);
    QCOMPARE(back.materialProps[i].bhData.size(),
        original.materialProps[i].bhData.size());
  }
  for (int i = 0; i < original.boundaryProps.size(); i++) {
    QCOMPARE(back.boundaryProps[i].name, original.boundaryProps[i].name);
    QCOMPARE(back.boundaryProps[i].bdryFormat,
        original.boundaryProps[i].bdryFormat);
  }
  for (int i = 0; i < original.circuitProps.size(); i++) {
    QCOMPARE(back.circuitProps[i].name, original.circuitProps[i].name);
    QCOMPARE(back.circuitProps[i].ampsRe, original.circuitProps[i].ampsRe);
    QCOMPARE(back.circuitProps[i].circType,
        original.circuitProps[i].circType);
  }
}

// ---------------------------------------------------------------------------
// Reaching the Qt GUI at all (issue #88)
// ---------------------------------------------------------------------------

namespace {

QString repoRoot()
{
  return QFileInfo(QFileInfo(QStringLiteral(FEMMQT_SOURCE_DIR)).absoluteFilePath())
      .absolutePath();
}

QString slurp(const QString& path)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return QString();
  return QString::fromUtf8(f.readAll());
}

} // namespace

void TestGuiSwitch::everyProblemTypeCanReachTheQtGui()
{
  // "Switch to Qt GUI..." lived in the magnetics editor and the
  // magnetics post-processor only. The other six windows had no way
  // across at all -- not a degraded one, none -- while femmqt had grown
  // an editor and a viewer for all four physics.
  //
  // A menu item with no handler is a greyed-out entry, and a handler
  // with no menu item is dead code; neither fails loudly, so both
  // halves are checked.
  const QString rc = slurp(repoRoot() + "/femm/femm.rc");
  QVERIFY2(!rc.isEmpty(), "femm/femm.rc did not read");

  const QStringList menus = { "IDR_FEMMETYPE", "IDR_FEMMVIEWTYPE",
    "IDR_BELADRAWTYPE", "IDR_BELAVIEWTYPE", "IDR_HDRAWTYPE", "IDR_HVIEWTYPE",
    "IDR_CDRAWTYPE", "IDR_CVIEWTYPE" };

  QStringList missingItem;
  for (const QString& menu : menus) {
    const int start = rc.indexOf(menu + QStringLiteral(" MENU"));
    QVERIFY2(start > 0, qPrintable(menu + " is gone from femm.rc"));
    // The block ends where the next menu begins, or at the end.
    int end = rc.size();
    for (const QString& other : menus) {
      if (other == menu)
        continue;
      const int at = rc.indexOf(other + QStringLiteral(" MENU"));
      if (at > start && at < end)
        end = at;
    }
    if (!rc.mid(start, end - start).contains(QStringLiteral("ID_VIEW_SWITCHTOQT")))
      missingItem << menu;
  }
  QVERIFY2(missingItem.isEmpty(),
      qPrintable(QStringLiteral("no \"Switch to Qt GUI...\" item in %1 -- that "
                                "problem type cannot reach the Qt GUI at all")
                     .arg(missingItem.join(", "))));

  const QStringList views = { "FemmeView.cpp", "FemmviewView.cpp",
    "beladrawView.cpp", "belaviewView.cpp", "hdrawView.cpp", "hviewView.cpp",
    "cdrawView.cpp", "cviewView.cpp" };

  QStringList missingHandler;
  for (const QString& name : views) {
    const QString code = slurp(repoRoot() + "/femm/" + name);
    QVERIFY2(!code.isEmpty(), qPrintable(name + " did not read"));
    if (!code.contains(QStringLiteral("ON_COMMAND(ID_VIEW_SWITCHTOQT")))
      missingHandler << name;
  }
  QVERIFY2(missingHandler.isEmpty(),
      qPrintable(QStringLiteral("%1 has no ID_VIEW_SWITCHTOQT handler, so its "
                                "menu item would be greyed out")
                     .arg(missingHandler.join(", "))));

  // And the fifty lines of femm.cfg rewriting plus CreateProcess must
  // exist once, not eight times. Two copies is how the repo has been
  // bitten before; eight would be worse.
  int spawners = 0;
  for (const QString& name : views) {
    if (slurp(repoRoot() + "/femm/" + name).contains(QStringLiteral("femmqt.exe\\\" \\\"%s")))
      spawners++;
  }
  QVERIFY2(spawners == 0,
      qPrintable(QStringLiteral("%1 view(s) still build the femmqt.exe command "
                                "line themselves instead of calling "
                                "HandOffToQtGui").arg(spawners)));
}

void TestGuiSwitch::theHandoffReadsSolutionsWithTheSharedReader()
{
  // The handoff branch in main() routed with isSolutionFile() -- which
  // has matched all four solution formats since #83 -- and then called
  // openAnsFile(), the MAGNETICS-only path. Nothing could reach it
  // while only magnetics had a "Switch to Qt GUI" item, which is
  // exactly what this issue adds.
  const QString main = slurp(QStringLiteral(FEMMQT_SOURCE_DIR) + "/main.cpp");
  QVERIFY2(!main.isEmpty(), "femmqt/main.cpp did not read");

  const int at = main.indexOf(QStringLiteral("FileRouting::isSolutionFile(args.at(1))"));
  QVERIFY2(at > 0, "the command-line handoff no longer routes by extension");

  const QString tail = main.mid(at);
  QVERIFY2(tail.contains(QStringLiteral("openSolutionFile(args.at(1))")),
      "the handoff opens a solution with openAnsFile, the magnetics-only path. "
      "A .anh handed over from the classic post-processor would be parsed as a "
      ".ans");
}

void TestGuiSwitch::theMagneticsReaderCannotStandInForTheOthers()
{
  // Why the line above matters, stated as behaviour rather than as a
  // claim about which function is called: the magnetics reader really
  // cannot read the other three formats, so routing a .anh to it is a
  // failure and not a slower path to the same answer.
  const QString anh = repoRoot() + "/manual_qt/images/example.anh";
  QVERIFY2(QFile::exists(anh), qPrintable(anh + " is missing"));

  FemmProblem problem;
  MeshSolution magnetics;
  QString error;
  QVERIFY2(!AnsFileIO::readAns(anh, problem, magnetics, error),
      "the magnetics .ans reader accepted a heat-flow solution");
  QVERIFY2(!error.isEmpty(), "it failed without saying why");

  FemmProblem shared;
  SolvedMesh mesh;
  QString sharedError;
  QVERIFY2(SolutionFileIO::read(anh, shared, mesh, sharedError),
      qPrintable("the shared reader could not read it either: " + sharedError));
  QVERIFY2(!mesh.elements.isEmpty(), "the shared reader returned an empty mesh");
}

// GUI-free: both units are plain file IO over QCoreApplication's own
// directory, so no QPA platform plugin is needed. See the note at the
// bottom of tst_femmqt_core.cpp for why that matters here.
QTEST_GUILESS_MAIN(TestGuiSwitch)
#include "tst_gui_switch.moc"
