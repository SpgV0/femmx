#include "NumberFormDialog.h"

#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

NumberFormDialog::NumberFormDialog(const QString& title, QWidget* parent)
    : QDialog(parent)
{
  setWindowTitle(title);
  auto* layout = new QVBoxLayout(this);
  m_form = new QFormLayout;
  layout->addLayout(m_form);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
}

int NumberFormDialog::addField(const QString& label, const QString& objectName, double initial, double minimum)
{
  auto* edit = new QLineEdit(QString::number(initial, 'g', 10), this);
  edit->setObjectName(objectName);
  auto* validator = new QDoubleValidator(minimum, 1e9, 10, edit);
  validator->setNotation(QDoubleValidator::StandardNotation);
  edit->setValidator(validator);
  m_form->addRow(label, edit);
  m_fields.push_back(edit);
  // OK only while every field holds a number in range.
  auto* ok = findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok);
  connect(edit, &QLineEdit::textChanged, this, [this, ok]() {
    bool all = true;
    for (QLineEdit* f : m_fields)
      all = all && f->hasAcceptableInput();
    ok->setEnabled(all);
  });
  if (m_fields.size() == 1) {
    edit->setFocus();
    edit->selectAll();
  }
  return m_fields.size() - 1;
}

double NumberFormDialog::value(int index) const
{
  return m_fields.value(index) ? m_fields[index]->text().toDouble() : 0.0;
}
