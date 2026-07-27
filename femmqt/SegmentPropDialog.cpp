#include "SegmentPropDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QIntValidator>
#include <QLineEdit>
#include <QVBoxLayout>

#include <algorithm>

namespace {
template <typename Getter>
bool allSame(const QVector<FemmSegment*>& segs, Getter get)
{
  for (int i = 1; i < segs.size(); i++)
    if (get(segs[i]) != get(segs[0]))
      return false;
  return true;
}
} // namespace

SegmentPropDialog::SegmentPropDialog(const QVector<FemmSegment*>& segments, const FemmProblem& problem, QWidget* parent)
    : QDialog(parent)
    , m_segments(segments)
{
  setWindowTitle(segments.size() == 1 ? "Segment Properties" : QString("Segment Properties (%1 segments)").arg(segments.size()));

  auto* form = new QFormLayout;

  m_boundary = new QComboBox(this);
  m_boundary->addItem("<None>");
  for (const FemmBoundaryProp& b : problem.boundaryProps)
    m_boundary->addItem(b.name);
  bool sameBoundary = allSame(segments, [](FemmSegment* s) { return s->boundaryMarker; });
  m_boundary->setCurrentIndex(sameBoundary ? qBound(0, segments.first()->boundaryMarker, problem.boundaryProps.size()) : 0);
  form->addRow("Boundary:", m_boundary);

  // Matches OpSegDlg's own mixed-selection rule: automesh wins if ANY
  // selected segment is on automesh; otherwise pre-fill with the average
  // of their mesh sizes (not an arbitrary single segment's value).
  bool anyAutomesh = false;
  double meshSizeSum = 0;
  for (FemmSegment* s : segments) {
    if (s->maxSideLength < 0)
      anyAutomesh = true;
    else
      meshSizeSum += s->maxSideLength;
  }
  m_automesh = new QCheckBox("Let Triangle choose mesh size", this);
  m_automesh->setChecked(anyAutomesh);
  form->addRow(QString(), m_automesh);

  double avgMeshSize = anyAutomesh ? 1.0 : (meshSizeSum / segments.size());
  m_meshSize = new QLineEdit(QString::number(avgMeshSize, 'g', 17), this);
  m_meshSize->setValidator(new QDoubleValidator(0.0, 1e300, 17, m_meshSize));
  m_meshSize->setEnabled(!m_automesh->isChecked());
  form->addRow("Max Segment Size:", m_meshSize);
  connect(m_automesh, &QCheckBox::toggled, this, &SegmentPropDialog::onAutomeshToggled);

  bool anyHidden = std::any_of(segments.begin(), segments.end(), [](FemmSegment* s) { return s->hidden; });
  m_hidden = new QCheckBox("Hide this segment in postprocessor", this);
  m_hidden->setChecked(anyHidden);
  form->addRow(QString(), m_hidden);

  bool sameGroup = allSame(segments, [](FemmSegment* s) { return s->inGroup; });
  m_inGroup = new QLineEdit(QString::number(sameGroup ? segments.first()->inGroup : 0), this);
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
  for (FemmSegment* s : m_segments) {
    s->boundaryMarker = m_boundary->currentIndex();
    s->maxSideLength = m_automesh->isChecked() ? -1.0 : m_meshSize->text().toDouble();
    s->hidden = m_hidden->isChecked();
    s->inGroup = m_inGroup->text().toInt();
  }
  accept();
}
