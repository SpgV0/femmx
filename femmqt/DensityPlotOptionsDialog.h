#pragma once

#include <QDialog>
#include <QVector>

#include "SolutionView.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QRadioButton;

// Qt port of femm/cv_DPlotDlg2.h's cvCDPlotDlg2 dialog (the one
// FemmviewView.cpp -- the magnetics postprocessor -- actually uses; NOT
// bv_DPlotDlg2, which is belasolv/electrostatics' own copy) -- unlike the
// prior version of this class (which only edited the range for whatever
// quantity was already selected via a separate "Density Quantity"
// submenu), this now also carries the quantity selector itself
// (m_dplottype's combo box) directly in the dialog, per direct user
// request ("a dialog box when clicking the density plot icon, similar
// to the classical gui, to set the different quantities and plotting
// options and range"). SolutionWindow's "Density Quantity" submenu was
// removed as part of this -- see SolutionWindow's menu-construction
// comment. The quantity list (10 entries, no "A"/vector-potential
// option, matching classic which doesn't offer one here either) matches
// cv_DPlotDlg2.cpp's OnInitDialog listtype==2 (AC) case exactly;
// listtype==1 (DC, no Re/Im split) is now also ported -- see
// isAcSolution below, and kDcQuantityOrder in the .cpp.
//
// Also carries the "Show Legend" checkbox (IDC_CV_SHOW_LEG2 in
// IDD_CV_DPLOTDLG2) -- SolutionWindow used to expose this as its own
// top-level View > Show Legend toggle, which isn't where classic puts
// it; moved here to match femm.rc's real dialog layout.
class DensityPlotOptionsDialog : public QDialog {
  Q_OBJECT

  using DensityQuantity = MeshSolutionItem::DensityQuantity;

  public:
  // Kept in sync by hand with DensityQuantity's value count, same as
  // MeshSolutionItem's own arrays (see that enum's comment). Public so
  // the .cpp's file-scope quantity-label table can size itself off it.
  static constexpr int kNumQuantities = 10;

  // isAcSolution selects which of classic's two listtype combos to show
  // (matches ContourPlotOptionsDialog's identically-named parameter and
  // its caller, SolutionWindow::onDensityOptionsTriggered) -- AC gets all
  // 10 quantities, DC only the 4 that make sense without a Re/Im split.
  explicit DensityPlotOptionsDialog(MeshSolutionItem* item, bool legendVisible, bool isAcSolution, QWidget* parent = nullptr);

  bool legendVisible() const { return m_showLegend; }

  private slots:
  void onAccept();
  void onQuantityChanged(int index);
  void updateFieldsEnabled();

  private:
  void saveFieldsToLocal(int qIndex);
  void loadFieldsFromLocal(int qIndex);

  MeshSolutionItem* m_item;
  // Maps the combo's visible position -> the real DensityQuantity index --
  // identity (0..9) when isAcSolution, a filtered 4-entry list otherwise.
  // Needed because DC hides 6 of the 10 quantities, so combo position and
  // quantity index diverge (see the constructor and onQuantityChanged).
  QVector<int> m_comboToQuantity;
  QComboBox* m_quantityCombo = nullptr;
  QCheckBox* m_grayscale = nullptr;
  QCheckBox* m_legendCheck = nullptr;
  QRadioButton* m_automatic = nullptr;
  QRadioButton* m_customRange = nullptr;
  QLineEdit* m_min = nullptr;
  QLineEdit* m_max = nullptr;

  int m_currentQtyIndex = 0;
  bool m_useCustom[kNumQuantities] = {};
  double m_lo[kNumQuantities] = {};
  double m_hi[kNumQuantities] = {};
  bool m_showLegend = true;
};
