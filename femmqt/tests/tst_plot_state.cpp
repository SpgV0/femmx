// tst_plot_state.cpp
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #86).
//
// The post-processor's view state has to survive a process boundary.
//
// Lua runs only in the classic MFC app, so setgui("qt") + mo_savepng
// means "hand the solved file to femmqt.exe and let it draw". femmqt
// opens the file fresh, with its own defaults. Before #86 the shell-out
// passed a size and nothing else, so
//
//     mo_showdensityplot(1, 0, 2.0, 0, "bmag")
//     mo_savepng("out.png")
//
// produced femmqt's default CONTOUR plot: 59% of pixels different from
// what was asked for, and not one word of error. That is the defect
// shape this repo keeps producing -- a valid image of the wrong thing.
//
// So the assertions here are about IDENTITY, not just difference.
// "Density and contour renders differ" would also pass if both were
// wrong in different ways. A density plot fills its area with colour
// bands; a contour plot is mostly background with thin lines through
// it. That property tells the two apart without pinning a hash to a
// particular palette, antialiasing setting or Qt version.

#include <QtTest>

#include "PlotStateArgs.h"
#include "FemmProblem.h"
#include "SolutionField.h"

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

namespace {

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

bool runRender(const QStringList& args, QString* stderrOut = nullptr)
{
  QProcess p;
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
  env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
  p.setProcessEnvironment(env);
  p.start(femmqtExe(), args);
  if (!p.waitForStarted(15000))
    return false;
  if (!p.waitForFinished(kCapMs)) {
    p.kill();
    p.waitForFinished(5000);
    return false;
  }
  if (stderrOut)
    *stderrOut = QString::fromLocal8Bit(p.readAllStandardError());
  return p.exitCode() == 0;
}

struct ImageStats {
  bool ok = false;
  int pixels = 0;
  double backgroundFraction = 0; // share of the single most common colour
  double colouredFraction = 0;   // share of pixels that are not neutral grey
  int distinctColours = 0;
};

ImageStats measure(const QString& path)
{
  ImageStats st;
  QImage image(path);
  if (image.isNull())
    return st;
  image = image.convertToFormat(QImage::Format_RGB32);

  QHash<QRgb, int> counts;
  int coloured = 0;
  for (int y = 0; y < image.height(); y++) {
    const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for (int x = 0; x < image.width(); x++) {
      const QRgb c = row[x];
      counts[c]++;
      const int r = qRed(c), g = qGreen(c), b = qBlue(c);
      // "Neutral" with a tolerance, because antialiasing can nudge a
      // grey by a count or two.
      if (qAbs(r - g) > 12 || qAbs(g - b) > 12 || qAbs(r - b) > 12)
        coloured++;
    }
  }

  st.pixels = image.width() * image.height();
  st.distinctColours = counts.size();
  int mostCommon = 0;
  for (auto it = counts.constBegin(); it != counts.constEnd(); ++it)
    mostCommon = qMax(mostCommon, it.value());
  st.backgroundFraction = st.pixels ? (double)mostCommon / st.pixels : 0.0;
  st.colouredFraction = st.pixels ? (double)coloured / st.pixels : 0.0;
  st.ok = true;
  return st;
}

int differingPixels(const QString& a, const QString& b, QRect* bounds = nullptr)
{
  QImage ia(a), ib(b);
  if (ia.isNull() || ib.isNull() || ia.size() != ib.size())
    return -1;
  ia = ia.convertToFormat(QImage::Format_RGB32);
  ib = ib.convertToFormat(QImage::Format_RGB32);

  int n = 0;
  int minX = ia.width(), minY = ia.height(), maxX = -1, maxY = -1;
  for (int y = 0; y < ia.height(); y++) {
    const QRgb* ra = reinterpret_cast<const QRgb*>(ia.constScanLine(y));
    const QRgb* rb = reinterpret_cast<const QRgb*>(ib.constScanLine(y));
    for (int x = 0; x < ia.width(); x++) {
      if (ra[x] == rb[x])
        continue;
      n++;
      minX = qMin(minX, x);
      maxX = qMax(maxX, x);
      minY = qMin(minY, y);
      maxY = qMax(maxY, y);
    }
  }
  if (bounds && maxX >= 0)
    *bounds = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
  return n;
}

QString readAll(const QString& path)
{
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
    return QString();
  return QString::fromUtf8(f.readAll());
}

} // namespace

