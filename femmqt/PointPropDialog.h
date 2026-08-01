#pragma once

#include <QDialog>

#include "FemmProblem.h"

class QLineEdit;
class QRadioButton;

// Edits a single FemmPointProp in place -- Qt equivalent of femm/
// NodeProp.h/.cpp's CNodeProp (invoked from femm/PtProp.cpp's
// CPtProp::OnModProp for PropType==0). Matches its "Specified Potential
// Property" / "Point Current Property" radio choice exactly: classic's
// OnSetA/OnSetI zero and disable whichever pair (A or J) isn't the
// currently-selected kind, so a point property is always EITHER a
// prescribed-A property OR a point-current property, never both at once
// -- not two independently-editable field pairs.
class PointPropDialog : public QDialog {
  Q_OBJECT

  public:
  explicit PointPropDialog(FemmPointProp& prop, QWidget* parent = nullptr);

  private slots:
  void onAccept();
  void updateFieldsEnabled();

  private:
  FemmPointProp& m_prop;
  QLineEdit* m_name = nullptr;
  QRadioButton* m_setA = nullptr;
  QRadioButton* m_setI = nullptr;
  QLineEdit* m_jr = nullptr;
  QLineEdit* m_ji = nullptr;
  QLineEdit* m_ar = nullptr;
  QLineEdit* m_ai = nullptr;
};
