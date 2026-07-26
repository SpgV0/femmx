#pragma once

#include <QDialog>

#include "FemmProblem.h"

class QLineEdit;
class QComboBox;
class QLabel;

// Edits a single FemmMaterialProp in place -- both its magnetic AND
// thermal properties, since a material now carries both (see
// FemmMaterialProp's own comment, Round 6: one materials library shared
// between magnetics and heat flow, per direct user request). muX/muY are
// still shown and editable even for a material with BH data (edited via
// "Edit BH Curve..." -> BHCurveDialog), since a note next to them
// clarifies they're ignored by the solver whenever bhData is non-empty
// (matching fkn.exe's own precedence), rather than hiding fields a saved
// file might already have meaningful values in -- Kx/Ky and tkData have
// the identical relationship on the thermal side.
class MaterialPropDialog : public QDialog {
  Q_OBJECT

  public:
  explicit MaterialPropDialog(FemmMaterialProp& prop, QWidget* parent = nullptr);

  private slots:
  void onAccept();
  void onEditBhCurve();

  private:
  void updateBhNote();
  void updateTkNote();

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

  // Heat-flow fields -- see FemmMaterialProp's comment on why 0 means
  // "no thermal data" rather than a fabricated default.
  QLineEdit* m_kx = nullptr;
  QLineEdit* m_ky = nullptr;
  QLabel* m_tkNote = nullptr;
  QLineEdit* m_kt = nullptr;
  QLineEdit* m_qv = nullptr;
};
