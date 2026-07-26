#pragma once

#include <QDialog>

#include "FemmProblem.h"

class QLineEdit;

// Edits a single FemmThermalPointProp in place -- fields match
// femm/hd_nosebl.h's CPointProp (prescribed nodal temperature Tp, point
// heat generation qp). Heat-flow counterpart to PointPropDialog.
class ThermalPointPropDialog : public QDialog {
  Q_OBJECT

  public:
  explicit ThermalPointPropDialog(FemmThermalPointProp& prop, QWidget* parent = nullptr);

  private slots:
  void onAccept();

  private:
  FemmThermalPointProp& m_prop;
  QLineEdit* m_name = nullptr;
  QLineEdit* m_tp = nullptr;
  QLineEdit* m_qp = nullptr;
};