class TestPlotState : public QObject
{
  Q_OBJECT

  private slots:
  void initTestCase();

  // The parser, on its own.
  void everyQuantityNameTheScriptLanguageAcceptsIsUnderstood();
  void anUnknownOptionIsRefusedRatherThanIgnored();
  void anUnknownOptionIsRefusedRatherThanIgnored_data();
  void boundsAreNormalisedTheSameWayTheScriptCommandNormalisesThem();
  void anEmptyStateProducesNoArgumentsAndSurvivesARoundTrip();
  void theClassicShellOutCanSpellEveryQuantityTheParserAccepts();

  // The renders.
  void aDensityRenderIsADensityPlotAndAContourRenderIsAContourPlot();
  void theQuantityChangesWhatIsPlotted();
  void greyscaleActuallyRemovesTheColour();
  void theLegendReachesTheRenderedImage();
  void customBoundsChangeTheBanding();
  void aPlotOptionOnAModelIsRefused();

  // Issue #87.
  void everyPhysicsRendersTheModeItWasAskedFor();
  void everyPhysicsRendersTheModeItWasAskedFor_data();
  void aQuantityTheOtherPhysicsCannotDrawIsRefusedByName();
  void aQuantityTheOtherPhysicsCannotDrawIsRefusedByName_data();
  void noMagneticsOnlyRefusalIsLeftStanding();
  void theClassicSideAndTheRendererAgreeOnWhichQuantityIsDrawn();

  private:
  QString m_ans;
  QTemporaryDir m_tmp;

  // Big enough that the legend is a corner rather than a wall. It is a
  // fixed pixel size -- and wider under the offscreen platform than
  // under Windows', since the default font differs -- so on a 400x300
  // canvas it covers about half the image and any whole-image statistic
  // ends up measuring the legend instead of the plot.
  static const int kW = 800;
  static const int kH = 600;

  QString render(const QString& name, const QStringList& options);
};

void TestPlotState::initTestCase()
{
  QVERIFY2(!femmqtExe().isEmpty(),
      "femmqt.exe is not beside the test binary -- the render cases drive the "
      "real executable and would otherwise pass vacuously");
  QVERIFY(m_tmp.isValid());
  m_ans = repoRoot() + "/manual_qt/images/example.ans";
  QVERIFY2(QFile::exists(m_ans), qPrintable(m_ans + " is missing"));
}

QString TestPlotState::render(const QString& name, const QStringList& options)
{
  const QString out = m_tmp.filePath(name + ".png");
  QStringList args = { "--render-png", m_ans, out, QString::number(kW),
    QString::number(kH) };
  args += options;
  QString err;
  if (!runRender(args, &err)) {
    qWarning("render %s failed: %s", qPrintable(name), qPrintable(err));
    return QString();
  }
  return out;
}

// ---------------------------------------------------------------------------
// The parser
// ---------------------------------------------------------------------------

