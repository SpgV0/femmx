#pragma once

#include <QDialog>

#include "FemmProblem.h"

class QLineEdit;
class QComboBox;

// Edits a single FemmThermalBoundaryProp in place. bdryFormat is
// restricted to the four non-periodic values this Qt GUI can mesh/solve
// for (0 = Fixed Temperature, 1 = Heat Flux, 2 = Convection, 3 =
// Radiation, matching femm/hd_BdryDlg.cpp's IDC_HD_BDRYFORMAT combo box
// exactly) -- periodic/antiperiodic (4, 5) stay out of scope this phase,
// same reasoning as BoundaryPropDialog's own restriction to 0-2. A
// boundary already using one of those (opened from a classic-GUI file)
// keeps its bdryFormat untouched unless the user actively picks a
// different type here.
//
// Unlike BoundaryPropDialog (magnetics' 3 formats have disjoint field
// sets, so that dialog uses a QStackedWidget page per format), heat
// flow's 4 formats share one growing field set -- Tset alone for Fixed
// Temperature, +qs for Heat Flux, +h/Tinf for Convection, +beta/TinfRad
// for Radiation -- so this mirrors hd_BdryDlg.cpp's own approach
// directly: one flat form, enabling/disabling fields per format instead
// of switching pages.
class ThermalBoundaryPropDialog : public QDialog {
  Q_OBJECT

  public:
  explicit ThermalBoundaryPropDialog(FemmThermalBoundaryProp& prop, QWidget* parent = nullptr);

  private slots:
  void onAccept();
  void onFormatChanged();

  private:
  FemmThermalBoundaryProp& m_prop;

  QLineEdit* m_name = nullptr;
  QComboBox* m_format = nullptr;

  QLineEdit* m_tset = nullptr;
  QLineEdit* m_qs = nullptr;
  QLineEdit* m_h = nullptr;
  QLineEdit* m_tinf = nullptr;
  QLineEdit* m_beta = nullptr;
  QLineEdit* m_tinfRad = nullptr;
};
