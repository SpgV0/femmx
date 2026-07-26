#pragma once

#include <QDialog>
#include <QVector>

#include "FemmProblem.h"

class QComboBox;
class QLineEdit;
class QCheckBox;

// Qt equivalent of femm/OpSegDlg.h/.cpp -- which boundary property (if
// any) these segment(s) use, their mesh size, hidden flag, and group.
// Operates on one OR MORE segments at once (matching classic FEMM's
// CFemmeDoc::OpSegDlg, which applies the dialog's chosen values to every
// currently-selected segment): boundary defaults to "<None>" rather than
// an arbitrary segment's value when the batch doesn't already agree on
// one; mesh size defaults to automesh if any selected segment is on
// automesh, else the average of their mesh sizes; hidden defaults to
// checked if any selected segment is hidden; group defaults to 0 when
// the batch doesn't already agree on one -- all matching OpSegDlg's own
// mixed-selection rules exactly.
class SegmentPropDialog : public QDialog {
  Q_OBJECT

  public:
  SegmentPropDialog(const QVector<FemmSegment*>& segments, const FemmProblem& problem, QWidget* parent = nullptr);

  private slots:
  void onAccept();
  void onAutomeshToggled(bool checked);

  private:
  QVector<FemmSegment*> m_segments;
  QComboBox* m_boundary = nullptr;
  QCheckBox* m_automesh = nullptr;
  QLineEdit* m_meshSize = nullptr;
  QCheckBox* m_hidden = nullptr;
  QLineEdit* m_inGroup = nullptr;
};
