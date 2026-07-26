#include "ArcPropDialog.h"

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
bool allSame(const QVector<FemmArcSegment*>& arcs, Getter get)
{
  for (int i = 1; i < arcs.size(); i++)
    if (get(arcs[i]) != get(arcs[0]))
      return false;
  return true;
}
} // namespace

ArcPropDialog::ArcPropDialog(const QVector<FemmArcSegment*>& arcs, const FemmProblem& problem, QWidget* parent)
    : QDialog(parent)
    , m_arcs(arcs)
{
  setWindowTitle(arcs.size() == 1 ? "Arc Segment Properties" : QString("Arc Segment Properties (%1 arcs)").arg(arcs.size()));

  auto* form = new QFormLayout;

  m_boundary = new QComboBox(this);
  m_boundary->addItem("<None>");
  for (const FemmBoundaryProp& b : problem.boundaryProps)
    m_boundary->addItem(b.name);
  bool sameBoundary = allSame(arcs, [](FemmArcSegment* a) { return a->boundaryMarker; });
  m_boundary->setCurrentIndex(sameBoundary ? qBound(0, arcs.first()->boundaryMarker, problem.boundaryProps.size()) : 0);
  form->addRow("Boundary:", m_boundary);

  // Matches OpArcSegDlg's own rule: always the average of the batch's
  // mesh sizes (not an arbitrary single arc's value), even when they
  // already agree (averaging a uniform set is a no-op).
  double sumMaxSeg = 0;
  for (FemmArcSegment* a : arcs)
    sumMaxSeg += a->maxSideLength;
  m_maxSeg = new QLineEdit(QString::number(sumMaxSeg / arcs.size(), 'g', 17), this);
  m_maxSeg->setValidator(new QDoubleValidator(0.001, 90.0, 17, m_maxSeg));
  form->addRow("Max Segment (deg):", m_maxSeg);

  bool anyHidden = std::any_of(arcs.begin(), arcs.end(), [](FemmArcSegment* a) { return a->hidden; });
  m_hidden = new QCheckBox("Hide this arc in postprocessor", this);
  m_hidden->setChecked(anyHidden);
  form->addRow(QString(), m_hidden);

  bool sameGroup = allSame(arcs, [](FemmArcSegment* a) { return a->inGroup; });
  m_inGroup = new QLineEdit(QString::number(sameGroup ? arcs.first()->inGroup : 0), this);
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
  for (FemmArcSegment* a : m_arcs) {
    a->boundaryMarker = m_boundary->currentIndex();
    a->maxSideLength = m_maxSeg->text().toDouble();
    a->hidden = m_hidden->isChecked();
    a->inGroup = m_inGroup->text().toInt();
  }
  accept();
}
