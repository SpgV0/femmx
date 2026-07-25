#include "MaterialPropDialog.h"

#include "BHCurveDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
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

MaterialPropDialog::MaterialPropDialog(FemmMaterialProp& prop, QWidget* parent)
    : QDialog(parent)
    , m_prop(prop)
{
  setWindowTitle("Material Property");

  auto* form = new QFormLayout;
  m_name = new QLineEdit(prop.name, this);
  form->addRow("Name:", m_name);

  m_muX = makeDoubleField(this, form, "Mu x (relative):", prop.muX);
  m_muY = makeDoubleField(this, form, "Mu y (relative):", prop.muY);

  m_bhNote = new QLabel(this);
  m_bhNote->setWordWrap(true);
  form->addRow(QString(), m_bhNote);
  auto* bhButton = new QPushButton("Edit BH Curve...", this);
  connect(bhButton, &QPushButton::clicked, this, &MaterialPropDialog::onEditBhCurve);
  form->addRow(QString(), bhButton);
  updateBhNote();

  m_hc = makeDoubleField(this, form, "Hc (A/m):", prop.Hc);
  m_hcAngle = makeDoubleField(this, form, "Hc Angle (deg):", prop.HcAngle);
  m_jsrcRe = makeDoubleField(this, form, "J re (MA/m\xC2\xB2):", prop.JsrcRe);
  m_jsrcIm = makeDoubleField(this, form, "J im (MA/m\xC2\xB2):", prop.JsrcIm);
  m_sigma = makeDoubleField(this, form, "Sigma (MS/m):", prop.sigma);
  m_dLam = makeDoubleField(this, form, "Lamination thickness (mm):", prop.dLam);
  m_phiH = makeDoubleField(this, form, "Hysteresis lag angle (deg):", prop.phiH);
  m_phiHx = makeDoubleField(this, form, "Hysteresis lag angle, x (deg):", prop.phiHx);
  m_phiHy = makeDoubleField(this, form, "Hysteresis lag angle, y (deg):", prop.phiHy);

  m_lamType = new QComboBox(this);
  m_lamType->addItems({ "0 - Not laminated / laminated in plane", "1 - Laminated perpendicular to plane",
      "2 - Magnet wire", "3 - Plain stranded wire", "4 - Litz wire", "5 - Square wire",
      "6 - 100% fill wire", "7 - 150% fill wire" });
  m_lamType->setCurrentIndex(qBound(0, prop.lamType, 7));
  form->addRow("Lamination Type:", m_lamType);

  m_lamFill = makeDoubleField(this, form, "Lamination/Fill Factor:", prop.lamFill);
  m_nStrands = new QLineEdit(QString::number(prop.nStrands), this);
  m_nStrands->setValidator(new QIntValidator(0, 1000000, m_nStrands));
  form->addRow("Number of Strands:", m_nStrands);
  m_wireD = makeDoubleField(this, form, "Wire Diameter (mm):", prop.wireD);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &MaterialPropDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(buttons);
}

void MaterialPropDialog::onAccept()
{
  if (m_name->text().trimmed().isEmpty()) {
    QMessageBox::warning(this, "Invalid Value", "Name cannot be empty.");
    return;
  }
  m_prop.name = m_name->text().trimmed();
  m_prop.muX = m_muX->text().toDouble();
  m_prop.muY = m_muY->text().toDouble();
  m_prop.Hc = m_hc->text().toDouble();
  m_prop.HcAngle = m_hcAngle->text().toDouble();
  m_prop.JsrcRe = m_jsrcRe->text().toDouble();
  m_prop.JsrcIm = m_jsrcIm->text().toDouble();
  m_prop.sigma = m_sigma->text().toDouble();
  m_prop.dLam = m_dLam->text().toDouble();
  m_prop.phiH = m_phiH->text().toDouble();
  m_prop.phiHx = m_phiHx->text().toDouble();
  m_prop.phiHy = m_phiHy->text().toDouble();
  m_prop.lamType = m_lamType->currentIndex();
  m_prop.lamFill = m_lamFill->text().toDouble();
  m_prop.nStrands = m_nStrands->text().toInt();
  m_prop.wireD = m_wireD->text().toDouble();
  accept();
}

void MaterialPropDialog::onEditBhCurve()
{
  BHCurveDialog dlg(m_prop, this);
  if (dlg.exec() == QDialog::Accepted)
    updateBhNote();
}

void MaterialPropDialog::updateBhNote()
{
  m_bhNote->setText(m_prop.bhData.isEmpty()
          ? "No BH curve defined -- Mu x/y above are used directly (linear material)."
          : QString("This material has a %1-point BH curve, which takes precedence over "
                     "Mu x/y above during solving.")
                .arg(m_prop.bhData.size()));
}