void TestPlotState::everyQuantityNameTheScriptLanguageAcceptsIsUnderstood()
{
  // The exact vocabulary femm/femmviewLua.cpp's lua_showdensity accepts.
  // A name it takes and this rejects is a script that renders the wrong
  // quantity -- or, now, fails outright, which is at least visible.
  const QVector<QPair<QString, int>> expected = {
    { "bmag", 0 }, { "mag", 0 },
    { "breal", 1 }, { "real", 1 },
    { "bimag", 2 }, { "imag", 2 },
    { "hmag", 3 }, { "hreal", 4 }, { "himag", 5 },
    { "jmag", 6 }, { "jreal", 7 }, { "jimag", 8 },
    { "logb", 9 },
  };
  for (const auto& e : expected) {
    QCOMPARE(PlotStateArgs::quantityFromName(e.first), e.second);
    // Case and surrounding space come from a Lua string the user typed.
    QCOMPARE(PlotStateArgs::quantityFromName(e.first.toUpper()), e.second);
    QCOMPARE(PlotStateArgs::quantityFromName("  " + e.first + " "), e.second);
  }

  QCOMPARE(PlotStateArgs::quantityFromName("bogus"), -1);
  QCOMPARE(PlotStateArgs::quantityFromName(""), -1);

  // The indices have to match MeshSolutionItem::DensityQuantity's order,
  // which is what applyPlotState casts them to. Ten of them, and the
  // canonical name must round-trip.
  for (int i = 0; i < 10; i++) {
    const QString name = PlotStateArgs::nameForQuantity(i);
    QVERIFY2(!name.isEmpty(), qPrintable(QStringLiteral("no name for %1").arg(i)));
    QCOMPARE(PlotStateArgs::quantityFromName(name), i);
  }
  QVERIFY(PlotStateArgs::nameForQuantity(10).isEmpty());
  QVERIFY(PlotStateArgs::nameForQuantity(-1).isEmpty());
}

void TestPlotState::anUnknownOptionIsRefusedRatherThanIgnored_data()
{
  QTest::addColumn<QStringList>("args");

  // A typo that fell through to defaults would be this ticket's defect
  // wearing a new hat: a render that ignores what it was asked for.
  QTest::newRow("typo") << QStringList{ "--greyscal", "1" };
  QTest::newRow("plausible but wrong") << QStringList{ "--colormap", "jet" };
  QTest::newRow("quantity missing value") << QStringList{ "--quantity" };
  QTest::newRow("quantity not a quantity") << QStringList{ "--quantity", "flux" };
  QTest::newRow("bounds missing one") << QStringList{ "--bounds", "1" };
  QTest::newRow("bounds not numbers") << QStringList{ "--bounds", "lo", "hi" };
  QTest::newRow("flag not 0 or 1") << QStringList{ "--legend", "yes please" };
  QTest::newRow("zero contours") << QStringList{ "--contours", "0" };
}

void TestPlotState::anUnknownOptionIsRefusedRatherThanIgnored()
{
  QFETCH(QStringList, args);

  PlotState state;
  QString error;
  QVERIFY2(!PlotStateArgs::parse(args, state, error),
      qPrintable(QStringLiteral("\"%1\" was accepted").arg(args.join(' '))));
  QVERIFY2(!error.isEmpty(), "refused without saying why");
}

void TestPlotState::boundsAreNormalisedTheSameWayTheScriptCommandNormalisesThem()
{
  // lua_showdensity swaps them if they arrive the wrong way round, so a
  // script that passes (upper, lower) gets a picture either way. If only
  // one GUI did that, the same script would produce two different
  // images depending on setgui().
  PlotState state;
  QString error;
  QVERIFY2(PlotStateArgs::parse({ "--bounds", "2", "0.5" }, state, error),
      qPrintable(error));
  QVERIFY(state.haveBounds);
  QCOMPARE(state.lower, 0.5);
  QCOMPARE(state.upper, 2.0);

  PlotState other;
  QVERIFY(PlotStateArgs::parse({ "--contour-bounds", "3", "-1" }, other, error));
  QCOMPARE(other.contourLower, -1.0);
  QCOMPARE(other.contourUpper, 3.0);
}

