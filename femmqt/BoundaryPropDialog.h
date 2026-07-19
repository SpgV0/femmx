#pragma once

#include <QDialog>

#include "FemmProblem.h"

class QLineEdit;
class QComboBox;
class QStackedWidget;

// Edits a single FemmBoundaryProp in place. bdryFormat is restricted to
// the three non-periodic values this Qt GUI can mesh/solve for (0 = fixed
// A, 1 = small skin depth, 2 = mixed) -- periodic/antiperiodic (4-7) stay
// out of scope this phase (see MainWindow::hasAppliedPeriodicBoundary),
// so they're deliberately not offered as a choice here, though a boundary
// already using one (opened from a file created in the classic GUI) keeps
// its bdryFormat untouched unless the user actively picks a different
// type from this dialog.
class BoundaryPropDialog : public QDialog {
  Q_OBJECT

  public:
  explicit BoundaryPropDialog(FemmBoundaryProp& prop, QWidget* parent = nullptr);

  private slots:
  void onAccept();
  void onFormatChanged();

  private:
  FemmBoundaryProp& m_prop;
  int m_originalFormat;

  QLineEdit* m_name = nullptr;
  QComboBox* m_format = nullptr;
  QStackedWidget* m_stack = nullptr;

  // Fixed A (format 0)
  QLineEdit* m_a0 = nullptr;
  QLineEdit* m_a1 = nullptr;
  QLineEdit* m_a2 = nullptr;
  QLineEdit* m_phi = nullptr;

  // Small skin depth (format 1)
  QLineEdit* m_muSsd = nullptr;
  QLineEdit* m_sigmaSsd = nullptr;

  // Mixed (format 2)
  QLineEdit* m_c0re = nullptr;
  QLineEdit* m_c0im = nullptr;
  QLineEdit* m_c1re = nullptr;
  QLineEdit* m_c1im = nullptr;
};
