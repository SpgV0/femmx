// tst_icons.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-12.
//
// Every toolbar button rendered as bare text -- no icons at all -- and
// nothing anywhere said why. That is the defect this file is really
// about: IconTheme::themedToolIcon opens its SVG with QFile and returns
// a default-constructed QIcon() if the open fails, and Qt's own
// behaviour for an empty icon is to draw the text and reserve no icon
// space. A missing resource and a deliberately text-only button are
// indistinguishable on screen, so 53 broken buttons produced zero
// diagnostics.
//
// The checks here are the ones that would have caught it before a user
// did:
//
//   * the resource is actually linked in (the whole failure), and
//   * every ":/icons/..." path the UI asks for exists in icons.qrc, and
//     every alias in icons.qrc has a real file behind it.
//
// The last two run as pure text scans, so they hold even where Qt
// resources cannot be initialised at all.

#include <QtTest>

#include "IconTheme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QRegularExpression>
#include <QToolBar>
#include "MainWindow.h"
#include "AppTheme.h"
#include <QSet>

namespace {

QString repoRoot()
{
  // Set by CMake; tests/ is two levels below the repo root.
  return QFileInfo(QStringLiteral(FEMMQT_SOURCE_DIR)).absoluteFilePath();
}

QStringList qrcAliases(QString& prefixOut)
{
  QStringList aliases;
  QFile f(repoRoot() + "/icons.qrc");
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return aliases;
  const QString text = QString::fromUtf8(f.readAll());

  const QRegularExpression prefixRe(QStringLiteral("<qresource\\s+prefix=\"([^\"]+)\""));
  const QRegularExpressionMatch pm = prefixRe.match(text);
  prefixOut = pm.hasMatch() ? pm.captured(1) : QString();

  const QRegularExpression fileRe(
      QStringLiteral("<file\\s+alias=\"([^\"]+)\"\\s*>([^<]+)</file>"));
  QRegularExpressionMatchIterator it = fileRe.globalMatch(text);
  while (it.hasNext())
    aliases << it.next().captured(1);
  return aliases;
}

QStringList qrcSourceFiles()
{
  QStringList files;
  QFile f(repoRoot() + "/icons.qrc");
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return files;
  const QString text = QString::fromUtf8(f.readAll());
  const QRegularExpression fileRe(
      QStringLiteral("<file\\s+alias=\"[^\"]+\"\\s*>([^<]+)</file>"));
  QRegularExpressionMatchIterator it = fileRe.globalMatch(text);
  while (it.hasNext())
    files << it.next().captured(1);
  return files;
}

} // namespace

class TestIcons : public QObject
{
  Q_OBJECT

private slots:
  void everyQrcEntryHasAFileOnDisk();
  void everyIconTheUiAsksForIsInTheQrc();
  void theIconResourceIsLinkedIn();
  void aThemedIconIsNotBlank();
  void everyThemedIconIsRegisteredForRethemeing();
  void everyThemedIconIsRegisteredForRethemeing_data();
  void everyToolbarIconFollowsALiveThemeSwitch();
};

// ---------------------------------------------------------------------------
// Text-only checks: these hold with or without a working resource system
// ---------------------------------------------------------------------------

void TestIcons::everyQrcEntryHasAFileOnDisk()
{
  const QStringList sources = qrcSourceFiles();
  QVERIFY2(!sources.isEmpty(), "icons.qrc listed no files at all");

  QStringList missing;
  for (const QString& rel : sources) {
    if (!QFile::exists(repoRoot() + "/" + rel.trimmed()))
      missing << rel.trimmed();
  }
  QVERIFY2(missing.isEmpty(),
      qPrintable(QStringLiteral("icons.qrc references files that do not "
                                "exist, so they cannot be compiled in: %1")
                     .arg(missing.join(", "))));
}

void TestIcons::everyIconTheUiAsksForIsInTheQrc()
{
  // MainWindow asks for icons by literal path. A typo or a renamed file
  // gives that one button no icon, silently -- the same failure as the
  // whole resource going missing, just smaller.
  QFile mw(repoRoot() + "/MainWindow.cpp");
  QVERIFY2(mw.open(QIODevice::ReadOnly | QIODevice::Text),
      "could not read MainWindow.cpp");
  const QString text = QString::fromUtf8(mw.readAll());

  QString prefix;
  const QStringList aliases = qrcAliases(prefix);
  QVERIFY2(!aliases.isEmpty(), "icons.qrc declared no aliases");
  QCOMPARE(prefix, QStringLiteral("/icons"));

  const QSet<QString> known(aliases.begin(), aliases.end());

  QSet<QString> asked;
  const QRegularExpression re(QStringLiteral("\":/icons/([^\"]+)\""));
  QRegularExpressionMatchIterator it = re.globalMatch(text);
  while (it.hasNext())
    asked.insert(it.next().captured(1));

  QVERIFY2(!asked.isEmpty(), "MainWindow.cpp asks for no icons at all");

  QStringList unknown;
  for (const QString& a : asked) {
    if (!known.contains(a))
      unknown << a;
  }
  QVERIFY2(unknown.isEmpty(),
      qPrintable(QStringLiteral("the UI asks for icons that icons.qrc does "
                                "not provide, so those buttons render as "
                                "bare text: %1").arg(unknown.join(", "))));
}