void TestPlotState::anEmptyStateProducesNoArgumentsAndSurvivesARoundTrip()
{
  PlotState empty;
  QVERIFY2(empty.isEmpty(), "a default state is not empty, so every render "
                            "would now override femmqt's own defaults");
  QVERIFY2(PlotStateArgs::toArguments(empty).isEmpty(),
      "an empty state emitted arguments -- mi_savepng, which has no plot "
      "state, would start changing the pictures it produces");

  // The positional size and crop arguments share the list and must be
  // ignored, not choked on.
  PlotState state;
  QString error;
  QVERIFY2(PlotStateArgs::parse({ "in.ans", "out.png", "600", "450", "0", "0",
                                    "1", "1" },
               state, error),
      qPrintable(error));
  QVERIFY(state.isEmpty());

  // Round trip: a field that gained a parser but no writer (or the
  // reverse) is exactly how a setting silently stops crossing.
  PlotState full;
  full.mode = PlotState::Mode::Density;
  full.quantity = 4;
  full.haveBounds = true;
  full.lower = -1.25;
  full.upper = 3.5;
  full.greyscale = 1;
  full.legend = 0;
  full.numContours = 13;
  full.haveContourBounds = true;
  full.contourLower = 0.5;
  full.contourUpper = 9.5;

  PlotState back;
  QVERIFY2(PlotStateArgs::parse(PlotStateArgs::toArguments(full), back, error),
      qPrintable(error));
  QCOMPARE((int)back.mode, (int)full.mode);
  QCOMPARE(back.quantity, full.quantity);
  QCOMPARE(back.haveBounds, full.haveBounds);
  QCOMPARE(back.lower, full.lower);
  QCOMPARE(back.upper, full.upper);
  QCOMPARE(back.greyscale, full.greyscale);
  QCOMPARE(back.legend, full.legend);
  QCOMPARE(back.numContours, full.numContours);
  QCOMPARE(back.haveContourBounds, full.haveContourBounds);
  QCOMPARE(back.contourLower, full.contourLower);
  QCOMPARE(back.contourUpper, full.contourUpper);
}

void TestPlotState::theClassicShellOutCanSpellEveryQuantityTheParserAccepts()
{
  // Cross-process parity, the same way #84 does it: both halves cannot
  // be driven against each other here (one is a modal MFC app), but a
  // quantity only crosses if the sender can name it and the receiver
  // knows the name. QtDensityQuantityName is where the classic side
  // turns its DensityPlot index back into a name.
  const QString code = readAll(repoRoot() + "/femm/ScriptGui.cpp");
  QVERIFY2(!code.isEmpty(), "femm/ScriptGui.cpp did not read");

  const int at = code.indexOf(QStringLiteral("QtDensityQuantityName"));
  QVERIFY2(at > 0, "QtDensityQuantityName is gone -- mo_savepng can no longer "
                   "say which quantity it wants");

  for (int i = 0; i < 10; i++) {
    const QString name = PlotStateArgs::nameForQuantity(i);
    QVERIFY2(code.contains(QStringLiteral("\"%1\"").arg(name)),
        qPrintable(QStringLiteral("femmqt accepts --quantity %1 but "
                                  "femm/ScriptGui.cpp never emits it, so that "
                                  "density plot cannot cross to the Qt render")
                       .arg(name)));
  }
}

// ---------------------------------------------------------------------------
// The renders
// ---------------------------------------------------------------------------

void TestPlotState::aDensityRenderIsADensityPlotAndAContourRenderIsAContourPlot()
{
  const QString density = render("density", { "--density", "--legend", "0" });
  const QString contour = render("contour", { "--contour", "--legend", "0" });
  QVERIFY(!density.isEmpty() && !contour.isEmpty());

  const ImageStats d = measure(density);
  const ImageStats c = measure(contour);
  QVERIFY(d.ok && c.ok);

  // "They differ" alone would also pass if both were wrong in different
  // ways. What actually identifies the two: a density plot FILLS its
  // area with colour bands, a contour plot leaves it mostly background
  // and draws thin lines across it.
  QVERIFY2(d.backgroundFraction < 0.35,
      qPrintable(QStringLiteral("the --density render is %1 background -- that "
                                "is a contour plot, which is exactly the defect "
                                "this guards")
                     .arg(d.backgroundFraction, 0, 'f', 3)));
  QVERIFY2(c.backgroundFraction > 0.55,
      qPrintable(QStringLiteral("the --contour render is only %1 background -- "
                                "that is a filled plot, not contour lines")
                     .arg(c.backgroundFraction, 0, 'f', 3)));

  const int differing = differingPixels(density, contour);
  QVERIFY2(differing > d.pixels / 4,
      qPrintable(QStringLiteral("only %1 of %2 pixels differ between the two "
                                "modes").arg(differing).arg(d.pixels)));
}

