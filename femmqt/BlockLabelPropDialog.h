#pragma once

#include <QDialog>
#include <QVector>

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
//
// Operates on one OR MORE labels at once (matching classic FEMM's
// CFemmeDoc::OpBlkDlg, which applies the dialog's chosen values to every
// currently-selected label). Where OpBlkDlg resets a mixed field to
// "<None>" (a real, applyable choice in its combo), this version can't
// do the same for material/circuit -- "<Hole>" is itself a meaningful,
// destructive choice here (see above), so silently defaulting a mixed
// batch to it on an unattended OK would turn every non-uniform selection
// into holes. Instead, a mixed material/circuit shows a non-selectable
// "<Multiple>" placeholder; accepting without changing it leaves each
// label's existing material/circuit untouched, and picking any real
// entry applies it to the whole batch same as any other field.
class BlockLabelPropDialog : public QDialog {
  Q_OBJECT

  public:
  BlockLabelPropDialog(const QVector<FemmBlockLabel*>& labels, const FemmProblem& problem, QWidget* parent = nullptr);

  private slots:
  void onAccept();
  void onMaterialChanged();

  private:
  QVector<FemmBlockLabel*> m_labels;

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

  // Index offset into m_material/m_circuit past a leading "<Multiple>"
  // placeholder -- 1 if it's present (mixed selection), 0 otherwise. Lets
  // onAccept() tell "user left it on <Multiple>" apart from "user picked
  // the same entry <Multiple> happened to sit next to."
  bool m_materialHasMultiplePlaceholder = false;
  bool m_circuitHasMultiplePlaceholder = false;
};
