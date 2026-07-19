#pragma once

#include <QDialog>

#include "FemmProblem.h"

class QLineEdit;
class QComboBox;
class QLabel;

// Edits a single FemmMaterialProp in place. muX/muY are still shown and
// editable even for a material with BH data (edited via "Edit BH
// Curve..." -> BHCurveDialog), since a note next to them clarifies
// they're ignored by the solver whenever bhData is non-empty (matching
// fkn.exe's own precedence), rather than hiding fields a saved file might
// already have meaningful values in.
class MaterialPropDialog : public QDialog {
  Q_OBJECT

  public:
  explicit MaterialPropDialog(FemmMaterialProp& prop, QWidget* parent = nullptr);

  private slots:
  void onAccept();
  void onEditBhCurve();

  private:
  void updateBhNote();

  FemmMaterialProp& m_prop;

  QLineEdit* m_name = nullptr;
  QLineEdit* m_muX = nullptr;
  QLineEdit* m_muY = nullptr;
  QLabel* m_bhNote = nullptr;
  QLineEdit* m_hc = nullptr;
  QLineEdit* m_hcAngle = nullptr;
  QLineEdit* m_jsrcRe = nullptr;
  QLineEdit* m_jsrcIm = nullptr;
  QLineEdit* m_sigma = nullptr;
  QLineEdit* m_dLam = nullptr;
  QLineEdit* m_phiH = nullptr;
  QLineEdit* m_phiHx = nullptr;
  QLineEdit* m_phiHy = nullptr;
  QComboBox* m_lamType = nullptr;
  QLineEdit* m_lamFill = nullptr;
  QLineEdit* m_nStrands = nullptr;
  QLineEdit* m_wireD = nullptr;
};
