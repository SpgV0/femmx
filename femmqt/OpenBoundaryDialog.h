#pragma once

#include <QDialog>

class QComboBox;
class QLineEdit;
struct FemmProblem;

// Qt port of bin/init.lua's mi_makeABC (invoked by femm/FemmeView.cpp's
// OnMakeABC) -- draws n concentric linear-material shells around the
// existing geometry to approximate an asymptotic (Kelvin-transform)
// open boundary, using the exact same permeability lookup tables
// (u2D0/u2D1 for planar, uAx0/uAx1 for axisymmetric, n=1..12) rather than
// an approximation of them -- see OpenBoundaryDialog.cpp's copy of those
// tables, transcribed directly from bin/init.lua.
class OpenBoundaryDialog : public QDialog {
  Q_OBJECT

  public:
  OpenBoundaryDialog(FemmProblem& problem, QWidget* parent = nullptr);

  private slots:
  void onAccept();

  private:
  void buildAbc(int n, double R, double x, double y, int bcType);

  FemmProblem& m_problem;
  QLineEdit* m_numShells = nullptr;
  QLineEdit* m_radius = nullptr;
  QLineEdit* m_centerX = nullptr;
  QLineEdit* m_centerY = nullptr;
  QComboBox* m_bcType = nullptr;
};
