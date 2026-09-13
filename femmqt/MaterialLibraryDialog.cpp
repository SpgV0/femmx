#include "MaterialLibraryDialog.h"

#include "FemmProblem.h"
#include "ProblemKind.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>


MaterialLibraryDialog::MaterialLibraryDialog(FemmProblem& problem, QWidget* parent)
    : QDialog(parent)
    , m_problem(problem)
{
  // Modified by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
  // (issue #81): the library that belongs to this document's physics.
  // Importing an electrostatics permittivity into a heat-flow model has
  // no meaning, so there is one library per kind and the document picks
  // which.
  setWindowTitle(QStringLiteral("%1 Materials Library")
                     .arg(ProblemKind::displayName(problem.kind)));
  resize(420, 520);

  QString error;
  const QString matPath = QCoreApplication::applicationDirPath() + "/"
      + MaterialLibraryIO::defaultFileName(problem.kind);
  if (!MaterialLibraryIO::load(matPath, problem.kind, m_root, error)) {
    // A missing library file and an empty one look identical in the
    // tree, so the difference is said out loud.
    QMessageBox::information(this, windowTitle(), error);
  }

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

  // #81: appendTo puts it in whichever of the four material lists this
  // document uses, and disambiguates the name against what is already
  // there -- two materials sharing a name makes the second unreachable,
  // since the writer identifies a material by name.
  const int index = MaterialLibraryIO::appendTo(m_problem, *node);
  if (index < 0)
    return;
  QMessageBox::information(this, windowTitle(),
      QStringLiteral("Added \"%1\" to the problem.")
          .arg(ProblemKind::name(m_problem, ProblemKind::Category::Material, index)));
}
