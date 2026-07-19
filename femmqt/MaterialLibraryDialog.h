#pragma once

#include "MaterialLibraryIO.h"

#include <QDialog>

class QTreeWidget;
class QTreeWidgetItem;
struct FemmProblem;

// Qt port of femm/fe_libdlg.cpp's fe_CLibDlg -- browses bin/matlib.dat's
// folder tree and copies a selected material into the current problem's
// materialProps (matching classic FEMM's own "Add" -- library entries are
// a template copied in, not a live reference the problem keeps pointing
// at).
class MaterialLibraryDialog : public QDialog {
  Q_OBJECT

  public:
  MaterialLibraryDialog(FemmProblem& problem, QWidget* parent = nullptr);

  private slots:
  void onAddToProblem();

  private:
  void populateTree(QTreeWidgetItem* parentItem, const MaterialLibraryNode& node);

  FemmProblem& m_problem;
  QTreeWidget* m_tree = nullptr;
  MaterialLibraryNode m_root;
};
