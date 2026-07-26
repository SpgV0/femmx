#include "ThermalPointPropDialog.h"

#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

ThermalPointPropDialog::ThermalPointPropDialog(FemmThermalPointProp& prop, QWidget* parent)
    : QDialog(parent)
    , m_prop(prop)
{
  setWindowTitle("Point Property (Heat Flow)");

  auto* form = new QFormLayout;
  m_name = new QLineEdit(prop.name, this);
  form->addRow("Name:", m_name);

  m_tp = new QLineEdit(QString::number(prop.Tp, 'g', 17), this);
  m_tp->setValidator(new QDoubleValidator(m_tp));
  form->addRow("Tp (K):", m_tp);

  m_qp = new QLineEdit(QString::number(prop.qp, 'g', 17), this);
  m_qp->setValidator(new QDoubleValidator(m_qp));
  form->addRow("qp (W):", m_qp);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &ThermalPointPropDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(buttons);
}

void ThermalPointPropDialog::onAccept()
{
  if (m_name->text().trimmed().isEmpty()) {
    QMessageBox::warning(this, "Invalid Value", "Name cannot be empty.");
    return;
  }
  m_prop.name = m_name->text().trimmed();
  m_prop.Tp = m_tp->text().toDouble();
  m_prop.qp = m_qp->text().toDouble();
  accept();
}
