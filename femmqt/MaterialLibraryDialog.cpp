#include "MaterialLibraryDialog.h"

#include "FemmProblem.h"
#include "ProblemKind.h"

#include <QAbstractButton>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>


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

  // A search box, a details line and a status line (#119): the library
  // had none, and every Add popped a modal "Added X" box to dismiss.
  m_search = new QLineEdit(this);
  m_search->setObjectName("materialSearch");
  m_search->setPlaceholderText("Search materials...");
  m_search->setClearButtonEnabled(true);
  layout->addWidget(m_search);

  m_tree = new QTreeWidget(this);
  m_tree->setHeaderHidden(true);
  populateTree(nullptr, m_root);
  connect(m_tree, &QTreeWidget::itemDoubleClicked, this, &MaterialLibraryDialog::onAddToProblem);
  connect(m_tree, &QTreeWidget::currentItemChanged, this, &MaterialLibraryDialog::onCurrentChanged);
  connect(m_search, &QLineEdit::textChanged, this, &MaterialLibraryDialog::applyFilter);
  layout->addWidget(m_tree, 1);

  m_details = new QLabel(this);
  m_details->setObjectName("materialDetails");
  m_details->setWordWrap(true);
  m_details->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_details);
  m_status = new QLabel(this);
  m_status->setObjectName("materialStatus");
  layout->addWidget(m_status);

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
  // Said in the dialog, not in a modal box to dismiss after every add.
  m_status->setText(QStringLiteral("Added \"%1\" to the problem.")
                        .arg(ProblemKind::name(m_problem, ProblemKind::Category::Material, index)));
}

// Shows only materials whose name contains `text` (case-insensitive),
// with the folders leading to them, expanded.
void MaterialLibraryDialog::applyFilter(const QString& text)
{
  const QString needle = text.trimmed();
  std::function<bool(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
    bool any = false;
    for (int i = 0; i < item->childCount(); i++)
      any = visit(item->child(i)) || any;
    const auto* node = static_cast<const MaterialLibraryNode*>(item->data(0, Qt::UserRole).value<void*>());
    const bool leafMatch = node && !node->isFolder && (needle.isEmpty() || item->text(0).contains(needle, Qt::CaseInsensitive));
    const bool show = leafMatch || any || (needle.isEmpty() && node && node->isFolder);
    item->setHidden(!show);
    if (!needle.isEmpty() && any)
      item->setExpanded(true);
    return leafMatch || any;
  };
  for (int i = 0; i < m_tree->topLevelItemCount(); i++)
    visit(m_tree->topLevelItem(i));
}

// The selected material's defining numbers, in this physics' terms.
void MaterialLibraryDialog::onCurrentChanged()
{
  QTreeWidgetItem* item = m_tree->currentItem();
  const auto* node = item ? static_cast<const MaterialLibraryNode*>(item->data(0, Qt::UserRole).value<void*>()) : nullptr;
  if (!node || node->isFolder) {
    m_details->clear();
    return;
  }
  auto g = [](double v) { return QString::number(v, 'g', 4); };
  switch (m_problem.kind) {
  case FemmProblemKind::Magnetics: {
    const FemmMaterialProp& m = node->material;
    QStringList parts;
    parts << (m.bhData.isEmpty() ? QStringLiteral("mu_r %1, %2").arg(g(m.muX), g(m.muY))
                                 : QStringLiteral("nonlinear B-H curve (%1 points)").arg(m.bhData.size()));
    parts << QStringLiteral("sigma %1 MS/m").arg(g(m.sigma));
    if (m.Hc != 0)
      parts << QStringLiteral("Hc %1 A/m").arg(g(m.Hc));
    if (m.JsrcRe != 0 || m.JsrcIm != 0)
      parts << QStringLiteral("J %1 MA/m^2").arg(g(m.JsrcRe));
    if (m.lamType != 0 || m.dLam != 0)
      parts << QStringLiteral("laminated");
    m_details->setText(parts.join(QStringLiteral(" · ")));
    break;
  }
  case FemmProblemKind::Electrostatics:
    m_details->setText(QStringLiteral("e_r %1, %2 · q_v %3 C/m^3").arg(g(node->esMaterial.ex), g(node->esMaterial.ey), g(node->esMaterial.qv)));
    break;
  case FemmProblemKind::HeatFlow:
    m_details->setText(QStringLiteral("k %1, %2 W/(m K) · q_v %3 W/m^3").arg(g(node->htMaterial.Kx), g(node->htMaterial.Ky), g(node->htMaterial.qv)));
    break;
  case FemmProblemKind::CurrentFlow:
    m_details->setText(QStringLiteral("sigma %1, %2 S/m · e_r %3, %4").arg(g(node->cfMaterial.ox), g(node->cfMaterial.oy), g(node->cfMaterial.ex), g(node->cfMaterial.ey)));
    break;
  }
}
