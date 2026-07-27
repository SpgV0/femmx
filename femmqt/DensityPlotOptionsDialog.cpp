#include "DensityPlotOptionsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace {
// Matches femm/cv_DPlotDlg2.cpp's OnInitDialog listtype==2 (AC) item
// order/labels exactly (see this dialog's header comment) -- kept as
// plain strings rather than MeshSolutionItem::legendTitle(q) since these
// are the combo's own item text, not the per-quantity groupbox caption
// below it.
const char* kQuantityLabels[DensityPlotOptionsDialog::kNumQuantities] = {
  "|B| (Tesla)",
  "|B_re| (Tesla)",
  "|B_im| (Tesla)",
  "log10(|B|)",
  "|H| (Amp/m)",
  "|Js+Je| (MA/m^2)",
};
}

DensityPlotOptionsDialog::DensityPlotOptionsDialog(MeshSolutionItem* item, bool legendVisible, QWidget* parent)
    : QDialog(parent)
    , m_item(item)
    , m_showLegend(legendVisible)
{
  setWindowTitle("Density Plot");

  auto* layout = new QVBoxLayout(this);

  auto* form = new QFormLayout;
  m_quantityCombo = new QComboBox(this);
  for (int i = 0; i < kNumQuantities; i++)
    m_quantityCombo->addItem(kQuantityLabels[i]);
  form->addRow("Quantity:", m_quantityCombo);
  layout->addLayout(form);

  m_grayscale = new QCheckBox("Greyscale", this);
  m_grayscale->setChecked(item->grayscale());
  layout->addWidget(m_grayscale);

  m_legendCheck = new QCheckBox("Show Legend", this);
  m_legendCheck->setChecked(m_showLegend);
  layout->addWidget(m_legendCheck);

  auto* rangeBox = new QGroupBox(this);
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

  // Seed the per-quantity local state (mirrors classic's PlotBounds[]
  // being populated up front) from whatever the item already has, so
  // switching the combo around without ever touching a field still
  // commits each quantity's PRE-EXISTING range on OK instead of
  // clobbering it back to Automatic.
  for (int i = 0; i < kNumQuantities; i++) {
    auto q = static_cast<DensityQuantity>(i);
    if (item->hasCustomRange(q)) {
      m_useCustom[i] = true;
      item->customRange(q, m_lo[i], m_hi[i]);
    } else {
      m_useCustom[i] = false;
      item->densityQuantityAutoRange(q, m_lo[i], m_hi[i]);
    }
  }

  m_currentQtyIndex = static_cast<int>(item->densityQuantity());
  m_quantityCombo->setCurrentIndex(m_currentQtyIndex);
  loadFieldsFromLocal(m_currentQtyIndex);
  rangeBox->setTitle(QString("Range for %1").arg(item->legendTitle(static_cast<DensityQuantity>(m_currentQtyIndex))));

  connect(m_automatic, &QRadioButton::toggled, this, &DensityPlotOptionsDialog::updateFieldsEnabled);
  updateFieldsEnabled();
  connect(m_quantityCombo, &QComboBox::currentIndexChanged, this, &DensityPlotOptionsDialog::onQuantityChanged);

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

void DensityPlotOptionsDialog::saveFieldsToLocal(int qIndex)
{
  m_useCustom[qIndex] = m_customRange->isChecked();
  m_lo[qIndex] = m_min->text().toDouble();
  m_hi[qIndex] = m_max->text().toDouble();
}

void DensityPlotOptionsDialog::loadFieldsFromLocal(int qIndex)
{
  if (m_useCustom[qIndex])
    m_customRange->setChecked(true);
  else
    m_automatic->setChecked(true);
  m_min->setText(QString::number(m_lo[qIndex], 'g', 6));
  m_max->setText(QString::number(m_hi[qIndex], 'g', 6));
  updateFieldsEnabled();
}

void DensityPlotOptionsDialog::onQuantityChanged(int index)
{
  // Matches classic's OnSelchangeDplottype: stash the outgoing
  // quantity's fields, then load whatever was last stashed for the
  // incoming one -- see this class's header comment.
  saveFieldsToLocal(m_currentQtyIndex);
  m_currentQtyIndex = index;
  loadFieldsFromLocal(m_currentQtyIndex);
  if (auto* rangeBox = qobject_cast<QGroupBox*>(m_customRange->parentWidget()))
    rangeBox->setTitle(QString("Range for %1").arg(m_item->legendTitle(static_cast<DensityQuantity>(index))));
}

void DensityPlotOptionsDialog::onAccept()
{
  saveFieldsToLocal(m_currentQtyIndex);

  m_showLegend = m_legendCheck->isChecked();
  m_item->setGrayscale(m_grayscale->isChecked());
  for (int i = 0; i < kNumQuantities; i++) {
    auto q = static_cast<DensityQuantity>(i);
    if (m_useCustom[i] && m_hi[i] > m_lo[i])
      m_item->setCustomRange(q, m_lo[i], m_hi[i]);
    else
      m_item->clearCustomRange(q);
  }
  m_item->setDensityQuantity(static_cast<DensityQuantity>(m_currentQtyIndex));
  accept();
}