// ---------------------------------------------------------------------------
// The failure itself
// ---------------------------------------------------------------------------

void TestIcons::theIconResourceIsLinkedIn()
{
  // The actual bug. icons.qrc lived in the femmqt_core STATIC library,
  // and a static library's resource initialiser is only pulled in if
  // something the linker keeps references it -- otherwise the object
  // file is discarded and the resource silently does not exist at
  // runtime. Nothing in the UI notices, because an empty QIcon draws as
  // plain text.
  QStringList prefixOut;
  QString prefix;
  const QStringList aliases = qrcAliases(prefix);
  QVERIFY(!aliases.isEmpty());

  QStringList absent;
  for (const QString& alias : aliases) {
    if (!QFile::exists(":/icons/" + alias))
      absent << alias;
  }

  QVERIFY2(absent.isEmpty(),
      qPrintable(QStringLiteral("%1 of %2 icons are not present as Qt "
                                "resources at runtime -- the .qrc is not "
                                "linked in. Every toolbar button will "
                                "render as bare text. First few: %3")
                     .arg(absent.size()).arg(aliases.size())
                     .arg(QStringList(absent.mid(0, 5)).join(", "))));
}

void TestIcons::aThemedIconIsNotBlank()
{
  // Loading is not enough: themedToolIcon substitutes the palette colour
  // into the SVG and rasterises it. An icon that loads but renders empty
  // looks exactly like a missing one.
  const QIcon icon = IconTheme::themedToolIcon(":/icons/select.svg");
  QVERIFY2(!icon.isNull(),
      "themedToolIcon returned a null icon for :/icons/select.svg");

  const QPixmap pm = icon.pixmap(24, 24);
  QVERIFY2(!pm.isNull(), "the rendered pixmap is null");
  QCOMPARE(pm.size().isEmpty(), false);

  // At least one pixel must have been drawn. A fully transparent pixmap
  // is what a failed SVG parse produces, and it is invisible on screen
  // in exactly the same way a missing icon is.
  const QImage img = pm.toImage();
  bool anyInk = false;
  for (int y = 0; y < img.height() && !anyInk; y++) {
    for (int x = 0; x < img.width(); x++) {
      if (qAlpha(img.pixel(x, y)) != 0) {
        anyInk = true;
        break;
      }
    }
  }
  QVERIFY2(anyInk,
      "the icon rasterised to a fully transparent image -- it loaded but "
      "drew nothing, which on screen is indistinguishable from missing");
}

// QTEST_MAIN, not GUILESS: rasterising an SVG into a QPixmap needs a
// QGuiApplication and a paint device, so this runs under the offscreen
// QPA plugin the CMake harness deploys.
// ---------------------------------------------------------------------------
// Re-theming (issue #92)
// ---------------------------------------------------------------------------

void TestIcons::everyThemedIconIsRegisteredForRethemeing_data()
{
  QTest::addColumn<QString>("file");
  QTest::newRow("MainWindow.cpp") << "MainWindow.cpp";
  QTest::newRow("SolutionView.cpp") << "SolutionView.cpp";
}

