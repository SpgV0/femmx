#pragma once

#include <QDialog>

#include "FemmProblem.h"

class QLineEdit;
class QComboBox;
class QLabel;

// Edits a single FemmMaterialProp in place -- Qt equivalent of femm/
// MatDlg.h/.cpp's CMatDlg (invoked from femm/PtProp.cpp's CPtProp::OnEdit
// for PropType==2). muX/muY are still shown and editable even for a
// material with BH data (edited via "Edit BH Curve..." -> BHCurveDialog),
// since a note next to them clarifies they're ignored by the solver
// whenever bhData is non-empty (matching fkn.exe's own precedence), rather
// than hiding fields a saved file might already have meaningful values in.
//
// FemmMaterialProp::HcAngle (the .fem file's <H_cAngle> tag, femm/
// NOSEBL.H's Theta_m) is deliberately NOT a field in this dialog -- CMatDlg
// itself has no control for it either (confirmed against IDD_MATDLG's
// resource template and CMatDlg::DoDataExchange, neither mentions it); the
// only place it's read is FemmeDoc.cpp's legacy region-attribute loader,
// which uses it to seed a freshly-created block's MagDir. It's still kept
// on FemmMaterialProp and round-tripped by FemmFileIO/FemxFileIO/
// MaterialLibraryIO so a real .fem's <H_cAngle> value survives an open/
// save cycle unchanged, exactly as classic's own edit-via-CMatDlg leaves
// it untouched too.
//
// Known, deliberately deferred gap: classic's IDC_JR is ONE wide edit
// field for the complex Jsrc (StdAfx.h #defines every DDX_Text to
// Lua_DDX_Text, so it actually accepts an arbitrary Lua expression
// evaluated to a complex number, not just "re,im" text) -- femmqt has no
// Lua interpreter linked in (same scope note as MagDirFctn/BlockLabelProp
// Dialog), so this stays two plain QLineEdits (jsrcRe/jsrcIm) rather than
// a bespoke complex-literal parser for a field that's rarely non-real in
// practice.
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
