#define _USE_MATH_DEFINES

#include "BlockLabelPropDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QIntValidator>
#include <QLineEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
template <typename Getter>
bool allSame(const QVector<FemmBlockLabel*>& labels, Getter get)
{
  for (int i = 1; i < labels.size(); i++)
    if (get(labels[i]) != get(labels[0]))
      return false;
  return true;
}
} // namespace

BlockLabelPropDialog::BlockLabelPropDialog(const QVector<FemmBlockLabel*>& labels, const FemmProblem& problem, QWidget* parent)
    : QDialog(parent)
    , m_labels(labels)
{
  setWindowTitle(labels.size() == 1 ? "Block Label Properties" : QString("Block Label Properties (%1 labels)").arg(labels.size()));

  auto* form = new QFormLayout;

  m_material = new QComboBox(this);
  bool sameMaterial = allSame(labels, [](FemmBlockLabel* l) { return l->blockTypeIndex; });
  m_materialHasMultiplePlaceholder = !sameMaterial;
  if (m_materialHasMultiplePlaceholder)
    m_material->addItem("<Multiple>");
  m_material->addItem("<Hole>");
  for (const FemmMaterialProp& m : problem.materialProps)
    m_material->addItem(m.name);
  // Combo index doubles as blockTypeIndex directly (offset by the
  // placeholder, if present): index 0 ("<Hole>") maps to -1, index i
  // (>=1) maps to material i -- see this dialog's header comment.
  if (sameMaterial) {
    FemmBlockLabel* l0 = labels.first();
    m_material->setCurrentIndex(l0->blockTypeIndex < 0 ? 0 : qBound(0, l0->blockTypeIndex, problem.materialProps.size()));
  } else {
    m_material->setCurrentIndex(0); // "<Multiple>"
  }
  form->addRow("Block Type:", m_material);

  m_circuit = new QComboBox(this);
  bool sameCircuit = allSame(labels, [](FemmBlockLabel* l) { return l->circuitIndex; });
  m_circuitHasMultiplePlaceholder = !sameCircuit;
  if (m_circuitHasMultiplePlaceholder)
    m_circuit->addItem("<Multiple>");
  m_circuit->addItem("<None>");
  for (const FemmCircuitProp& c : problem.circuitProps)
    m_circuit->addItem(c.name);
  if (sameCircuit)
    m_circuit->setCurrentIndex(qBound(0, labels.first()->circuitIndex, problem.circuitProps.size()));
  else
    m_circuit->setCurrentIndex(0); // "<Multiple>"
  form->addRow("In Circuit:", m_circuit);

  // Matches OpBlkDlg's own rule: the LARGEST mesh area among the
  // selected labels wins (not an average) -- automesh only if every
  // selected label is already on automesh (maxArea <= 0).
  double maxArea = 0;
  for (FemmBlockLabel* l : labels)
    maxArea = std::max(maxArea, l->maxArea);
  m_automesh = new QCheckBox("Let Triangle choose mesh size", this);
  m_automesh->setChecked(maxArea <= 0);
  form->addRow(QString(), m_automesh);

  double sideLength = maxArea > 0 ? std::sqrt(4.0 * maxArea / M_PI) : 1.0;
  m_meshSize = new QLineEdit(QString::number(sideLength, 'g', 17), this);
  m_meshSize->setValidator(new QDoubleValidator(0.0, 1e300, 17, m_meshSize));
  m_meshSize->setEnabled(!m_automesh->isChecked());
  connect(m_automesh, &QCheckBox::toggled, m_meshSize, [this](bool checked) { m_meshSize->setEnabled(!checked); });
  form->addRow("Mesh Size:", m_meshSize);

  // No cross-label aggregation for these three, matching OpBlkDlg (which
  // just takes the last selected label's raw values as a starting point,
  // no mixed-value detection) -- close enough here with the first.
  FemmBlockLabel* rep = labels.first();
  m_magDir = new QLineEdit(QString::number(rep->magDir, 'g', 17), this);
  m_magDir->setValidator(new QDoubleValidator(m_magDir));
  form->addRow("Magnetization Direction (deg):", m_magDir);

  m_magDirFctn = new QLineEdit(rep->magDirFctn, this);
  form->addRow("Mag. Direction Function (Lua, optional):", m_magDirFctn);

  m_turns = new QLineEdit(QString::number(rep->turns), this);
  m_turns->setValidator(new QIntValidator(-1000000, 1000000, m_turns));
  form->addRow("Number of Turns:", m_turns);

  bool sameGroup = allSame(labels, [](FemmBlockLabel* l) { return l->inGroup; });
  m_inGroup = new QLineEdit(QString::number(sameGroup ? rep->inGroup : 0), this);
  m_inGroup->setValidator(new QIntValidator(0, 1000000, m_inGroup));
  form->addRow("In Group:", m_inGroup);

  bool anyExternal = std::any_of(labels.begin(), labels.end(), [](FemmBlockLabel* l) { return l->isExternal; });
  m_isExternal = new QCheckBox("Is an external region label (axisymmetric)", this);
  m_isExternal->setChecked(anyExternal);
  form->addRow(QString(), m_isExternal);

  bool anyDefault = std::any_of(labels.begin(), labels.end(), [](FemmBlockLabel* l) { return l->isDefault; });
  m_isDefault = new QCheckBox("Is the default block label", this);
  m_isDefault->setChecked(anyDefault);
  // Matches OpBlkDlg: only a single-label selection can (un)set the
  // default label -- for a batch, applying it would be ambiguous (which
  // one becomes THE default?), so classic silently skips writing it back
  // when nselected > 1. Disabling it here makes that explicit instead of
  // silent.
  m_isDefault->setEnabled(labels.size() == 1);
  form->addRow(QString(), m_isDefault);

  connect(m_material, &QComboBox::currentIndexChanged, this, &BlockLabelPropDialog::onMaterialChanged);
  onMaterialChanged();

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &BlockLabelPropDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(buttons);
}

