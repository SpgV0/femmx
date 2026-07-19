#include <QApplication>
#include <QFileInfo>

#include "AnsFileIO.h"
#include "AnsxFileIO.h"
#include "FemmProblem.h"
#include "MainWindow.h"
#include "MeshSolution.h"
#include "SolutionView.h"

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
  const QStringList args = app.arguments();

  if (args.size() >= 3 && args.at(1) == "--convert-ansx")
    return convertAnsxCli(args.at(2));

  // A file path on the command line opens immediately -- used by the
  // femm.cfg-driven GUI switch (step 7) to hand off the currently-open
  // file to the other GUI, mirroring femm.cpp's ProcessShellCommand
  // single-file-open convention. Routed by extension: a solved .ans/.ansx
  // (handed off from the classic GUI's post-processor, CFemmviewView::
  // OnSwitchToQtGui) opens the Solution Viewer, not the geometry editor --
  // otherwise the geometry editor would try to load a possibly-huge
  // solved mesh as if it were raw, editable geometry.
  bool isSolutionFile = false;
  if (args.size() > 1) {
    QString suffix = QFileInfo(args.at(1)).suffix();
    isSolutionFile = suffix.compare("ans", Qt::CaseInsensitive) == 0 || suffix.compare("ansx", Qt::CaseInsensitive) == 0;
  }

  if (isSolutionFile) {
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
