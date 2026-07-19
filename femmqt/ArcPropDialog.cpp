#include "ArcPropDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QIntValidator>
#include <QLineEdit>
#include <QVBoxLayout>

ArcPropDialog::ArcPropDialog(FemmArcSegment& arc, const FemmProblem& problem, QWidget* parent)
    : QDialog(parent)
    , m_arc(arc)
{
  setWindowTitle("Arc Segment Properties");

  auto* form = new QFormLayout;

  m_boundary = new QComboBox(this);
  m_boundary->addItem("<None>");
  for (const FemmBoundaryProp& b : problem.boundaryProps)
    m_boundary->addItem(b.name);
  m_boundary->setCurrentIndex(qBound(0, arc.boundaryMarker, problem.boundaryProps.size()));
  form->addRow("Boundary:", m_boundary);

  m_maxSeg = new QLineEdit(QString::number(arc.maxSideLength, 'g', 17), this);
  m_maxSeg->setValidator(new QDoubleValidator(0.001, 90.0, 17, m_maxSeg));
  form->addRow("Max Segment (deg):", m_maxSeg);

  m_hidden = new QCheckBox("Hide this arc in postprocessor", this);
  m_hidden->setChecked(arc.hidden);
  form->addRow(QString(), m_hidden);

  m_inGroup = new QLineEdit(QString::number(arc.inGroup), this);
  m_inGroup->setValidator(new QIntValidator(0, 1000000, m_inGroup));
  form->addRow("In Group:", m_inGroup);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &ArcPropDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(buttons);
}

void ArcPropDialog::onAccept()
{
  m_arc.boundaryMarker = m_boundary->currentIndex();
  m_arc.maxSideLength = m_maxSeg->text().toDouble();
  m_arc.hidden = m_hidden->isChecked();
  m_arc.inGroup = m_inGroup->text().toInt();
  accept();
}