void TestPlotState::theQuantityChangesWhatIsPlotted()
{
  // |B| and |J| are independent fields, so their plots must differ.
  //
  // |H| deliberately is NOT asserted against |B|: in a linear region
  // H = B/(mu*mu0), a constant factor, and the banding is normalised to
  // the visible range -- so an identical picture there is correct, and
  // asserting otherwise would be asserting a bug.
  const QString b = render("q_bmag", { "--density", "--legend", "0", "--quantity", "bmag" });
  const QString j = render("q_jmag", { "--density", "--legend", "0", "--quantity", "jmag" });
  QVERIFY(!b.isEmpty() && !j.isEmpty());

  const int differing = differingPixels(b, j);
  QVERIFY2(differing > 1000,
      qPrintable(QStringLiteral("|B| and |J| rendered near-identically (%1 "
                                "pixels differ) -- --quantity is not reaching "
                                "the plot").arg(differing)));
}

void TestPlotState::greyscaleActuallyRemovesTheColour()
{
  const QString colour = render("g_colour", { "--density", "--legend", "0", "--greyscale", "0" });
  const QString grey = render("g_grey", { "--density", "--legend", "0", "--greyscale", "1" });
  QVERIFY(!colour.isEmpty() && !grey.isEmpty());

  const ImageStats c = measure(colour);
  const ImageStats g = measure(grey);
  QVERIFY(c.ok && g.ok);

  QVERIFY2(c.colouredFraction > 0.3,
      qPrintable(QStringLiteral("the colour render is only %1 coloured")
                     .arg(c.colouredFraction, 0, 'f', 3)));
  // Not zero: the geometry overlay and selection colours are drawn on
  // top of the bands and are not part of the greyscale map.
  QVERIFY2(g.colouredFraction < 0.05,
      qPrintable(QStringLiteral("the greyscale render is still %1 coloured -- "
                                "--greyscale did not reach the colour map")
                     .arg(g.colouredFraction, 0, 'f', 3)));
}

void TestPlotState::theLegendReachesTheRenderedImage()
{
  // The legend is a child widget of the viewport, not an item in the
  // scene, and renderToImage renders the SCENE -- so before #86 a
  // rendered density plot came back with no colour key at all, and
  // --legend 0 and --legend 1 produced identical files.
  const QString on = render("l_on", { "--density", "--legend", "1" });
  const QString off = render("l_off", { "--density", "--legend", "0" });
  QVERIFY(!on.isEmpty() && !off.isEmpty());

  QRect bounds;
  const int differing = differingPixels(on, off, &bounds);
  QVERIFY2(differing > 500,
      qPrintable(QStringLiteral("--legend 0 and --legend 1 produced images "
                                "differing in %1 pixels -- the legend is not in "
                                "the render").arg(differing)));

  // And it is where a legend goes: tucked into the top-right corner.
  // Asserted as "right-aligned and top-anchored" rather than "starts
  // past the middle", because its WIDTH is not fixed -- it is sized from
  // font metrics, and the offscreen platform's default font makes it
  // noticeably wider than Windows' does. A wider legend starting further
  // left is still correctly placed; one drifting away from the corner is
  // not.
  QVERIFY2(bounds.right() >= kW - 16,
      qPrintable(QStringLiteral("the legend difference ends at x=%1 on a %2-wide "
                                "image -- it is not right-aligned")
                     .arg(bounds.right()).arg(kW)));
  QVERIFY2(bounds.top() <= 16,
      qPrintable(QStringLiteral("the legend difference starts at y=%1, which is "
                                "not the top corner").arg(bounds.top())));
  // A difference smeared over the whole image would mean the flag
  // changed the PLOT rather than the overlay.
  QVERIFY2(bounds.left() > kW / 3,
      qPrintable(QStringLiteral("the legend difference starts at x=%1, too far "
                                "left to be an overlay").arg(bounds.left())));
}

