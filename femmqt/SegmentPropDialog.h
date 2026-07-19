#pragma once

#include <QDialog>

#include "FemmProblem.h"

class QComboBox;
class QLineEdit;
class QCheckBox;

// Qt equivalent of femm/OpSegDlg.h/.cpp -- which boundary property (if
// any) this specific segment uses, its mesh size, hidden flag, and group.
class SegmentPropDialog : public QDialog {
  Q_OBJECT

  public:
  SegmentPropDialog(FemmSegment& segment, const FemmProblem& problem, QWidget* parent = nullptr);

  private slots:
  void onAccept();
  void onAutomeshToggled(bool checked);

  private:
  FemmSegment& m_segment;
  QComboBox* m_boundary = nullptr;
  QCheckBox* m_automesh = nullptr;
  QLineEdit* m_meshSize = nullptr;
  QCheckBox* m_hidden = nullptr;
  QLineEdit* m_inGroup = nullptr;
};
