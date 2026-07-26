#include "ThermalBoundaryPropDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

namespace {
QLineEdit* makeField(QWidget* parent, QFormLayout* form, const QString& label, double value)
{
  auto* edit = new QLineEdit(QString::number(value, 'g', 17), parent);
  edit->setValidator(new QDoubleValidator(edit));
  form->addRow(label, edit);
  return edit;
}
}

ThermalBoundaryPropDialog::ThermalBoundaryPropDialog(FemmThermalBoundaryProp& prop, QWidget* parent)
    : QDialog(parent)
    , m_prop(prop)
{
  setWindowTitle("Boundary Property (Heat Flow)");

  auto* form = new QFormLayout;
  m_name = new QLineEdit(prop.name, this);
  form->addRow("Name:", m_name);

  m_format = new QComboBox(this);
  m_format->addItems({ "Fixed Temperature", "Heat Flux", "Convection", "Radiation" });
  // See this dialog's header comment for why 4/5 (periodic/antiperiodic)
  // fall back to page 0 without silently changing bdryFormat.
  m_format->setCurrentIndex((prop.bdryFormat >= 0 && prop.bdryFormat <= 3) ? prop.bdryFormat : 0);
  form->addRow("BC Type:", m_format);

  m_tset = makeField(this, form, "Tset (K):", prop.Tset);
  m_qs = makeField(this, form, "qs (W/m\xC2\xB2):", prop.qs);
  m_h = makeField(this, form, "h (W/(m\xC2\xB2K)):", prop.h);
  m_tinf = makeField(this, form, "Tinf (K):", prop.Tinf);
  m_beta = makeField(this, form, "beta:", prop.beta);
  m_tinfRad = makeField(this, form, "TinfRad (K):", prop.TinfRad);

  connect(m_format, &QComboBox::currentIndexChanged, this, &ThermalBoundaryPropDialog::onFormatChanged);
  onFormatChanged();

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &ThermalBoundaryPropDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(buttons);
}

void ThermalBoundaryPropDialog::onFormatChanged()
{
  // Matches femm/hd_BdryDlg.cpp's OnSelchangeBdryformat() exactly: each
  // format enables the fields it actually uses (see this dialog's header
  // comment for the field-set/format mapping).
  int fmt = m_format->currentIndex();
  m_tset->setEnabled(fmt == 0);
  m_qs->setEnabled(fmt >= 1);
  m_h->setEnabled(fmt >= 2);
  m_tinf->setEnabled(fmt >= 2);
  m_beta->setEnabled(fmt == 3);
  m_tinfRad->setEnabled(fmt == 3);
}

void ThermalBoundaryPropDialog::onAccept()
{
  if (m_name->text().trimmed().isEmpty()) {
    QMessageBox::warning(this, "Invalid Value", "Name cannot be empty.");
    return;
  }
  m_prop.name = m_name->text().trimmed();
  m_prop.bdryFormat = m_format->currentIndex();
  m_prop.Tset = m_tset->text().toDouble();
  m_prop.qs = m_qs->text().toDouble();
  m_prop.h = m_h->text().toDouble();
  m_prop.Tinf = m_tinf->text().toDouble();
  m_prop.beta = m_beta->text().toDouble();
  m_prop.TinfRad = m_tinfRad->text().toDouble();
  accept();
}
