#include "BoundaryPropDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace {
QLineEdit* makeField(QWidget* parent, QFormLayout* form, const QString& label, double value)
{
  auto* edit = new QLineEdit(QString::number(value, 'g', 17), parent);
  edit->setValidator(new QDoubleValidator(edit));
  form->addRow(label, edit);
  return edit;
}
}

BoundaryPropDialog::BoundaryPropDialog(FemmBoundaryProp& prop, QWidget* parent)
    : QDialog(parent)
    , m_prop(prop)
    , m_originalFormat(prop.bdryFormat)
{
  setWindowTitle("Boundary Property");

  auto* topForm = new QFormLayout;
  m_name = new QLineEdit(prop.name, this);
  topForm->addRow("Name:", m_name);

  m_format = new QComboBox(this);
  m_format->addItems({ "Fixed A", "Small Skin Depth", "Mixed" });
  // A boundary already using a periodic/antiperiodic format (4-7, only
  // reachable by opening a file made in the classic GUI -- this dialog
  // never produces one) has no matching entry in this combo; default to
  // "Fixed A"'s page for editing everything else about it without
  // silently changing bdryFormat unless the user actively picks a new
  // type (see onAccept).
  m_format->setCurrentIndex((prop.bdryFormat >= 0 && prop.bdryFormat <= 2) ? prop.bdryFormat : 0);
  topForm->addRow("Type:", m_format);

  m_stack = new QStackedWidget(this);

  auto* fixedPage = new QWidget(this);
  auto* fixedForm = new QFormLayout(fixedPage);
  m_a0 = makeField(fixedPage, fixedForm, "A0:", prop.A0);
  m_a1 = makeField(fixedPage, fixedForm, "A1:", prop.A1);
  m_a2 = makeField(fixedPage, fixedForm, "A2:", prop.A2);
  m_phi = makeField(fixedPage, fixedForm, "Phi (deg):", prop.phi);
  m_stack->addWidget(fixedPage);

  auto* ssdPage = new QWidget(this);
  auto* ssdForm = new QFormLayout(ssdPage);
  m_muSsd = makeField(ssdPage, ssdForm, "Mu:", prop.muSsd);
  m_sigmaSsd = makeField(ssdPage, ssdForm, "Sigma (MS/m):", prop.sigmaSsd);
  m_stack->addWidget(ssdPage);

  auto* mixedPage = new QWidget(this);
  auto* mixedForm = new QFormLayout(mixedPage);
  m_c0re = makeField(mixedPage, mixedForm, "c0 re:", prop.c0re);
  m_c0im = makeField(mixedPage, mixedForm, "c0 im:", prop.c0im);
  m_c1re = makeField(mixedPage, mixedForm, "c1 re:", prop.c1re);
  m_c1im = makeField(mixedPage, mixedForm, "c1 im:", prop.c1im);
  m_stack->addWidget(mixedPage);

  m_stack->setCurrentIndex(m_format->currentIndex());
  connect(m_format, &QComboBox::currentIndexChanged, this, &BoundaryPropDialog::onFormatChanged);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &BoundaryPropDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(topForm);
  layout->addWidget(m_stack);
  layout->addWidget(buttons);
}

void BoundaryPropDialog::onFormatChanged()
{
  m_stack->setCurrentIndex(m_format->currentIndex());
}

void BoundaryPropDialog::onAccept()
{
  if (m_name->text().trimmed().isEmpty()) {
    QMessageBox::warning(this, "Invalid Value", "Name cannot be empty.");
    return;
  }
  m_prop.name = m_name->text().trimmed();
  m_prop.bdryFormat = m_format->currentIndex();
  m_prop.A0 = m_a0->text().toDouble();
  m_prop.A1 = m_a1->text().toDouble();
  m_prop.A2 = m_a2->text().toDouble();
  m_prop.phi = m_phi->text().toDouble();
  m_prop.muSsd = m_muSsd->text().toDouble();
  m_prop.sigmaSsd = m_sigmaSsd->text().toDouble();
  m_prop.c0re = m_c0re->text().toDouble();
  m_prop.c0im = m_c0im->text().toDouble();
  m_prop.c1re = m_c1re->text().toDouble();
  m_prop.c1im = m_c1im->text().toDouble();
  accept();
}
