#pragma once

#include <QDialog>
#include <QVector>

#include "FemmProblem.h"

class QComboBox;
class QLineEdit;
class QCheckBox;

// Qt equivalent of femm/OpArcSegDlg.h/.cpp -- boundary property, max
// degrees per mesh element side, hidden flag, group. arcLength (the
// included angle) is a geometric property set when the arc is drawn, not
// edited here -- matching COpArcSegDlg, which doesn't expose it either.
// Operates on one OR MORE arcs at once (matching classic FEMM's
// CFemmeDoc::OpArcSegDlg, which applies the dialog's chosen values to
// every currently-selected arc): boundary defaults to "<None>" rather
// than an arbitrary arc's value when the batch doesn't already agree on
// one; max segment defaults to the average across the batch; hidden
// defaults to checked if any selected arc is hidden; group defaults to 0
// when the batch doesn't already agree on one -- all matching
// OpArcSegDlg's own mixed-selection rules exactly.
class ArcPropDialog : public QDialog {
  Q_OBJECT

  public:
  ArcPropDialog(const QVector<FemmArcSegment*>& arcs, const FemmProblem& problem, QWidget* parent = nullptr);

  private slots:
  void onAccept();

  private:
  QVector<FemmArcSegment*> m_arcs;
  QComboBox* m_boundary = nullptr;
  QLineEdit* m_maxSeg = nullptr;
  QCheckBox* m_hidden = nullptr;
  QLineEdit* m_inGroup = nullptr;
};
