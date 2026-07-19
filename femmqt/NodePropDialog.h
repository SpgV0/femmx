#pragma once

#include <QDialog>

#include "FemmProblem.h"

class QComboBox;
class QLineEdit;

// Per-node "which point property (if any) does this node use" dialog --
// Qt equivalent of femm/OpNodeDlg.h/.cpp. Distinct from PointPropDialog,
// which edits a point property's own field values in the library; this
// one just picks which library entry (or none) a specific node
// references, plus its group number.
class NodePropDialog : public QDialog {
  Q_OBJECT

  public:
  NodePropDialog(FemmNode& node, const FemmProblem& problem, QWidget* parent = nullptr);

  private slots:
  void onAccept();

  private:
  FemmNode& m_node;
  QComboBox* m_pointProp = nullptr;
  QLineEdit* m_inGroup = nullptr;
};
