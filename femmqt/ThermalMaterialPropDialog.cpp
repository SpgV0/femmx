#include "ThermalMaterialPropDialog.h"

#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

namespace {
QLineEdit* makeDoubleField(QWidget* parent, QFormLayout* form, const QString& label, double value)
{
  auto* edit = new QLineEdit(QString::number(value, 'g', 17), parent);
  edit->setValidator(new QDoubleValidator(edit));
  form->addRow(label, edit);
  return edit;
}
}

ThermalMaterialPropDialog::ThermalMaterialPropDialog(FemmThermalMaterialProp& prop, QWidget* parent)
    : QDialog(parent)
    , m_prop(prop)
{
  setWindowTitle("Material Property (Heat Flow)");

  auto* form = new QFormLayout;
  m_name = new QLineEdit(prop.name, this);
  form->addRow("Name:", m_name);

  m_kx = makeDoubleField(this, form, "Kx (W/(m\xB7K)):", prop.Kx);
  m_ky = makeDoubleField(this, form, "Ky (W/(m\xB7K)):", prop.Ky);

  m_tkNote = new QLabel(this);
  m_tkNote->setWordWrap(true);
  m_tkNote->setText(prop.tkData.isEmpty()
          ? "No nonlinear k(T) curve defined -- Kx/Ky above are used directly (linear material)."
          : QString("This material has a %1-point nonlinear conductivity curve (from the source "
                     "file), which takes precedence over Kx/Ky above during solving. Not editable here yet.")
                .arg(prop.tkData.size()));
  form->addRow(QString(), m_tkNote);

  m_kt = makeDoubleField(this, form, "Kt (MJ/(m\xB3\xB7K)):", prop.Kt);
  m_qv = makeDoubleField(this, form, "qv (W/m\xB3):", prop.qv);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &ThermalMaterialPropDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(buttons);
}

void ThermalMaterialPropDialog::onAccept()
{
  if (m_name->text().trimmed().isEmpty()) {
    QMessageBox::warning(this, "Invalid Value", "Name cannot be empty.");
    return;
  }
  m_prop.name = m_name->text().trimmed();
  m_prop.Kx = m_kx->text().toDouble();
  m_prop.Ky = m_ky->text().toDouble();
  m_prop.Kt = m_kt->text().toDouble();
  m_prop.qv = m_qv->text().toDouble();
  accept();
}
