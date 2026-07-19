#pragma once

#include <QDialog>

class QLineEdit;
struct FemmProblem;

// Qt port of femm/ExteriorProps.h's CExteriorProps -- exactly 3 fields,
// no validation in the classic dialog either (femm/FemmeDoc.cpp's
// OnEditExterior just copies dlg.m_Ro/m_Ri/m_Zo back unconditionally on
// OK). Meaningful only for an axisymmetric problem with at least one
// block label marked "external" (see BlockLabelPropDialog); those fields
// already exist on FemmProblem (extZo/extRo/extRi), this just edits them.
class ExteriorRegionDialog : public QDialog {
  Q_OBJECT

  public:
  ExteriorRegionDialog(FemmProblem& problem, QWidget* parent = nullptr);

  private slots:
  void onAccept();

  private:
  FemmProblem& m_problem;
  QLineEdit* m_zo = nullptr;
  QLineEdit* m_ro = nullptr;
  QLineEdit* m_ri = nullptr;
};