void BlockLabelPropDialog::onMaterialChanged()
{
  // Everything below is meaningless for a hole -- see
  // FemmBlockLabel::blockTypeIndex's comment (FemmProblem.h) -- disabled
  // rather than hidden so the (still-preserved) values stay visible.
  // "<Multiple>", when present, sits at index 0 with "<Hole>" pushed to
  // index 1 -- treat it like a hole for enabling purposes (nothing below
  // is safe to edit blind for a batch that hasn't settled on one kind).
  bool isHole = m_material->currentIndex() == (m_materialHasMultiplePlaceholder ? 1 : 0);
  bool isUnresolvedMultiple = m_materialHasMultiplePlaceholder && m_material->currentIndex() == 0;
  bool enable = !isHole && !isUnresolvedMultiple;
  m_circuit->setEnabled(enable);
  m_automesh->setEnabled(enable);
  m_meshSize->setEnabled(enable && !m_automesh->isChecked());
  m_magDir->setEnabled(enable);
  m_magDirFctn->setEnabled(enable);
  m_turns->setEnabled(enable);
  m_isExternal->setEnabled(enable);
  m_isDefault->setEnabled(enable && m_labels.size() == 1);
}

void BlockLabelPropDialog::onAccept()
{
  bool materialTouched = !(m_materialHasMultiplePlaceholder && m_material->currentIndex() == 0);
  int matIdx = m_material->currentIndex() - (m_materialHasMultiplePlaceholder ? 1 : 0);

  bool circuitTouched = !(m_circuitHasMultiplePlaceholder && m_circuit->currentIndex() == 0);
  int circIdx = m_circuit->currentIndex() - (m_circuitHasMultiplePlaceholder ? 1 : 0);

  for (FemmBlockLabel* l : m_labels) {
    if (materialTouched)
      l->blockTypeIndex = (matIdx == 0) ? -1 : matIdx;
    if (circuitTouched)
      l->circuitIndex = circIdx;
    l->maxArea = m_automesh->isChecked() ? 0.0 : (M_PI * std::pow(m_meshSize->text().toDouble(), 2) / 4.0);
    l->magDir = m_magDir->text().toDouble();
    l->magDirFctn = m_magDirFctn->text();
    l->turns = m_turns->text().toInt();
    l->inGroup = m_inGroup->text().toInt();
    l->isExternal = m_isExternal->isChecked();
  }

  if (m_labels.size() == 1 && m_isDefault->isEnabled()) {
    m_labels.first()->isDefault = m_isDefault->isChecked();
  }

  accept();
}
