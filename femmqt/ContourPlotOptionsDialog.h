#pragma once

#include <QDialog>

#include "SolutionView.h"

class QCheckBox;
class QLineEdit;
class QRadioButton;

// Qt port of femm/CPlotDlg.h/CPlotDlg2.h's CCPlotDlg/CCplotDlg2 (IDD_
// CPLOTDLG/IDD_CPLOTDLG2, femm/FemmviewView.cpp's OnCplot -- picks
// whichever of the two based on pDoc->Frequency). Number of Contours and
// the Lower/Upper Bound range are common to both; "Real component of A" /
// "Imaginary component of A" as two independent checkboxes is the AC
// case only -- the DC dialog has a single "Show flux lines" toggle
// instead, which this dialog doesn't reproduce as a separate control
// since Contour Plot's own View-menu/toolbar toggle already is that
// on/off switch (duplicating it here would just be two controls for one
// state, the same reasoning Density Plot Options already applies to
// skipping IDD_CPLOTDLG2's sibling "Show Density Plot" checkbox). Re(A)
// is therefore always shown while this item is in Contour mode; the
// "Show Imaginary Component" checkbox here only ever ADDS the Im(A)
// lines alongside it, and is disabled entirely for a DC solution (no
// Im(A) to show). "Stress Tensor Mask" isn't ported -- see this dialog's
// header .cpp comment on why that's grouped with the still-deferred
// Force/Torque (Block Integrals) feature rather than attempted alone.
class ContourPlotOptionsDialog : public QDialog {
  Q_OBJECT

  public:
  // isAcSolution: whether to offer the Imaginary Component checkbox at
  // all (matches classic's own IDD_CPLOTDLG-vs-IDD_CPLOTDLG2 dialog
  // choice, gated the same way -- SolutionWindow's m_frequency != 0).
  ContourPlotOptionsDialog(MeshSolutionItem* item, bool isAcSolution, QWidget* parent = nullptr);

  private slots:
  void onAccept();
  void updateFieldsEnabled();

  private:
  MeshSolutionItem* m_item;
  QLineEdit* m_numContours = nullptr;
  QCheckBox* m_showImag = nullptr;
  QRadioButton* m_automatic = nullptr;
  QRadioButton* m_customRange = nullptr;
  QLineEdit* m_min = nullptr;
  QLineEdit* m_max = nullptr;
};