void TestIcons::everyThemedIconIsRegisteredForRethemeing()
{
  QFETCH(QString, file);

  // IconTheme::themedToolIcon() bakes in whichever palette is current
  // when it is called, and a QAction never re-queries its icon. So an
  // action created with addAction(themedToolIcon(...)) shows the right
  // artwork exactly until the user picks View > Dark Theme, and then
  // shows the wrong artwork forever.
  //
  // That is not hypothetical and it is not loud: seven actions -- Trim,
  // Extend, Split and the four dimension tools -- sat as a grey run in
  // the middle of a white toolbar after a live switch, and were noticed
  // from a screenshot rather than from anything failing. A session that
  // STARTS dark is fine, because main() sets the theme before any
  // window exists, so the path people actually look at hides it.
  //
  // The rule that prevents it: every themed icon goes in through a
  // helper that also records it for refreshToolbarIcons(). A raw
  // addAction(themedToolIcon(...)) is what drift looks like, so the
  // helpers are the only place it may appear.
  QFile f(QStringLiteral(FEMMQT_SOURCE_DIR) + "/" + file);
  QVERIFY2(f.open(QIODevice::ReadOnly | QIODevice::Text),
      qPrintable(file + " did not read"));
  const QString code = QString::fromUtf8(f.readAll());

  const QRegularExpression raw(
      QStringLiteral("addAction\\s*\\(\\s*IconTheme::themedToolIcon\\s*\\(([^)]*)\\)"));

  QStringList offenders;
  QRegularExpressionMatchIterator it = raw.globalMatch(code);
  while (it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    // The helpers themselves pass the path through as a variable and
    // register it on the next line; every other caller names a literal
    // ":/icons/....svg", which is the shape that skips registration.
    const QString argument = m.captured(1).trimmed();
    if (!argument.startsWith('"'))
      continue;
    const int line = code.left(m.capturedStart()).count('\n') + 1;
    offenders << QStringLiteral("%1:%2 (%3)").arg(file).arg(line).arg(argument);
  }

  QVERIFY2(offenders.isEmpty(),
      qPrintable(QStringLiteral("a toolbar icon is set directly instead of "
                                "through a helper that records it, so it will "
                                "keep its old artwork when the theme is "
                                "switched at runtime: %1")
                     .arg(offenders.join(", "))));

  // And the hand-kept list that caused the drift must not come back:
  // refreshToolbarIcons is one loop over one list, not a loop plus a
  // column of setIcon calls that somebody has to remember to extend.
  const int at = code.indexOf(QStringLiteral("::refreshToolbarIcons"));
  if (at < 0)
    return; // SolutionView has one; a file without one has nothing to check
  int end = code.indexOf(QStringLiteral("\n}"), at);
  QVERIFY(end > at);
  const QString body = code.mid(at, end - at);
  QVERIFY2(!body.contains(QStringLiteral("themedToolIcon(\":/icons/")),
      qPrintable(QStringLiteral("%1's refreshToolbarIcons names icons by hand. "
                                "That list drifted from the actions that "
                                "actually exist once already (#92) -- register "
                                "them instead.").arg(file)));
}

void TestIcons::everyToolbarIconFollowsALiveThemeSwitch()
{
  // The source-level guard above says every icon is registered. This
  // says the registration actually reaches the screen, by doing what
  // the user did: open the window and pick View > Dark Theme.
  //
  // Worth having as well as the text scan, because the two fail for
  // different reasons -- a helper that records the action but forgets
  // to re-set its icon would pass the scan and fail here.
  const bool wasDark = AppTheme::isDark();
  const QString cfg = QCoreApplication::applicationDirPath() + "/femm.cfg";
  const bool hadCfg = QFile::exists(cfg);
  QByteArray savedCfg;
  if (hadCfg) {
    QFile f(cfg);
    if (f.open(QIODevice::ReadOnly))
      savedCfg = f.readAll();
  }

  {
    MainWindow window;
    // Every toolbar, not the first: the window has a horizontal tool
    // strip and a vertical one down the left, and checking one of them
    // would miss whichever half the next mistake lands in.
    const QList<QToolBar*> bars = window.findChildren<QToolBar*>();
    QVERIFY2(!bars.isEmpty(), "the main window has no toolbar");

    QVector<QAction*> icons;
    for (QToolBar* bar : bars) {
      for (QAction* a : bar->actions()) {
        if (!a->isSeparator() && !a->icon().isNull())
          icons << a;
      }
    }
    QVERIFY2(icons.size() > 20,
        qPrintable(QStringLiteral("only %1 toolbar icons found -- this case "
                                  "would pass by checking almost nothing")
                       .arg(icons.size())));

    QVector<qint64> before;
    for (QAction* a : icons)
      before << a->icon().cacheKey();

    // The user's route in, found the way a user finds it.
    QAction* darkTheme = nullptr;
    for (QAction* a : window.findChildren<QAction*>()) {
      if (a->text().contains(QStringLiteral("Dark Theme")))
        darkTheme = a;
    }
    QVERIFY2(darkTheme, "there is no Dark Theme action to trigger");
    darkTheme->setChecked(!darkTheme->isChecked()); // fires onDarkThemeToggled

    QStringList unchanged;
    for (int i = 0; i < icons.size(); i++) {
      if (icons[i]->icon().cacheKey() == before[i])
        unchanged << icons[i]->text();
    }
    QVERIFY2(unchanged.isEmpty(),
        qPrintable(QStringLiteral("%1 toolbar icon(s) kept their old artwork "
                                  "through a theme switch, so they now sit at "
                                  "the wrong contrast against the rest: %2")
                       .arg(unchanged.size()).arg(unchanged.join(", "))));
  }

  AppTheme::setDark(wasDark);
  if (hadCfg) {
    QFile f(cfg);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
      f.write(savedCfg);
  } else {
    QFile::remove(cfg);
  }
}

QTEST_MAIN(TestIcons)
#include "tst_icons.moc"
