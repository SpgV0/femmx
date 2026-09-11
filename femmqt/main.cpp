#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include "AnsFileIO.h"
#include "AnsxFileIO.h"
#include "AppPreferences.h"
#include "AppTheme.h"
#include "ConstraintSolver.h"
#include "CircuitAnalysis.h"
#include "DxfIO.h"
#include "FemmProblem.h"
#include "FemmFileIO.h"
#include "FemmProblemEdit.h"
#include "MainWindow.h"
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
    const int w = (args.size() >= 6) ? args.at(4).toInt() : 1024;
    const int h = (args.size() >= 6) ? args.at(5).toInt() : 768;
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

    const QString suffix = QFileInfo(in).suffix();
    const bool isSolution = suffix.compare("ans", Qt::CaseInsensitive) == 0
        || suffix.compare("ansx", Qt::CaseInsensitive) == 0;

    // Optional scene-space crop: --render-png in out w h x0 y0 x1 y1.
    // Renders a zoomed-in region without a GUI, which matters because the
    // density plot's color banding is scaled to whatever is VISIBLE (see
    // MeshSolutionItem::paintDensity) -- so a full-model render cannot
    // reproduce, or regress-test, how a zoomed-in view actually looks.
    QRectF source;
    if (args.size() >= 10) {
      const double x0 = args.at(6).toDouble(), y0 = args.at(7).toDouble();
      const double x1 = args.at(8).toDouble(), y1 = args.at(9).toDouble();
      source = QRectF(QPointF(x0, y0), QPointF(x1, y1)).normalized();
    }

    QImage image;
    // The windows are constructed but never shown: renderToImage draws
    // the scene directly, which is why this works without a desktop.
    if (isSolution) {
      SolutionWindow window;
      window.resize(w, h);
      window.openAnsFile(in);
      // Modified by Claude (Anthropic), noreply@anthropic.com: found while
      // trying to visually verify an unrelated density-plot fix -- this
      // call was never here. selectDensityPlot() existed and --density
      // was documented in the commit that added it, but nothing in main()
      // actually checked args for it, so EVERY --density render since
      // that commit silently stayed in the viewer's default Contour mode
      // instead. Every "density" screenshot taken via this CLI path
      // before this fix was actually a contour plot.
      if (args.contains("--density"))
        window.selectDensityPlot();
      image = window.renderToImage(QSize(w, h), source);
    } else {
      MainWindow window;
      window.resize(w, h);
      window.openFile(in);
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
  QString suffix = args.size() > 1 ? QFileInfo(args.at(1)).suffix() : QString();
  bool isMagSolutionFile = suffix.compare("ans", Qt::CaseInsensitive) == 0 || suffix.compare("ansx", Qt::CaseInsensitive) == 0;

  if (isMagSolutionFile) {
    auto* solutionWindow = new SolutionWindow();
    solutionWindow->show();
    solutionWindow->openAnsFile(args.at(1));
  } else {
    auto* window = new MainWindow();
    window->show();
    if (args.size() > 1)
      window->openFile(args.at(1));
  }

  return app.exec();
}
