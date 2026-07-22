#include "NodePropDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIntValidator>
#include <QLineEdit>
#include <QVBoxLayout>

namespace {
// True if every node in `nodes` shares the same value for `get`.
template <typename Getter>
bool allSame(const QVector<FemmNode*>& nodes, Getter get)
{
  for (int i = 1; i < nodes.size(); i++)
    if (get(nodes[i]) != get(nodes[0]))
      return false;
  return true;
}
} // namespace

NodePropDialog::NodePropDialog(const QVector<FemmNode*>& nodes, const FemmProblem& problem, QWidget* parent)
    : QDialog(parent)
    , m_nodes(nodes)
{
  setWindowTitle(nodes.size() == 1 ? "Node Properties" : QString("Node Properties (%1 nodes)").arg(nodes.size()));

  auto* form = new QFormLayout;

  m_pointProp = new QComboBox(this);
  m_pointProp->addItem("<None>");
  for (const FemmPointProp& pp : problem.pointProps)
    m_pointProp->addItem(pp.name);
  bool samePointProp = allSame(nodes, [](FemmNode* n) { return n->pointPropIndex; });
  m_pointProp->setCurrentIndex(samePointProp ? qBound(0, nodes.first()->pointPropIndex, problem.pointProps.size()) : 0);
  form->addRow("Point Property:", m_pointProp);

  bool sameGroup = allSame(nodes, [](FemmNode* n) { return n->inGroup; });
  m_inGroup = new QLineEdit(QString::number(sameGroup ? nodes.first()->inGroup : 0), this);
  m_inGroup->setValidator(new QIntValidator(0, 1000000, m_inGroup));
  form->addRow("In Group:", m_inGroup);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &NodePropDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(buttons);
}

void NodePropDialog::onAccept()
{
  for (FemmNode* n : m_nodes) {
    n->pointPropIndex = m_pointProp->currentIndex(); // 0 = <None>, else 1-based
    n->inGroup = m_inGroup->text().toInt();
  }
  accept();
}
