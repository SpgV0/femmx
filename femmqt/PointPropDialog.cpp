#include "PointPropDialog.h"

#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QMessageBox>
#include <QRadioButton>
#include <QVBoxLayout>

#include <cmath>

PointPropDialog::PointPropDialog(FemmPointProp& prop, QWidget* parent)
    : QDialog(parent)
    , m_prop(prop)
{
  setWindowTitle("Point Property");

  auto* layout = new QVBoxLayout(this);

  auto* form = new QFormLayout;
  m_name = new QLineEdit(prop.name, this);
  form->addRow("Name:", m_name);
  layout->addLayout(form);

  m_setA = new QRadioButton("Specified Potential Property", this);
  m_setI = new QRadioButton("Point Current Property", this);
  // Matches CNodeProp::OnInitDialog: start on whichever kind this
  // property already is, based on which of Jp/Ap is nonzero (Jp wins if
  // somehow both are, same as classic's abs(m_jp) > 0 check).
  bool startOnCurrent = std::hypot(prop.Jr, prop.Ji) > 0;
  m_setI->setChecked(startOnCurrent);
  m_setA->setChecked(!startOnCurrent);
  layout->addWidget(m_setA);
  layout->addWidget(m_setI);
  connect(m_setA, &QRadioButton::toggled, this, &PointPropDialog::updateFieldsEnabled);

  auto* aBox = new QGroupBox("Specified Vector Potential, Wb/m", this);
  auto* aForm = new QFormLayout(aBox);
  m_ar = new QLineEdit(QString::number(prop.Ar, 'g', 17), this);
  m_ar->setValidator(new QDoubleValidator(m_ar));
  aForm->addRow("A re:", m_ar);
  m_ai = new QLineEdit(QString::number(prop.Ai, 'g', 17), this);
  m_ai->setValidator(new QDoubleValidator(m_ai));
  aForm->addRow("A im:", m_ai);
  layout->addWidget(aBox);

  auto* jBox = new QGroupBox("Point Current, Amps", this);
  auto* jForm = new QFormLayout(jBox);
  m_jr = new QLineEdit(QString::number(prop.Jr, 'g', 17), this);
  m_jr->setValidator(new QDoubleValidator(m_jr));
  jForm->addRow("I re:", m_jr);
  m_ji = new QLineEdit(QString::number(prop.Ji, 'g', 17), this);
  m_ji->setValidator(new QDoubleValidator(m_ji));
  jForm->addRow("I im:", m_ji);
  layout->addWidget(jBox);

  updateFieldsEnabled();

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &PointPropDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
}

void PointPropDialog::updateFieldsEnabled()
{
  bool onA = m_setA->isChecked();
  m_ar->setEnabled(onA);
  m_ai->setEnabled(onA);
  m_jr->setEnabled(!onA);
  m_ji->setEnabled(!onA);
}

void PointPropDialog::onAccept()
{
  if (m_name->text().trimmed().isEmpty()) {
    QMessageBox::warning(this, "Invalid Value", "Name cannot be empty.");
    return;
  }
  m_prop.name = m_name->text().trimmed();
  // Matches OnSetA/OnSetI: whichever kind isn't selected is zeroed, not
  // just left disabled with a stale prior value.
  if (m_setA->isChecked()) {
    m_prop.Ar = m_ar->text().toDouble();
    m_prop.Ai = m_ai->text().toDouble();
    m_prop.Jr = 0;
    m_prop.Ji = 0;
  } else {
    m_prop.Jr = m_jr->text().toDouble();
    m_prop.Ji = m_ji->text().toDouble();
    m_prop.Ar = 0;
    m_prop.Ai = 0;
  }
  accept();
}
