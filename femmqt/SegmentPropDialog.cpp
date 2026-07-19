#include "SegmentPropDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QIntValidator>
#include <QLineEdit>
#include <QVBoxLayout>

SegmentPropDialog::SegmentPropDialog(FemmSegment& segment, const FemmProblem& problem, QWidget* parent)
    : QDialog(parent)
    , m_segment(segment)
{
  setWindowTitle("Segment Properties");

  auto* form = new QFormLayout;

  m_boundary = new QComboBox(this);
  m_boundary->addItem("<None>");
  for (const FemmBoundaryProp& b : problem.boundaryProps)
    m_boundary->addItem(b.name);
  m_boundary->setCurrentIndex(qBound(0, segment.boundaryMarker, problem.boundaryProps.size()));
  form->addRow("Boundary:", m_boundary);

  // maxSideLength < 0 means "<No Mesh Constraint>" (automesh) -- see
  // FemmSegment::maxSideLength's comment.
  m_automesh = new QCheckBox("Let Triangle choose mesh size", this);
  m_automesh->setChecked(segment.maxSideLength < 0);
  form->addRow(QString(), m_automesh);

  m_meshSize = new QLineEdit(QString::number(segment.maxSideLength < 0 ? 1.0 : segment.maxSideLength, 'g', 17), this);
  m_meshSize->setValidator(new QDoubleValidator(0.0, 1e300, 17, m_meshSize));
  m_meshSize->setEnabled(!m_automesh->isChecked());
  form->addRow("Max Segment Size:", m_meshSize);
  connect(m_automesh, &QCheckBox::toggled, this, &SegmentPropDialog::onAutomeshToggled);

  m_hidden = new QCheckBox("Hide this segment in postprocessor", this);
  m_hidden->setChecked(segment.hidden);
  form->addRow(QString(), m_hidden);

  m_inGroup = new QLineEdit(QString::number(segment.inGroup), this);
  m_inGroup->setValidator(new QIntValidator(0, 1000000, m_inGroup));
  form->addRow("In Group:", m_inGroup);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &SegmentPropDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(buttons);
}

void SegmentPropDialog::onAutomeshToggled(bool checked)
{
  m_meshSize->setEnabled(!checked);
}

void SegmentPropDialog::onAccept()
{
  m_segment.boundaryMarker = m_boundary->currentIndex();
  m_segment.maxSideLength = m_automesh->isChecked() ? -1.0 : m_meshSize->text().toDouble();
  m_segment.hidden = m_hidden->isChecked();
  m_segment.inGroup = m_inGroup->text().toInt();
  accept();
}
