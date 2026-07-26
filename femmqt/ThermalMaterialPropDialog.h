#pragma once

#include <QDialog>

#include "FemmProblem.h"

class QLineEdit;
class QLabel;

// Edits a single FemmThermalMaterialProp in place. Kx/Ky are still shown
// and editable even for a material with nonlinear k(T) data, since a note
// next to them clarifies they're ignored by hsolv.exe whenever tkData is
// non-empty -- mirrors MaterialPropDialog's identical BH-curve precedent.
// Unlike MaterialPropDialog, there's no "Edit TK Curve..." button yet --
// tkData is read/written (round-tripped) but not editable this round,
// matching FemmThermalMaterialProp::tkData's own comment.
class ThermalMaterialPropDialog : public QDialog {
  Q_OBJECT

  public:
  explicit ThermalMaterialPropDialog(FemmThermalMaterialProp& prop, QWidget* parent = nullptr);

  private slots:
  void onAccept();

  private:
  FemmThermalMaterialProp& m_prop;

  QLineEdit* m_name = nullptr;
  QLineEdit* m_kx = nullptr;
  QLineEdit* m_ky = nullptr;
  QLabel* m_tkNote = nullptr;
  QLineEdit* m_kt = nullptr;
  QLineEdit* m_qv = nullptr;
};
