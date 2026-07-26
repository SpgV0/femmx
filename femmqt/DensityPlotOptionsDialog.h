#pragma once

#include <QDialog>

#include "SolutionView.h"

class QCheckBox;
class QLineEdit;
class QRadioButton;

// Qt port of femm/cv_DPlotDlg2.h's cvCDPlotDlg2 dialog -- Greyscale
// (m_gscale) and a custom Min/Max range (PlotBounds[quantity][0/1]) for
// whichever DensityQuantity is currently selected (femmqt picks the
// quantity itself via a separate "Density Quantity" submenu, unlike
// classic's combined combo box, so this dialog only edits the current
// one's range rather than reproducing that combo). "Automatic" maps to
// classic's default (no override) and to this app's own existing
// zoom-adaptive local-range behavior (MeshSolutionItem::paintDensity) --
// "Reset Bounds" in classic terms is just switching back to Automatic
// here, since there's no separate stored value to reset to once cleared.
class DensityPlotOptionsDialog : public QDialog {
  Q_OBJECT

  using DensityQuantity = MeshSolutionItem::DensityQuantity;

  public:
  explicit DensityPlotOptionsDialog(MeshSolutionItem* item, QWidget* parent = nullptr);

  private slots:
  void onAccept();
  void updateFieldsEnabled();

  private:
  MeshSolutionItem* m_item;
  QCheckBox* m_grayscale = nullptr;
  QRadioButton* m_automatic = nullptr;
  QRadioButton* m_customRange = nullptr;
  QLineEdit* m_min = nullptr;
  QLineEdit* m_max = nullptr;
};
