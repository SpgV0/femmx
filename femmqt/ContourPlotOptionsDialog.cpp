#include "ContourPlotOptionsDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

ContourPlotOptionsDialog::ContourPlotOptionsDialog(MeshSolutionItem* item, bool isAcSolution, QWidget* parent)
    : QDialog(parent)
    , m_item(item)
{
  setWindowTitle("Contour Plot");

  auto* layout = new QVBoxLayout(this);

  auto* form = new QFormLayout;
  m_numContours = new QLineEdit(QString::number(item->numContours()), this);
  m_numContours->setValidator(new QIntValidator(1, 200, m_numContours));
  form->addRow("Number of Contours:", m_numContours);
  layout->addLayout(form);

  // See this class's header comment for why there's no "Real component"/
  // "Show flux lines" checkbox here -- Contour mode's own toggle already
  // covers that; Imaginary is the only independently-optional piece,
  // and only for an AC solution.
  m_showImag = new QCheckBox("Show Imaginary Component", this);
  m_showImag->setChecked(item->showImagContour());
  m_showImag->setEnabled(isAcSolution);
  layout->addWidget(m_showImag);

  auto* rangeBox = new QGroupBox("Range", this);
  auto* rangeLayout = new QVBoxLayout(rangeBox);

  m_automatic = new QRadioButton("Automatic (whole mesh's range)", rangeBox);
  m_customRange = new QRadioButton("Fixed range:", rangeBox);
  rangeLayout->addWidget(m_automatic);
  rangeLayout->addWidget(m_customRange);

  auto* fieldsRow = new QHBoxLayout;
  m_min = new QLineEdit(rangeBox);
  m_min->setValidator(new QDoubleValidator(m_min));
  m_max = new QLineEdit(rangeBox);
  m_max->setValidator(new QDoubleValidator(m_max));
  fieldsRow->addWidget(new QLabel("Lower Bound:", rangeBox));
  fieldsRow->addWidget(m_min);
  fieldsRow->addWidget(new QLabel("Upper Bound:", rangeBox));
  fieldsRow->addWidget(m_max);
  fieldsRow->setContentsMargins(20, 0, 0, 0);
  rangeLayout->addLayout(fieldsRow);
  layout->addWidget(rangeBox);

  double lo, hi;
  if (item->hasCustomContourRange()) {
    m_customRange->setChecked(true);
    item->contourRange(lo, hi);
  } else {
    m_automatic->setChecked(true);
    item->contourAutoRange(lo, hi);
  }
  m_min->setText(QString::number(lo, 'g', 6));
  m_max->setText(QString::number(hi, 'g', 6));

  connect(m_automatic, &QRadioButton::toggled, this, &ContourPlotOptionsDialog::updateFieldsEnabled);
  updateFieldsEnabled();

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &ContourPlotOptionsDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
}

void ContourPlotOptionsDialog::updateFieldsEnabled()
{
  bool custom = m_customRange->isChecked();
  m_min->setEnabled(custom);
  m_max->setEnabled(custom);
}

void ContourPlotOptionsDialog::onAccept()
{
  m_item->setNumContours(m_numContours->text().toInt());
  m_item->setShowImagContour(m_showImag->isChecked());
  double lo = m_min->text().toDouble(), hi = m_max->text().toDouble();
  if (m_customRange->isChecked() && hi > lo)
    m_item->setContourRange(lo, hi);
  else
    m_item->clearContourRange();
  accept();
}
