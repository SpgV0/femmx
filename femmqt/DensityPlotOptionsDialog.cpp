#include "DensityPlotOptionsDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QVBoxLayout>

DensityPlotOptionsDialog::DensityPlotOptionsDialog(MeshSolutionItem* item, QWidget* parent)
    : QDialog(parent)
    , m_item(item)
{
  setWindowTitle("Density Plot Options");

  auto* layout = new QVBoxLayout(this);

  m_grayscale = new QCheckBox("Greyscale", this);
  m_grayscale->setChecked(item->grayscale());
  layout->addWidget(m_grayscale);

  auto* rangeBox = new QGroupBox(QString("Range for %1").arg(item->legendTitle()), this);
  auto* rangeLayout = new QVBoxLayout(rangeBox);

  m_automatic = new QRadioButton("Automatic (rescales to whatever's visible on screen)", rangeBox);
  m_customRange = new QRadioButton("Fixed range:", rangeBox);
  rangeLayout->addWidget(m_automatic);
  rangeLayout->addWidget(m_customRange);

  auto* fieldsRow = new QHBoxLayout;
  m_min = new QLineEdit(rangeBox);
  m_min->setValidator(new QDoubleValidator(m_min));
  m_max = new QLineEdit(rangeBox);
  m_max->setValidator(new QDoubleValidator(m_max));
  fieldsRow->addWidget(new QLabel("Min:", rangeBox));
  fieldsRow->addWidget(m_min);
  fieldsRow->addWidget(new QLabel("Max:", rangeBox));
  fieldsRow->addWidget(m_max);
  fieldsRow->setContentsMargins(20, 0, 0, 0);
  rangeLayout->addLayout(fieldsRow);

  DensityQuantity q = item->densityQuantity();
  double lo, hi;
  if (item->hasCustomRange(q)) {
    item->customRange(q, lo, hi);
    m_customRange->setChecked(true);
  } else {
    item->densityQuantityAutoRange(q, lo, hi);
    m_automatic->setChecked(true);
  }
  m_min->setText(QString::number(lo, 'g', 6));
  m_max->setText(QString::number(hi, 'g', 6));

  connect(m_automatic, &QRadioButton::toggled, this, &DensityPlotOptionsDialog::updateFieldsEnabled);
  updateFieldsEnabled();

  layout->addWidget(rangeBox);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &DensityPlotOptionsDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
}

void DensityPlotOptionsDialog::updateFieldsEnabled()
{
  bool custom = m_customRange->isChecked();
  m_min->setEnabled(custom);
  m_max->setEnabled(custom);
}

void DensityPlotOptionsDialog::onAccept()
{
  m_item->setGrayscale(m_grayscale->isChecked());
  DensityQuantity q = m_item->densityQuantity();
  if (m_customRange->isChecked()) {
    double lo = m_min->text().toDouble();
    double hi = m_max->text().toDouble();
    if (hi > lo)
      m_item->setCustomRange(q, lo, hi);
  } else {
    m_item->clearCustomRange(q);
  }
  accept();
}
