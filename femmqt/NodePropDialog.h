#pragma once

#include <QDialog>
#include <QVector>

#include "FemmProblem.h"

class QComboBox;
class QLineEdit;

// Per-node "which point property (if any) do these nodes use" dialog --
// Qt equivalent of femm/OpNodeDlg.h/.cpp. Distinct from PointPropDialog,
// which edits a point property's own field values in the library; this
// one just picks which library entry (or none) the given node(s)
// reference, plus their group number. Operates on one OR MORE nodes at
// once (matching classic FEMM's CFemmeDoc::OpNodeDlg, which applies the
// dialog's chosen values to every currently-selected node) -- when the
// selected nodes don't already share the same point property (or group),
// the field starts at a neutral default ("<None>" / 0) rather than an
// arbitrary single node's value, so accepting the dialog without
// touching that field can't silently overwrite a meaningfully different
// existing assignment across the batch.
class NodePropDialog : public QDialog {
  Q_OBJECT

  public:
  NodePropDialog(const QVector<FemmNode*>& nodes, const FemmProblem& problem, QWidget* parent = nullptr);

  private slots:
  void onAccept();

  private:
  QVector<FemmNode*> m_nodes;
  QComboBox* m_pointProp = nullptr;
  QLineEdit* m_inGroup = nullptr;
};
