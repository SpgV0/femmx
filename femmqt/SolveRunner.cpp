#include "SolveRunner.h"

#include "FemmProblem.h"
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

bool SolveRunner::solve(const FemmProblem& problem, const QString& femPath, QString& errorMessage)
{
  QFileInfo fi(femPath);
  QString rootPath = fi.absolutePath() + "/" + fi.completeBaseName();
  QString workingDir = fi.absolutePath();

  if (!mesh(problem, femPath, errorMessage))
    return false;

  QProcess fkn;
  fkn.setWorkingDirectory(workingDir);
  fkn.start(solverDir() + "/fkn.exe", QStringList{ rootPath });
  if (!fkn.waitForStarted(10000)) {
    errorMessage = "Problem executing the solver.";
    return false;
  }
  waitPumpingEvents(fkn);

  if (fkn.exitStatus() != QProcess::NormalExit) {
    errorMessage = "fkn.exe terminated abnormally.";
    return false;
  }

  // Exit code mapping mirrors femm/FemmeView.cpp:2804-2817 exactly.
  switch (fkn.exitCode()) {
  case 0: return true;
  case 1: errorMessage = "Material properties have not been defined for all regions"; return false;
  case 2: errorMessage = "problem loading mesh"; return false;
  case 3: errorMessage = "problem renumbering node points"; return false;
  case 4: errorMessage = "couldn't allocate enough space for matrices"; return false;
  case 5: errorMessage = "Couldn't solve the problem"; return false;
  case 6: errorMessage = "couldn't write results to disk"; return false;
  case 7: errorMessage = "problem loading input file"; return false;
  default: errorMessage = QStringLiteral("fkn.exe exited with unrecognized code %1").arg(fkn.exitCode()); return false;
  }
}
