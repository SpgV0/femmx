#include "PointPropDialog.h"

#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

PointPropDialog::PointPropDialog(FemmPointProp& prop, QWidget* parent)
    : QDialog(parent)
    , m_prop(prop)
{
  setWindowTitle("Point Property");

  auto* form = new QFormLayout;
  m_name = new QLineEdit(prop.name, this);
  form->addRow("Name:", m_name);

  m_jr = new QLineEdit(QString::number(prop.Jr, 'g', 17), this);
  m_jr->setValidator(new QDoubleValidator(m_jr));
  form->addRow("I re (A):", m_jr);

  m_ji = new QLineEdit(QString::number(prop.Ji, 'g', 17), this);
  m_ji->setValidator(new QDoubleValidator(m_ji));
  form->addRow("I im (A):", m_ji);

  m_ar = new QLineEdit(QString::number(prop.Ar, 'g', 17), this);
  m_ar->setValidator(new QDoubleValidator(m_ar));
  form->addRow("A re:", m_ar);

  m_ai = new QLineEdit(QString::number(prop.Ai, 'g', 17), this);
  m_ai->setValidator(new QDoubleValidator(m_ai));
  form->addRow("A im:", m_ai);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &PointPropDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(buttons);
}

void PointPropDialog::onAccept()
{
  if (m_name->text().trimmed().isEmpty()) {
    QMessageBox::warning(this, "Invalid Value", "Name cannot be empty.");
    return;
  }
  m_prop.name = m_name->text().trimmed();
  m_prop.Jr = m_jr->text().toDouble();
  m_prop.Ji = m_ji->text().toDouble();
  m_prop.Ar = m_ar->text().toDouble();
  m_prop.Ai = m_ai->text().toDouble();
  accept();
}
