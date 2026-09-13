#include "SolveRunner.h"

#include "FemmProblem.h"
#include "ProblemKind.h"
#include "MeshBuilder.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>

#include <algorithm>

namespace {

// femm/StdAfx.h:102-103, ported verbatim.
constexpr double kMinAngleBump = 3.0;
constexpr double kMinAngleMax = 33.8;

QString solverDir()
{
  return QCoreApplication::applicationDirPath();
}

// Plain QProcess::waitForFinished(-1) blocks this thread's event loop
// entirely for however long triangle.exe/fkn.exe take -- fine on its own,
// but it means nothing else in the app can run meanwhile either: no
// repaints, and no QTimer ever fires. That silently broke the Load
// Monitor (LoadMonitorDialog.cpp's sampling is timer-driven) and made the
// whole window appear frozen/"Not Responding" during a long solve.
// Polling in short bursts and pumping the event loop between them keeps
// both working, at the cost of solve() no longer being safely
// re-entrant -- callers must disable whatever UI could start a second
// solve/mesh concurrently while this runs (see MainWindow::
// onSolveTriggered/onCreateMeshTriggered).
void waitPumpingEvents(QProcess& proc)
{
  while (proc.state() != QProcess::NotRunning) {
    proc.waitForFinished(50);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  }
}

} // namespace

bool SolveRunner::mesh(const FemmProblem& problem, const QString& femPath, QString& errorMessage)
{
  QFileInfo fi(femPath);
  QString rootPath = fi.absolutePath() + "/" + fi.completeBaseName();
  QString workingDir = fi.absolutePath();

  if (!MeshBuilder::writePolyAndPbc(problem, rootPath, errorMessage))
    return false;

  double minAngle = std::min(problem.minAngle + kMinAngleBump, kMinAngleMax);

  QProcess triangle;
  triangle.setWorkingDirectory(workingDir);
  QStringList triangleArgs = {
    "-p", "-P", "-j",
    QStringLiteral("-q%1").arg(minAngle),
    "-e", "-A", "-a", "-z", "-Q", "-I",
    rootPath
  };
  triangle.start(solverDir() + "/triangle.exe", triangleArgs);
  if (!triangle.waitForStarted(10000)) {
    errorMessage = "Couldn't spawn triangle.exe.";
    return false;
  }
  waitPumpingEvents(triangle);
  if (triangle.exitStatus() != QProcess::NormalExit || triangle.exitCode() != 0) {
    errorMessage = "Call to triangle was unsuccessful. Check for small angles.";
    return false;
  }
  return true;
}

namespace {

// What each solver's exit codes MEAN, taken from each one's own main.cpp
// rather than assumed to match fkn's -- which #82 asked for, and which
// turned out to matter.
//
// fkn, belasolv and csolv agree:
//     2 mesh   3 renumber   4 allocate   5 solve   6 write   7 input file
//
// hsolv DOES NOT. Its codes are shifted by one from 3 upward, it has an
// extra failure of its own, and it reuses 7:
//     2 mesh
//     3 could not load the previous solution   <- hsolv only, and silent
//     4 renumber
//     5 allocate
//     6 solve
//     7 input file OR could not write results  <- two meanings
//
// Using fkn's table for heat flow would therefore mislabel every failure
// from code 3 up: a heat-flow run that ran out of memory would report
// "couldn't solve the problem", and one that could not write its results
// would report "problem loading the .feh file".
} // namespace

QString SolveRunner::exitMessage(FemmProblemKind kind, int code)
{
  if (kind == FemmProblemKind::HeatFlow) {
    switch (code) {
    case 2: return QStringLiteral("problem loading mesh");
    case 3:
      // hsolv exits here without a message of its own when LoadPrev()
      // fails, which is the <PrevSoln> file named in the .feh.
      return QStringLiteral("couldn't load the previous solution named by this "
                            "problem (check <PrevSoln>)");
    case 4: return QStringLiteral("problem renumbering nodes");
    case 5: return QStringLiteral("couldn't allocate enough space for matrices");
    case 6: return QStringLiteral("couldn't solve the problem");
    case 7:
      // The one genuinely ambiguous code in any of the four.
      return QStringLiteral("problem loading the .feh file, or couldn't write "
                            "results to disk -- hsolv reports both as 7");
    default: break;
    }
    return QStringLiteral("exited with unrecognized code %1").arg(code);
  }

  switch (code) {
  // No solver actually emits 1; the classic GUI maps it, so it is kept
  // for parity with what a user of femmx.exe would have seen.
  case 1: return QStringLiteral("material properties have not been defined for all regions");
  case 2: return QStringLiteral("problem loading mesh");
  case 3: return QStringLiteral("problem renumbering node points");
  case 4: return QStringLiteral("couldn't allocate enough space for matrices");
  case 5: return QStringLiteral("couldn't solve the problem");
  case 6: return QStringLiteral("couldn't write results to disk");
  case 7: return QStringLiteral("problem loading input file");
  default: break;
  }
  return QStringLiteral("exited with unrecognized code %1").arg(code);
}

bool SolveRunner::solve(const FemmProblem& problem, const QString& filePath, QString& errorMessage)
{
  QFileInfo fi(filePath);
  QString rootPath = fi.absolutePath() + "/" + fi.completeBaseName();
  QString workingDir = fi.absolutePath();

  if (!mesh(problem, filePath, errorMessage))
    return false;

  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
  // (issue #82): the solver that matches the problem, not fkn.exe.
  const QString exeName = ProblemKind::solverExecutable(problem.kind);
  const QString exePath = solverDir() + "/" + exeName;

  // Checked before starting, so a missing or unbuilt solver says which
  // binary is missing and where it was looked for. QProcess's own
  // failure for a non-existent program is a generic start failure that
  // names neither.
  if (!QFileInfo::exists(exePath)) {
    errorMessage = QStringLiteral(
        "The %1 solver \"%2\" was not found in \"%3\". It ships with FEMMX; "
        "if this is a development build, it may not have been built.")
                       .arg(ProblemKind::displayName(problem.kind), exeName, solverDir());
    return false;
  }

  QProcess solver;
  solver.setWorkingDirectory(workingDir);
  // Same argument convention as the classic GUI: the root path with no
  // extension, and nothing else.
  //
  // The classic call sites append "bLinehook" when their load monitor is
  // open, which makes the solver report progress back through a hook.
  // femmqt's Load Monitor samples the process from outside instead (see
  // LoadMonitorDialog), so it does not need the solver's cooperation and
  // the argument is deliberately not passed.
  solver.start(exePath, QStringList{ rootPath });
  if (!solver.waitForStarted(10000)) {
    errorMessage = QStringLiteral("Could not start the %1 solver \"%2\".")
                       .arg(ProblemKind::displayName(problem.kind), exeName);
    return false;
  }
  waitPumpingEvents(solver);

  if (solver.exitStatus() != QProcess::NormalExit) {
    errorMessage = QStringLiteral("%1 terminated abnormally.").arg(exeName);
    return false;
  }

  const int code = solver.exitCode();
  if (code == 0)
    return true;
  errorMessage = QStringLiteral("%1: %2").arg(exeName, SolveRunner::exitMessage(problem.kind, code));
  return false;
}
