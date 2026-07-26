#include "NodePropDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
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

  if (nodes.size() == 1) {
    m_x = new QLineEdit(QString::number(nodes.first()->x, 'g', 12), this);
    m_x->setValidator(new QDoubleValidator(m_x));
    form->addRow("X:", m_x);

    m_y = new QLineEdit(QString::number(nodes.first()->y, 'g', 12), this);
    m_y->setValidator(new QDoubleValidator(m_y));
    form->addRow("Y:", m_y);
  }

  m_pointProp = new QComboBox(this);
  m_pointProp->addItem("<None>");
  for (const FemmPointProp& pp : problem.pointProps)
    m_pointProp->addItem(pp.name);
  bool samePointProp = allSame(nodes, [](FemmNode* n) { return n->pointPropIndex; });
  m_pointProp->setCurrentIndex(samePointProp ? qBound(0, nodes.first()->pointPropIndex, problem.pointProps.size()) : 0);
  form->addRow("Point Property:", m_pointProp);

  m_thermalPointProp = new QComboBox(this);
  m_thermalPointProp->addItem("<None>");
  for (const FemmThermalPointProp& pp : problem.thermalPointProps)
    m_thermalPointProp->addItem(pp.name);
  bool sameThermalPointProp = allSame(nodes, [](FemmNode* n) { return n->thermalPointPropIndex; });
  m_thermalPointProp->setCurrentIndex(sameThermalPointProp ? qBound(0, nodes.first()->thermalPointPropIndex, problem.thermalPointProps.size()) : 0);
  form->addRow("Point Property (Heat Flow):", m_thermalPointProp);

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
  if (m_x && m_y && m_nodes.size() == 1) {
    m_nodes.first()->x = m_x->text().toDouble();
    m_nodes.first()->y = m_y->text().toDouble();
  }
  for (FemmNode* n : m_nodes) {
    n->pointPropIndex = m_pointProp->currentIndex(); // 0 = <None>, else 1-based
    n->thermalPointPropIndex = m_thermalPointProp->currentIndex();
    n->inGroup = m_inGroup->text().toInt();
  }
  accept();
}
