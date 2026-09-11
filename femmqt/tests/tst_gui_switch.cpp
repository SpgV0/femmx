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
#include "FileRouting.h"
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
  // Not a solution: the other three physics have their own solution
  // extensions (.res/.anh/.anc) and femmqt has no viewer for them yet, so
  // they must not be silently opened as if they were magnetics.
  QTest::newRow("res") << "model.res" << false;
  QTest::newRow("anh") << "model.anh" << false;
  QTest::newRow("anc") << "model.anc" << false;
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

// GUI-free: both units are plain file IO over QCoreApplication's own
// directory, so no QPA platform plugin is needed. See the note at the
// bottom of tst_femmqt_core.cpp for why that matters here.
QTEST_GUILESS_MAIN(TestGuiSwitch)
#include "tst_gui_switch.moc"
