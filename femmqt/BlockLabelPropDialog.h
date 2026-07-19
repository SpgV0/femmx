#pragma once

#include <QDialog>

#include "FemmProblem.h"

class QComboBox;
class QLineEdit;
class QCheckBox;

// Qt equivalent of femm/OpBlkDlg.h/.cpp. Unlike the classic GUI (which
// only ever shows this dialog for a label that's already a real block --
// holes have no property dialog at all, just x/y/group, edited by
// dragging/deleting), this version's material combo has a leading
// "<Hole>" entry that maps to blockTypeIndex -1, so toggling a label
// between "hole" and "assigned to material X" is one dropdown instead of
// two different code paths -- a small, deliberate improvement over the
// original given this is a from-scratch UI anyway.
class BlockLabelPropDialog : public QDialog {
  Q_OBJECT

  public:
  BlockLabelPropDialog(FemmBlockLabel& label, const FemmProblem& problem, QWidget* parent = nullptr);

  private slots:
  void onAccept();
  void onMaterialChanged();

  private:
  FemmBlockLabel& m_label;

  QComboBox* m_material = nullptr;
  QComboBox* m_circuit = nullptr;
  QCheckBox* m_automesh = nullptr;
  QLineEdit* m_meshSize = nullptr;
  QLineEdit* m_magDir = nullptr;
  QLineEdit* m_magDirFctn = nullptr;
  QLineEdit* m_turns = nullptr;
  QLineEdit* m_inGroup = nullptr;
  QCheckBox* m_isExternal = nullptr;
  QCheckBox* m_isDefault = nullptr;
};
