#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include "AnsFileIO.h"
#include "SolutionFileIO.h"
#include "ProblemKind.h"
#include "AnsxFileIO.h"
#include "AppPreferences.h"
#include "AppTheme.h"
#include "ConstraintSolver.h"
#include "CircuitAnalysis.h"
#include "DxfIO.h"
#include "FemmProblem.h"
#include "FemmFileIO.h"
#include "FemmProblemEdit.h"
#include "FileRouting.h"
#include "MainWindow.h"
#include "Notify.h"
#include "PlotStateArgs.h"
#include "MeshSolution.h"
#include "SolutionView.h"

#include <cmath>
#include <complex>
#include <cstdio>

namespace {
// Offline/batch .ansx generation, independent of opening any window --
// `femmqt.exe --convert-ansx foo.ans` regenerates foo.ansx unconditionally
// (skips the freshness check AnsxFileIO::isUpToDate uses elsewhere) and
// exits, per this phase's plan. Uses QCoreApplication-only APIs (file I/O,
// no widgets), so it's safe to run before QApplication would normally be
// needed -- though the executable is still WIN32-subsystem, so stdio
// output is only visible when launched from a console that keeps it
// attached.
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-10:
// `femmqt.exe --import-dxf <in.dxf> <out.fem> [tolerance]` parses a DXF
// through femmqt's own DxfIO and writes the resulting geometry as a .fem.
// There are two independent DXF implementations in this tree -- classic
// FEMM's MOVECOPY.CPP ReadDXF and femmqt's port of it -- and nothing could
// compare them, because the classic one is reachable from Lua
// (mi_readdxf) while this one was only reachable by driving the GUI
// (issue #8). Mirrors --convert-ansx: stderr for messages, exit code for
// success. Omitting the tolerance uses the same suggested value classic
// FEMM's import dialog auto-fills.
int importDxfCli(const QString& dxfPath, const QString& femPath,
                 const QString& toleranceArg)
{
  FemmProblem problem;
  double suggestedTolerance = 0.0;
  QString error;
  if (!DxfIO::parseDxf(dxfPath, problem, suggestedTolerance, error)) {
    fprintf(stderr, "%s\n", qPrintable(error));
    return 1;
  }

  double tolerance = suggestedTolerance;
  if (!toleranceArg.isEmpty()) {
    bool ok = false;
    const double parsed = toleranceArg.toDouble(&ok);
    if (!ok || parsed < 0.0) {
      fprintf(stderr, "invalid tolerance: %s\n", qPrintable(toleranceArg));
      return 1;
    }
    tolerance = parsed;
  }
  if (tolerance > 0.0)
    DxfIO::mergeCoincidentNodes(problem, tolerance);

  if (!FemmFileIO::writeFem(femPath, problem, error)) {
    fprintf(stderr, "%s\n", qPrintable(error));
    return 1;
  }
  fprintf(stderr, "Wrote %s (%d nodes, %d segments, %d arcs, tolerance %g)\n",
          qPrintable(femPath), (int)problem.nodes.size(),
          (int)problem.segments.size(), (int)problem.arcSegments.size(),
          tolerance);
  return 0;
}
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-11:
// `femmqt.exe --probe-ans <in.ans> <out.csv> [maxElements]` dumps what
// femmqt's own post-processing computes from a solution -- per-element
// flux density and centroid, the block areas, and each circuit's current,
// voltage drop and flux linkage -- as CSV on stdout-equivalent.
//
// femmqt has a second, independent implementation of numbers users make
// engineering decisions with (AnsFileIO's field computation,
// CircuitAnalysis, the block integrals), and nothing could compare them
// against the classic post-processor, because classic's are reachable
// from Lua while femmqt's were only reachable by driving the GUI
// (issue #16). Mirrors --convert-ansx and --import-dxf: stderr for
// messages, exit code for success, and a machine-readable artifact for a
// test to diff.
int probeAnsCli(const QString& ansPath, const QString& csvPath,
                int maxElements)
{
  FemmProblem problem;
  MeshSolution solution;
  QString error;
  if (!AnsFileIO::readAns(ansPath, problem, solution, error)) {
    fprintf(stderr, "%s\n", qPrintable(error));
    return 1;
  }

  QFile out(csvPath);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
    fprintf(stderr, "--probe-ans: could not write %s\n", qPrintable(csvPath));
    return 1;
  }
  QTextStream ts(&out);
  ts.setRealNumberNotation(QTextStream::ScientificNotation);
  ts.setRealNumberPrecision(12);

