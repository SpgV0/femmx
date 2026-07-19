#include "NodePropDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIntValidator>
#include <QLineEdit>
#include <QVBoxLayout>

NodePropDialog::NodePropDialog(FemmNode& node, const FemmProblem& problem, QWidget* parent)
    : QDialog(parent)
    , m_node(node)
{
  setWindowTitle("Node Properties");

  auto* form = new QFormLayout;

  m_pointProp = new QComboBox(this);
  m_pointProp->addItem("<None>");
  for (const FemmPointProp& pp : problem.pointProps)
    m_pointProp->addItem(pp.name);
  m_pointProp->setCurrentIndex(qBound(0, node.pointPropIndex, problem.pointProps.size()));
  form->addRow("Point Property:", m_pointProp);

  m_inGroup = new QLineEdit(QString::number(node.inGroup), this);
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
  m_node.pointPropIndex = m_pointProp->currentIndex(); // 0 = <None>, else 1-based
  m_node.inGroup = m_inGroup->text().toInt();
  accept();
}
