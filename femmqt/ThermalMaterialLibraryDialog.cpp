#include "ThermalMaterialLibraryDialog.h"

#include "FemmProblem.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
QString uniqueMaterialName(const QVector<FemmThermalMaterialProp>& list, const QString& base)
{
  QSet<QString> existing;
  for (const FemmThermalMaterialProp& m : list)
    existing.insert(m.name);
  if (!existing.contains(base))
    return base;
  for (int n = 2;; n++) {
    QString candidate = QString("%1 (%2)").arg(base).arg(n);
    if (!existing.contains(candidate))
      return candidate;
  }
}
} // namespace

ThermalMaterialLibraryDialog::ThermalMaterialLibraryDialog(FemmProblem& problem, QWidget* parent)
    : QDialog(parent)
    , m_problem(problem)
{
  setWindowTitle("Heat Flow Materials Library");
  resize(420, 520);

  QString error;
  QString path = QCoreApplication::applicationDirPath() + "/heatlib.dat";
  ThermalMaterialLibraryIO::load(path, m_root, error);

  auto* layout = new QVBoxLayout(this);

  m_tree = new QTreeWidget(this);
  m_tree->setHeaderHidden(true);
  populateTree(nullptr, m_root);
  connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &ThermalMaterialLibraryDialog::onAddToProblem);
  layout->addWidget(m_tree, 1);

  auto* buttons = new QDialogButtonBox(this);
  auto* addButton = buttons->addButton("Add to Problem", QDialogButtonBox::ActionRole);
  connect(addButton, &QPushButton::clicked, this, &ThermalMaterialLibraryDialog::onAddToProblem);
  buttons->addButton(QDialogButtonBox::Close);
  connect(buttons, &QDialogButtonBox::clicked, this, [this, buttons](QAbstractButton* b) {
    if (buttons->buttonRole(b) != QDialogButtonBox::ActionRole)
      accept();
  });
  layout->addWidget(buttons);

  if (!error.isEmpty()) {
    QMessageBox::warning(this, "Heat Flow Materials Library", error);
  }
}

void ThermalMaterialLibraryDialog::populateTree(QTreeWidgetItem* parentItem, const ThermalMaterialLibraryNode& node)
{
  for (const ThermalMaterialLibraryNode& child : node.children) {
    QTreeWidgetItem* item = parentItem
        ? new QTreeWidgetItem(parentItem, { child.name })
        : new QTreeWidgetItem(m_tree, { child.name });
    item->setData(0, Qt::UserRole, QVariant::fromValue<void*>(const_cast<ThermalMaterialLibraryNode*>(&child)));
    if (child.isFolder)
      populateTree(item, child);
  }
}

void ThermalMaterialLibraryDialog::onAddToProblem()
{
  QTreeWidgetItem* item = m_tree->currentItem();
  if (!item)
    return;
  const auto* node = static_cast<const ThermalMaterialLibraryNode*>(item->data(0, Qt::UserRole).value<void*>());
  if (!node || node->isFolder)
    return;

  FemmThermalMaterialProp m = node->material;
  m.name = uniqueMaterialName(m_problem.thermalMaterialProps, m.name);
  m_problem.thermalMaterialProps.push_back(m);
  QMessageBox::information(this, "Heat Flow Materials Library", QString("Added \"%1\" to the problem.").arg(m.name));
}
