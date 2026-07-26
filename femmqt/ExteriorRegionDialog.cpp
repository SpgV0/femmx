#include "ExteriorRegionDialog.h"

#include "FemmProblem.h"

#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QLineEdit>
#include <QVBoxLayout>

ExteriorRegionDialog::ExteriorRegionDialog(FemmProblem& problem, QWidget* parent)
    : QDialog(parent)
    , m_problem(problem)
{
  setWindowTitle("Exterior Region Properties");

  auto* form = new QFormLayout;

  m_zo = new QLineEdit(QString::number(problem.extZo, 'g', 17), this);
  m_zo->setValidator(new QDoubleValidator(m_zo));
  form->addRow("Center of the Exterior Region:", m_zo);

  m_ro = new QLineEdit(QString::number(problem.extRo, 'g', 17), this);
  m_ro->setValidator(new QDoubleValidator(m_ro));
  form->addRow("Radius of the Exterior Region:", m_ro);

  m_ri = new QLineEdit(QString::number(problem.extRi, 'g', 17), this);
  m_ri->setValidator(new QDoubleValidator(m_ri));
  form->addRow("Radius of the Interior Region:", m_ri);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &ExteriorRegionDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(buttons);
}

void ExteriorRegionDialog::onAccept()
{
  m_problem.extZo = m_zo->text().toDouble();
  m_problem.extRo = m_ro->text().toDouble();
  m_problem.extRi = m_ri->text().toDouble();
  accept();
}
