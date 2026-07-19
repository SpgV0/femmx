#pragma once

#include <QDialog>

#include "FemmProblem.h"

class QComboBox;
class QCheckBox;
class QLineEdit;
class QPlainTextEdit;

// Qt equivalent of the classic GUI's probdlg (femm/probdlg.h/.cpp) --
// same fields (problem type, length units, frequency, smart mesh, depth,
// precision, min angle, AC solver, GPU accel, prev-solution type/path,
// comment), Qt-native controls rather than a pixel-identical clone of the
// MFC dialog layout. Coordinates (polar/cartesian) and the axisymmetric
// external-region fields (extZo/extRo/extRi) aren't exposed here either,
// matching probdlg itself -- neither is user-editable from that dialog in
// the classic GUI (only round-tripped via the .fem file / Lua scripting),
// so there's no missing "button" being skipped by leaving them out here.
class ProblemPropertiesDialog : public QDialog {
  Q_OBJECT

  public:
  explicit ProblemPropertiesDialog(FemmProblem& problem, QWidget* parent = nullptr);

  private slots:
  void onAccept();
  void onProblemTypeChanged();

  private:
  FemmProblem& m_problem;

  QComboBox* m_problemType = nullptr;
  QComboBox* m_lengthUnits = nullptr;
  QLineEdit* m_frequency = nullptr;
  QCheckBox* m_smartMesh = nullptr;
  QLineEdit* m_depth = nullptr;
  QLineEdit* m_precision = nullptr;
  QLineEdit* m_minAngle = nullptr;
  QComboBox* m_solver = nullptr;
  QCheckBox* m_gpuAccel = nullptr;
  QComboBox* m_prevType = nullptr;
  QLineEdit* m_prevSoln = nullptr;
  QPlainTextEdit* m_comment = nullptr;
};