void TestPlotState::customBoundsChangeTheBanding()
{
  // This model's |B| runs 0 to about 2.1e-5 T, so these three ranges
  // band it differently: the first covers it, the others compress it
  // into fewer bands.
  const QString full = render("b_full", { "--density", "--legend", "0", "--bounds", "0", "2.1e-5" });
  const QString half = render("b_half", { "--density", "--legend", "0", "--bounds", "0", "1e-5" });
  const QString quarter = render("b_quarter", { "--density", "--legend", "0", "--bounds", "0", "5e-6" });
  QVERIFY(!full.isEmpty() && !half.isEmpty() && !quarter.isEmpty());

  QVERIFY2(differingPixels(full, half) > 1000,
      "two different --bounds ranges produced the same banding");
  QVERIFY2(differingPixels(half, quarter) > 1000,
      "two different --bounds ranges produced the same banding");

  // An empty range means "auto", not "one flat colour": a script that
  // passes the same number twice should get a readable plot, not a
  // single band.
  const QString degenerate = render("b_same", { "--density", "--legend", "0", "--bounds", "1", "1" });
  QVERIFY(!degenerate.isEmpty());
  const ImageStats st = measure(degenerate);
  QVERIFY2(st.distinctColours > 50,
      qPrintable(QStringLiteral("--bounds 1 1 rendered %1 distinct colours -- an "
                                "empty range collapsed the plot instead of "
                                "falling back to auto").arg(st.distinctColours)));
}

void TestPlotState::aPlotOptionOnAModelIsRefused()
{
  // Plot options describe a SOLUTION. Accepting them on a model would
  // silently drop them, which is the whole family of bug being fixed.
  const QString model = repoRoot() + "/manual_qt/images/example.feh";
  QVERIFY(QFile::exists(model));

  QString err;
  const bool ok = runRender({ "--render-png", model, m_tmp.filePath("m.png"),
                                "300", "220", "--density" },
      &err);
  QVERIFY2(!ok, "plot options on a model were accepted and silently dropped");
  QVERIFY2(err.contains("model"), qPrintable("unhelpful message: " + err));
}

// ---------------------------------------------------------------------------
// All four physics (issue #87)
// ---------------------------------------------------------------------------
//
// eo_/ho_/co_savepng used to refuse the Qt renderer outright, on the
// true premise that femmqt drew magnetics only. #83 gave it a Solution
// Viewer for all four and #85 stopped an unreadable file hanging the
// render, so the premise is gone. What has to survive the lifting is
// the PRINCIPLE the refusals were protecting: never quietly render
// something other than what was asked for.

void TestPlotState::everyPhysicsRendersTheModeItWasAskedFor_data()
{
  QTest::addColumn<QString>("solution");

  QTest::newRow("magnetics") << "manual_qt/images/example.ans";
  QTest::newRow("electrostatics") << "test/results/analytic_fields/coax.res";
  QTest::newRow("heat flow") << "manual_qt/images/example.anh";
  QTest::newRow("current flow") << "test/results/analytic_fields/bar.anc";
}

void TestPlotState::everyPhysicsRendersTheModeItWasAskedFor()
{
  QFETCH(QString, solution);
  const QString path = repoRoot() + "/" + solution;
  QVERIFY2(QFile::exists(path), qPrintable(path + " is missing"));

  const QString base = QFileInfo(path).completeBaseName() + QFileInfo(path).suffix();
  const QString density = m_tmp.filePath("d_" + base + ".png");
  const QString contour = m_tmp.filePath("c_" + base + ".png");

  QString err;
  QVERIFY2(runRender({ "--render-png", path, density, QString::number(kW),
                         QString::number(kH), "--density", "--legend", "0" },
               &err),
      qPrintable("density render failed: " + err));
  QVERIFY2(runRender({ "--render-png", path, contour, QString::number(kW),
                         QString::number(kH), "--contour", "--legend", "0" },
               &err),
      qPrintable("contour render failed: " + err));

  const ImageStats d = measure(density);
  const ImageStats c = measure(contour);
  QVERIFY(d.ok && c.ok);

  // Identity, not difference -- "it produced a file" would pass even if
  // every physics came back with femmqt's default plot, which is
  // precisely what this used to do for magnetics.
  //
  // The property used is COLOUR, not area: a density plot paints filled
  // bands from the colour map, a contour plot draws thin near-neutral
  // lines. Measured across the four solutions the density renders are
  // 45-100% coloured and the contour renders 0.6-0.7% -- two orders of
  // magnitude apart. Area alone is not usable: bar.anc is a thin bar
  // that occupies little of a 4:3 canvas, so its density plot is 55%
  // background and a threshold tuned to the others would fail it for a
  // reason that has nothing to do with the mode.
  QVERIFY2(d.colouredFraction > 0.25,
      qPrintable(QStringLiteral("%1: the --density render is only %2 coloured -- "
                                "that is contour lines, not filled bands")
                     .arg(solution).arg(d.colouredFraction, 0, 'f', 4)));
  QVERIFY2(c.colouredFraction < 0.05,
      qPrintable(QStringLiteral("%1: the --contour render is %2 coloured -- that "
                                "is a filled plot, not contour lines")
                     .arg(solution).arg(c.colouredFraction, 0, 'f', 4)));
  QVERIFY2(c.backgroundFraction > d.backgroundFraction,
      qPrintable(QStringLiteral("%1: the contour render covers as much of the "
                                "canvas as the density one")
                     .arg(solution)));
}

