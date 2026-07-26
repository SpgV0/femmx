#pragma once

#include "ThermalMaterialLibraryIO.h"

#include <QDialog>

class QTreeWidget;
class QTreeWidgetItem;
struct FemmProblem;

// Heat flow's counterpart to MaterialLibraryDialog -- browses
// bin/heatlib.dat's folder tree and copies a selected material into the
// current problem's thermalMaterialProps.
class ThermalMaterialLibraryDialog : public QDialog {
  Q_OBJECT

  public:
  ThermalMaterialLibraryDialog(FemmProblem& problem, QWidget* parent = nullptr);

  private slots:
  void onAddToProblem();

  private:
  void populateTree(QTreeWidgetItem* parentItem, const ThermalMaterialLibraryNode& node);

  FemmProblem& m_problem;
  QTreeWidget* m_tree = nullptr;
  ThermalMaterialLibraryNode m_root;
};
