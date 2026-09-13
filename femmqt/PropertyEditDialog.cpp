#include "PropertyEditDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>

PropertyEditDialog::PropertyEditDialog(PropertyFields::Spec spec, QWidget* parent)
    : QDialog(parent)
    , m_spec(std::move(spec))
{
  setWindowTitle(m_spec.title);

  auto* form = new QFormLayout;

  m_name = new QLineEdit(m_spec.getName ? m_spec.getName() : QString(), this);
  form->addRow(m_spec.nameLabel, m_name);

  if (!m_spec.selector.isEmpty()) {
    m_selector = new QComboBox(this);
    m_selector->addItems(m_spec.selector.options);
    if (m_spec.selector.get) {
      m_selector->setCurrentIndex(
          qBound(0, m_spec.selector.get(), m_spec.selector.options.size() - 1));
    }
    form->addRow(m_spec.selector.label, m_selector);
    connect(m_selector, &QComboBox::currentIndexChanged, this,
        &PropertyEditDialog::updateEnabledFields);
  }

  m_edits.reserve(m_spec.fields.size());
  for (const PropertyFields::Field& f : m_spec.fields) {
    auto* edit = new QLineEdit(f.get ? f.get() : QString(), this);
    if (f.integer)
      edit->setValidator(new QIntValidator(edit));
    else
      edit->setValidator(new QDoubleValidator(-1e300, 1e300, 15, edit));
    if (!f.tooltip.isEmpty())
      edit->setToolTip(f.tooltip);
    auto* label = new QLabel(f.label, this);
    if (!f.tooltip.isEmpty())
      label->setToolTip(f.tooltip);
    form->addRow(label, edit);
    m_edits.push_back(edit);
  }

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &PropertyEditDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(buttons);

  updateEnabledFields();
}

void PropertyEditDialog::updateEnabledFields()
{
  const int option = m_selector ? m_selector->currentIndex() : -1;
  for (int i = 0; i < m_edits.size() && i < m_spec.fields.size(); i++) {
    const PropertyFields::Field& f = m_spec.fields[i];
    // No restriction listed means the field always applies -- which is
    // every field of a material, and most of a point property.
    const bool enabled = f.enabledForOptions.isEmpty()
        || (option >= 0 && f.enabledForOptions.contains(option));
    m_edits[i]->setEnabled(enabled);
    // Also grey the label, so a disabled row reads as inapplicable
    // rather than as an empty box the user has failed to fill in.
    if (auto* form = qobject_cast<QFormLayout*>(layout()->itemAt(0)->layout())) {
      if (QWidget* label = form->labelForField(m_edits[i]))
        label->setEnabled(enabled);
    }
  }
}

void PropertyEditDialog::onAccept()
{
  if (m_spec.setName)
    m_spec.setName(m_name->text());
  if (m_selector && m_spec.selector.set)
    m_spec.selector.set(m_selector->currentIndex());

  // Every field is written, including the disabled ones, which keep
  // whatever value they already had. Deliberately NOT zeroing the
  // inapplicable ones: <BdryType> already says which apply, the solver
  // reads only those, and clearing them would silently discard a
  // convection coefficient the moment someone looked at a boundary as
  // "Fixed Temperature" and pressed OK.
  for (int i = 0; i < m_edits.size() && i < m_spec.fields.size(); i++) {
    if (m_spec.fields[i].set)
      m_spec.fields[i].set(m_edits[i]->text());
  }
  accept();
}