void TestPlotState::aQuantityTheOtherPhysicsCannotDrawIsRefusedByName_data()
{
  QTest::addColumn<QString>("solution");
  QTest::addColumn<QString>("expectField");

  QTest::newRow("electrostatics") << "test/results/analytic_fields/coax.res" << "|D|";
  QTest::newRow("heat flow") << "manual_qt/images/example.anh" << "|F|";
  QTest::newRow("current flow") << "test/results/analytic_fields/bar.anc" << "|J|";
}

void TestPlotState::aQuantityTheOtherPhysicsCannotDrawIsRefusedByName()
{
  QFETCH(QString, solution);
  QFETCH(QString, expectField);

  const QString path = repoRoot() + "/" + solution;
  QVERIFY2(QFile::exists(path), qPrintable(path + " is missing"));

  // The ten density quantities are magnetics'. |H| and |J| come from
  // permeability and conductivity, which mean something else or nothing
  // at all in these formats -- so drawing the field and labelling it
  // "|H|" would be the defect, not the fix.
  QString err;
  const bool ok = runRender({ "--render-png", path, m_tmp.filePath("q.png"),
                                "300", "220", "--density", "--quantity", "hmag" },
      &err);
  QVERIFY2(!ok,
      qPrintable(QStringLiteral("%1 accepted --quantity hmag").arg(solution)));
  QVERIFY2(err.contains(expectField),
      qPrintable(QStringLiteral("the refusal does not name the one quantity this "
                                "physics does plot (%1): %2")
                     .arg(expectField, err)));

  // And the quantity this physics DOES draw needs no naming at all --
  // the plain density render must still work.
  QVERIFY2(runRender({ "--render-png", path, m_tmp.filePath("q_ok.png"), "300",
                         "220", "--density" },
               &err),
      qPrintable("a plain density render was refused too: " + err));
}

void TestPlotState::noMagneticsOnlyRefusalIsLeftStanding()
{
  // Six copies of the same hardcoded sentence existed -- three
  // post-processors and three editors. A stale one is a script getting
  // told the Qt GUI cannot do something it now does.
  const QStringList files = { "belaviewLua.cpp", "hviewLua.cpp", "CVIEWLUA.CPP",
    "beladrawLua.cpp", "HDRAWLUA.CPP", "CDRAWLUA.CPP" };

  QStringList offenders;
  for (const QString& name : files) {
    const QString path = repoRoot() + "/femm/" + name;
    QVERIFY2(QFile::exists(path), qPrintable(name + " is gone -- update this list"));
    const QString code = readAll(path);
    QVERIFY2(!code.isEmpty(), qPrintable(name + " read as empty"));
    if (code.contains(QStringLiteral("supports magnetics only")))
      offenders << name;
  }
  QVERIFY2(offenders.isEmpty(),
      qPrintable(QStringLiteral("%1 still tells scripts the Qt GUI supports "
                                "magnetics only, which stopped being true when "
                                "#83 landed").arg(offenders.join(", "))));
}