  ts << "kind,index,a,b,c,d\n";
  ts << "mesh,0," << solution.nodes.size() << "," << solution.elements.size()
     << "," << solution.bMagMin << "," << solution.bMagMax << "\n";

  // Per-element centroid and flux density. Sampled rather than dumped in
  // full: a real mesh has tens of thousands of elements and the test only
  // needs enough points to catch a systematic difference.
  const int total = solution.elements.size();
  const int stride = (maxElements > 0 && total > maxElements)
      ? (total + maxElements - 1) / maxElements
      : 1;
  for (int i = 0; i < total; i += stride) {
    const MeshSolutionElement& e = solution.elements[i];
    const double bx = std::hypot(e.B1re, e.B1im);
    const double by = std::hypot(e.B2re, e.B2im);
    ts << "element," << i << "," << e.ctrX << "," << e.ctrY << ","
       << bx << "," << by << "\n";
  }

  // Circuit properties, femmqt's own CircuitAnalysis.
  QVector<CircuitAnalysis::BlockCircuitInfo> blockInfo;
  QString circErr;
  if (!CircuitAnalysis::readBlockCircuitInfo(ansPath, blockInfo, circErr)) {
    // Reported rather than silently skipped: a test that finds no circuit
    // rows needs to know whether that is "this model has none" or "the
    // read failed".
    fprintf(stderr, "--probe-ans: no circuit info (%s)\n",
            qPrintable(circErr));
  } else {
    for (int c = 0; c < problem.circuitProps.size(); c++) {
      CircuitAnalysis::Result r =
          CircuitAnalysis::compute(problem, solution, blockInfo, c + 1);
      if (!r.ok)
        continue;
      ts << "circuit," << c << "," << r.amps.real() << ","
         << r.voltsDrop.real() << "," << r.fluxLinkage.real() << ",0\n";
    }
  }

  out.close();
  fprintf(stderr, "Wrote %s (%d nodes, %d elements, every %d-th sampled)\n",
          qPrintable(csvPath), (int)solution.nodes.size(), total, stride);
  return 0;
}

int convertAnsxCli(const QString& ansPath)
{
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
  // (issue #83): .ansx is the MAGNETICS mesh cache, exactly as .femx is
  // the magnetics model cache (see FILE_FORMATS.md for why that scoping
  // is deliberate). Handed a .res/.anh/.anc this would read it with the
  // magnetics parser, which finds none of its own node columns, and then
  // write a cache of the wrong mesh -- silently, since every step
  // "succeeds".
  FemmProblemKind solutionKind = FemmProblemKind::Magnetics;
  if (SolutionFileIO::kindForSolutionPath(ansPath, solutionKind)
      && solutionKind != FemmProblemKind::Magnetics) {
    fprintf(stderr,
        "--convert-ansx: .ansx caches magnetics solutions only; \"%s\" is a %s "
        "solution. Nothing to convert.\n",
        qPrintable(QFileInfo(ansPath).fileName()),
        qPrintable(ProblemKind::displayName(solutionKind)));
    return 1;
  }

  FemmProblem problem;
  MeshSolution solution;
  QString error;
  if (!AnsFileIO::readAns(ansPath, problem, solution, error)) {
    fprintf(stderr, "%s\n", qPrintable(error));
    return 1;
  }
  QFileInfo fi(ansPath);
  QString ansxPath = fi.absolutePath() + "/" + fi.completeBaseName() + ".ansx";
  if (!AnsxFileIO::writeAnsx(ansxPath, ansPath, (int)problem.problemType, (int)problem.lengthUnits,
          problem.frequency, solution, error)) {
    fprintf(stderr, "%s\n", qPrintable(error));
    return 1;
  }
  fprintf(stderr, "Wrote %s (%d nodes, %d elements)\n", qPrintable(ansxPath), (int)solution.nodes.size(), (int)solution.elements.size());
  return 0;
}

// Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-11:
// the ~520 lines of constraint/dimension test scenarios that used to live
// here moved to femmqt/tests/tst_constraint_solver.cpp, where ctest runs
// them on every build (issue #12). They were a bespoke harness printing
// "ALL TESTS PASSED" that nothing in CI ever invoked -- numerical solver
// code no automated job runs is code that drifts. The flag is kept as a
// pointer rather than deleted outright, so anyone with it in a script or a
// note gets told where the tests went instead of an unknown-argument error.
int testConstraintsCli()
{
  fprintf(stderr,
      "The constraint/dimension tests moved into femmqt's QTest binary and\n"
      "now run automatically. To run them:\n"
      "\n"
      "  ./build.ps1 -DoNotUpdateTOOL -DisableInteractive -DisableLaTeX\n"
      "             -ForceTriangle32bit -DoNotDeleteBuildFolder\n"
      "  ctest --test-dir build_win_release64_notriangle -C Release\n"
      "\n"
      "Source: femmqt/tests/tst_constraint_solver.cpp\n");
  return 0;
}
} // namespace

int main(int argc, char* argv[])
{
  QApplication app(argc, argv);
  // Gives QSettings (MainWindow's recent-files list) a stable registry
  // location -- without this it defaults to an unset/empty organization,
  // which still works but isn't a location a user (or an uninstaller)
  // could find on purpose.
  QCoreApplication::setOrganizationName("FEMMX");
  QCoreApplication::setApplicationName("femmqt");

  // Must happen before any window is constructed -- IconTheme::
  // themedToolIcon() and GeometryScene/SolutionWindow's background brush
  // both bake in whatever AppTheme::isDark()/qApp->palette() is current
  // at construction time.
  AppTheme::setDark(AppPreferences::load().darkTheme);

  const QStringList args = app.arguments();

  // Issue #85: decide ONCE, before any window exists, whether there is
  // anybody to answer a dialog. Every option below is a batch entry
  // point, usually with no desktop at all, and a modal QMessageBox on
  // one of them is not an error report -- it is a hang that only a
  // timeout can distinguish from a slow model. See Notify.h.
  //
  // Keyed on "argv[1] starts with --" rather than on each flag
  // individually, so an option added later is headless by default
  // instead of by being remembered.
  if (args.size() >= 2 && args.at(1).startsWith(QLatin1String("--")))
    Notify::setHeadless(true);

  if (args.size() >= 3 && args.at(1) == "--convert-ansx")
    return convertAnsxCli(args.at(2));

  if (args.size() >= 4 && args.at(1) == "--import-dxf")
    return importDxfCli(args.at(2), args.at(3),
                        args.size() >= 5 ? args.at(4) : QString());

  if (args.size() >= 4 && args.at(1) == "--probe-ans")
    return probeAnsCli(args.at(2), args.at(3),
                       args.size() >= 5 ? args.at(4).toInt() : 200);

  // `femmqt.exe --render-png <in> <out> [w h]` renders a .fem/.ans
  // offscreen to a PNG. This is what the classic GUI's Lua
  // mi_savepng/mo_savepng shell out to after a script calls
  // setgui("qt") -- Lua only ever runs in the MFC app, so "render with
  // the new GUI" has to mean handing the file to this executable. See
  // femm/ScriptGui.h for the whole arrangement.
  if (args.size() >= 4 && args.at(1) == "--render-png") {
    const QString in = args.at(2);
    const QString out = args.at(3);
    // The positional arguments are recognised by SHAPE, not by count.
    // Issue #86 appends named options after them, so `--render-png in
    // out 600 450 --quantity bmag --bounds 0 2` has ten arguments and
    // the old count-based rule would have read "--quantity" and "bmag"
    // as crop coordinates -- toDouble()ing both to 0 and rendering an
    // empty region with nothing said.
    auto positionalNumber = [&args](int index, double& out) {
      if (index >= args.size() || args.at(index).startsWith(QLatin1String("--")))
        return false;
      bool ok = false;
      const double v = args.at(index).toDouble(&ok);
      if (ok)
        out = v;
      return ok;
    };

    double wd = 1024, hd = 768;
    const bool haveSize = positionalNumber(4, wd) && positionalNumber(5, hd);
    const int w = haveSize ? (int)wd : 1024;
    const int h = haveSize ? (int)hd : 768;
    if (w <= 0 || h <= 0) {
      fprintf(stderr, "--render-png: bad size %dx%d\n", w, h);
      return 1;
    }

    // Issue #14: check the input exists BEFORE constructing a window.
    // MainWindow::openFile / SolutionWindow::openAnsFile report a missing
    // or unreadable file through a modal QMessageBox, which in a CLI run
    // has nobody to dismiss it: measured, `--render-png missing.fem out.png`
    // sat there until killed rather than failing. A batch render over a
    // directory with one bad path would hang the whole job.
    if (!QFileInfo::exists(in)) {
      fprintf(stderr, "--render-png: no such file: %s\n", qPrintable(in));
      return 1;
    }

    const bool isSolution = FileRouting::isSolutionFile(in);

    // Issue #85: refuse an extension neither window understands, here,
    // rather than letting it fall through to the geometry editor. That
    // path used to pop a modal "not a FEMM model file" warning and hang;
    // with Notify it no longer hangs, but it would still leave an empty
    // scene to render and exit 0 -- a valid PNG of nothing, which is in
    // some ways worse than the hang, because nothing reports it. Name
    // the extension: the usual cause is a typo, or a solver that wrote
    // its output somewhere other than where the caller looked.
    FemmProblemKind modelKind = FemmProblemKind::Magnetics;
    if (!isSolution && !ProblemKind::kindForPath(in, modelKind)) {
      const QString suffix = QFileInfo(in).suffix();
      fprintf(stderr,
          "--render-png: %s: unrecognised extension \"%s\". Models are .fem, "
          ".fee, .feh, .fec; solutions are .ans, .res, .anh, .anc.\n",
          qPrintable(in),
          qPrintable(suffix.isEmpty() ? QStringLiteral("(none)") : suffix));
      return 1;
    }

    // Optional scene-space crop: --render-png in out w h x0 y0 x1 y1.
    // Renders a zoomed-in region without a GUI, which matters because the
    // density plot's color banding is scaled to whatever is VISIBLE (see
    // MeshSolutionItem::paintDensity) -- so a full-model render cannot
    // reproduce, or regress-test, how a zoomed-in view actually looks.
    // Issue #86: the plot state the classic post-processor was showing
    // when the script called mo_savepng. Parsed before anything is
    // constructed so a bad option fails immediately -- and a bad option
    // IS a failure: silently rendering defaults after being told what
    // to draw is the bug being fixed, not a fallback.
    PlotState plot;
    QString plotError;
    if (!PlotStateArgs::parse(args.mid(2), plot, plotError)) {
      fprintf(stderr, "--render-png: %s\n", qPrintable(plotError));
      return 1;
    }
    if (!plot.isEmpty() && !isSolution) {
      fprintf(stderr, "--render-png: plot options apply to a solution; \"%s\" "
                      "is a model\n", qPrintable(in));
      return 1;
    }

    QRectF source;
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (haveSize && positionalNumber(6, x0) && positionalNumber(7, y0)
        && positionalNumber(8, x1) && positionalNumber(9, y1)) {
      source = QRectF(QPointF(x0, y0), QPointF(x1, y1)).normalized();
    }

    QImage image;
    // The windows are constructed but never shown: renderToImage draws
    // the scene directly, which is why this works without a desktop.
    if (isSolution) {
      SolutionWindow window;
      window.resize(w, h);
      // #83: openSolutionFile routes .ans/.ansx down the unchanged
      // magnetics path and .res/.anh/.anc through the shared reader, so
      // --render-png works for every solver's output.
      if (!window.openSolutionFile(in)) {
        // Notify has already said what went wrong on stderr; the exit
        // code is what a batch caller actually branches on.
        return 1;
      }
      // Issue #86: the post-processor's view state, forwarded by
      // mo_savepng. Applied AFTER the load, because everything it
      // touches lives on the item the load creates -- and the return
      // value is checked, because a state that silently failed to
      // apply produces exactly the defect this fixes: a valid PNG of
      // the wrong plot.
      QString plotApplyError;
      if (!plot.isEmpty() && !window.applyPlotState(plot, &plotApplyError)) {
        fprintf(stderr, "--render-png: %s\n", qPrintable(plotApplyError));
        return 1;
      }
      // Modified by Claude (Anthropic), noreply@anthropic.com: found while
      // trying to visually verify an unrelated density-plot fix -- this
      // call was never here. selectDensityPlot() existed and --density
      // was documented in the commit that added it, but nothing in main()
      // actually checked args for it, so EVERY --density render since
      // that commit silently stayed in the viewer's default Contour mode
      // instead. Every "density" screenshot taken via this CLI path
      // before this fix was actually a contour plot.
      // The plot mode used to be applied here as well as in
      // applyPlotState. Two paths setting the same thing is how one of
      // them stops being exercised without anything noticing -- the
      // mode now crosses exactly once, through the state.
      image = window.renderToImage(QSize(w, h), source);
    } else {
      MainWindow window;
      window.resize(w, h);
      if (!window.openFile(in))
        return 1;
      image = window.renderToImage(QSize(w, h), source);
    }

    if (image.isNull()) {
      fprintf(stderr, "--render-png: nothing to render from %s\n",
          qPrintable(in));
      return 1;
    }
    if (!image.save(out, "PNG")) {
      fprintf(stderr, "--render-png: couldn't write %s\n", qPrintable(out));
      return 1;
    }
    return 0;
  }

  if (args.size() >= 2 && args.at(1) == "--test-constraints")
    return testConstraintsCli();

  // A file path on the command line opens immediately -- used by the
  // femm.cfg-driven GUI switch (step 7) to hand off the currently-open
  // file to the other GUI, mirroring femm.cpp's ProcessShellCommand
  // single-file-open convention. Routed by extension: a solved .ans/.ansx
  // (handed off from the classic GUI's post-processor, CFemmviewView::
  // OnSwitchToQtGui) opens the Solution Viewer, not the geometry editor --
  // otherwise the geometry editor would try to load a possibly-huge
  // solved mesh as if it were raw, editable geometry.
  //
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
  // (issue #88): this said openAnsFile, the MAGNETICS-only path, while
  // isSolutionFile() has matched all four solution formats since #83.
  // It went unnoticed only because the classic GUI had no "Switch to Qt
  // GUI" item outside magnetics, so nothing could hand a .anh across --
  // which is exactly what #88 adds. openSolutionFile routes .ans/.ansx
  // down the unchanged magnetics path and the other three through the
  // shared reader.
  const bool isSolutionFile = args.size() > 1
      && FileRouting::isSolutionFile(args.at(1));

  if (isSolutionFile) {
    auto* solutionWindow = new SolutionWindow();
    solutionWindow->show();
    solutionWindow->openSolutionFile(args.at(1));
  } else {
    auto* window = new MainWindow();
    window->show();
    if (args.size() > 1)
      window->openFile(args.at(1));
  }

  return app.exec();
}
