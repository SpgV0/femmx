#include "MaterialLibraryDialog.h"

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
// Same disambiguation convention as MainWindow.cpp's own uniqueName() --
// duplicated locally rather than shared since it's a 10-line template and
// this dialog otherwise has no dependency on MainWindow.
QString uniqueMaterialName(const QVector<FemmMaterialProp>& list, const QString& base)
{
  QSet<QString> existing;
  for (const FemmMaterialProp& m : list)
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

MaterialLibraryDialog::MaterialLibraryDialog(FemmProblem& problem, QWidget* parent)
    : QDialog(parent)
    , m_problem(problem)
{
  setWindowTitle("Materials Library");
  resize(420, 520);

  QString error;
  QString path = QCoreApplication::applicationDirPath() + "/matlib.dat";
  MaterialLibraryIO::load(path, m_root, error);

  auto* layout = new QVBoxLayout(this);

  m_tree = new QTreeWidget(this);
  m_tree->setHeaderHidden(true);
  populateTree(nullptr, m_root);
  connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &MaterialLibraryDialog::onAddToProblem);
  layout->addWidget(m_tree, 1);

  auto* buttons = new QDialogButtonBox(this);
  auto* addButton = buttons->addButton("Add to Problem", QDialogButtonBox::ActionRole);
  connect(addButton, &QPushButton::clicked, this, &MaterialLibraryDialog::onAddToProblem);
  buttons->addButton(QDialogButtonBox::Close);
  connect(buttons, &QDialogButtonBox::clicked, this, [this, buttons](QAbstractButton* b) {
    if (buttons->buttonRole(b) != QDialogButtonBox::ActionRole)
      accept();
  });
  layout->addWidget(buttons);

  if (!error.isEmpty()) {
    QMessageBox::warning(this, "Materials Library", error);
  }
}

void MaterialLibraryDialog::populateTree(QTreeWidgetItem* parentItem, const MaterialLibraryNode& node)
{
  for (const MaterialLibraryNode& child : node.children) {
    QTreeWidgetItem* item = parentItem
        ? new QTreeWidgetItem(parentItem, { child.name })
        : new QTreeWidgetItem(m_tree, { child.name });
    item->setData(0, Qt::UserRole, QVariant::fromValue<void*>(const_cast<MaterialLibraryNode*>(&child)));
    if (child.isFolder)
      populateTree(item, child);
  }
}

void MaterialLibraryDialog::onAddToProblem()
{
  QTreeWidgetItem* item = m_tree->currentItem();
  if (!item)
    return;
  const auto* node = static_cast<const MaterialLibraryNode*>(item->data(0, Qt::UserRole).value<void*>());
  if (!node || node->isFolder)
    return;

  FemmMaterialProp m = node->material;
  m.name = uniqueMaterialName(m_problem.materialProps, m.name);
  m_problem.materialProps.push_back(m);
  QMessageBox::information(this, "Materials Library", QString("Added \"%1\" to the problem.").arg(m.name));
}