void TestPlotState::theClassicSideAndTheRendererAgreeOnWhichQuantityIsDrawn()
{
  // The risk this closes: the classic side decides, from its own table,
  // which DensityPlot index femmqt is able to render -- and then does
  // NOT pass a quantity, because these physics have only one. So if
  // that table named the wrong index, a script showing |E| would be
  // rendered as |D| with nothing to say so. femmqt cannot catch it,
  // having been told nothing.
  //
  // The table is MFC code and not linked here, so it is read as text --
  // properly, out of the switch, not out of a comment. If the shape
  // changes enough that this cannot find it, that is a failure worth
  // having: an unreadable rule is an unchecked one.
  const QString code = readAll(repoRoot() + "/femm/ScriptGui.cpp");
  QVERIFY2(!code.isEmpty(), "femm/ScriptGui.cpp did not read");

  struct Case {
    const char* physics;
    FemmProblemKind kind;
  };
  const QVector<Case> cases = {
    { "Electrostatics", FemmProblemKind::Electrostatics },
    { "HeatFlow", FemmProblemKind::HeatFlow },
    { "CurrentFlow", FemmProblemKind::CurrentFlow },
  };

  for (const Case& c : cases) {
    // case QtRenderPhysics::X: ... names = kY; ... renderableIndex = N;
    const QRegularExpression armRe(
        QStringLiteral("case QtRenderPhysics::%1:(.*?)break;").arg(c.physics),
        QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch arm = armRe.match(code);
    QVERIFY2(arm.hasMatch(),
        qPrintable(QStringLiteral("no QtRenderPhysics::%1 arm in ScriptGui.cpp")
                       .arg(c.physics)));

    const QRegularExpressionMatch nameVar =
        QRegularExpression(QStringLiteral("names\\s*=\\s*(k\\w+)\\s*;"))
            .match(arm.captured(1));
    const QRegularExpressionMatch indexM =
        QRegularExpression(QStringLiteral("renderableIndex\\s*=\\s*(\\d+)\\s*;"))
            .match(arm.captured(1));
    QVERIFY2(nameVar.hasMatch() && indexM.hasMatch(),
        qPrintable(QStringLiteral("could not read the table for %1").arg(c.physics)));

    // The array it points at.
    const QRegularExpression arrayRe(
        QStringLiteral("%1\\[\\]\\s*=\\s*\\{(.*?)\\};").arg(nameVar.captured(1)),
        QRegularExpression::DotMatchesEverythingOption);
    const QRegularExpressionMatch array = arrayRe.match(code);
    QVERIFY2(array.hasMatch(),
        qPrintable(QStringLiteral("no %1[] in ScriptGui.cpp").arg(nameVar.captured(1))));

    QStringList entries;
    QRegularExpressionMatchIterator it =
        QRegularExpression(QStringLiteral("\"([^\"]*)\"")).globalMatch(array.captured(1));
    while (it.hasNext())
      entries << it.next().captured(1);

    const int index = indexM.captured(1).toInt(); // 1-based
    QVERIFY2(index >= 1 && index <= entries.size(),
        qPrintable(QStringLiteral("%1's renderable index %2 is outside its own "
                                  "table of %3")
                       .arg(c.physics).arg(index).arg(entries.size())));

    const QString classicName = entries.at(index - 1);
    const SolutionField::Quantity ours = SolutionField::fieldQuantity(c.kind);

    // The classic legend strings read "|D|, C/m^2"; the unit is the
    // half that cannot be got right by accident.
    QVERIFY2(classicName.contains(ours.unit),
        qPrintable(QStringLiteral("for %1 the classic side treats \"%2\" as the "
                                  "quantity femmqt renders, but femmqt renders "
                                  "%3 in %4. A script showing a different "
                                  "quantity would be rendered as this one with "
                                  "nothing to say so.")
                       .arg(c.physics, classicName, ours.name, ours.unit)));
  }
}

QTEST_GUILESS_MAIN(TestPlotState)
#include "tst_plot_state.moc"
