#pragma once

#include <QDialog>

#include "FemmProblem.h"

class QComboBox;
class QLineEdit;
class QCheckBox;

// Qt equivalent of femm/OpArcSegDlg.h/.cpp -- boundary property, max
// degrees per mesh element side, hidden flag, group. arcLength (the
// included angle) is a geometric property set when the arc is drawn, not
// edited here -- matching COpArcSegDlg, which doesn't expose it either.
class ArcPropDialog : public QDialog {
  Q_OBJECT

  public:
  ArcPropDialog(FemmArcSegment& arc, const FemmProblem& problem, QWidget* parent = nullptr);

  private slots:
  void onAccept();

  private:
  FemmArcSegment& m_arc;
  QComboBox* m_boundary = nullptr;
  QLineEdit* m_maxSeg = nullptr;
  QCheckBox* m_hidden = nullptr;
  QLineEdit* m_inGroup = nullptr;
};
