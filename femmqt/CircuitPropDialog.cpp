#include "CircuitPropDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

CircuitPropDialog::CircuitPropDialog(FemmCircuitProp& prop, QWidget* parent)
    : QDialog(parent)
    , m_prop(prop)
{
  setWindowTitle("Circuit Property");

  auto* form = new QFormLayout;
  m_name = new QLineEdit(prop.name, this);
  form->addRow("Name:", m_name);

  m_ampsRe = new QLineEdit(QString::number(prop.ampsRe, 'g', 17), this);
  m_ampsRe->setValidator(new QDoubleValidator(m_ampsRe));
  form->addRow("Total Amps re (A):", m_ampsRe);

  m_ampsIm = new QLineEdit(QString::number(prop.ampsIm, 'g', 17), this);
  m_ampsIm->setValidator(new QDoubleValidator(m_ampsIm));
  form->addRow("Total Amps im (A):", m_ampsIm);

  m_circType = new QComboBox(this);
  m_circType->addItems({ "Parallel", "Series" });
  m_circType->setCurrentIndex(prop.circType == 1 ? 1 : 0);
  form->addRow("Circuit Type:", m_circType);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &CircuitPropDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(buttons);
}

void CircuitPropDialog::onAccept()
{
  if (m_name->text().trimmed().isEmpty()) {
    QMessageBox::warning(this, "Invalid Value", "Name cannot be empty.");
    return;
  }
  m_prop.name = m_name->text().trimmed();
  m_prop.ampsRe = m_ampsRe->text().toDouble();
  m_prop.ampsIm = m_ampsIm->text().toDouble();
  m_prop.circType = m_circType->currentIndex();
  accept();
}
