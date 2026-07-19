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

#include <cmath>

BlockLabelPropDialog::BlockLabelPropDialog(FemmBlockLabel& label, const FemmProblem& problem, QWidget* parent)
    : QDialog(parent)
    , m_label(label)
{
  setWindowTitle("Block Label Properties");

  auto* form = new QFormLayout;

  m_material = new QComboBox(this);
  m_material->addItem("<Hole>");
  for (const FemmMaterialProp& m : problem.materialProps)
    m_material->addItem(m.name);
  // Combo index doubles as blockTypeIndex directly: index 0 ("<Hole>")
  // maps to -1, index i (>=1) maps to material i -- see this dialog's
  // header comment.
  m_material->setCurrentIndex(label.blockTypeIndex < 0 ? 0 : qBound(0, label.blockTypeIndex, problem.materialProps.size()));
  form->addRow("Block Type:", m_material);

  m_circuit = new QComboBox(this);
  m_circuit->addItem("<None>");
  for (const FemmCircuitProp& c : problem.circuitProps)
    m_circuit->addItem(c.name);
  m_circuit->setCurrentIndex(qBound(0, label.circuitIndex, problem.circuitProps.size()));
  form->addRow("In Circuit:", m_circuit);

  m_automesh = new QCheckBox("Let Triangle choose mesh size", this);
  m_automesh->setChecked(label.maxArea <= 0);
  form->addRow(QString(), m_automesh);

  double sideLength = label.maxArea > 0 ? std::sqrt(4.0 * label.maxArea / M_PI) : 1.0;
  m_meshSize = new QLineEdit(QString::number(sideLength, 'g', 17), this);
  m_meshSize->setValidator(new QDoubleValidator(0.0, 1e300, 17, m_meshSize));
  m_meshSize->setEnabled(!m_automesh->isChecked());
  connect(m_automesh, &QCheckBox::toggled, m_meshSize, [this](bool checked) { m_meshSize->setEnabled(!checked); });
  form->addRow("Mesh Size:", m_meshSize);

  m_magDir = new QLineEdit(QString::number(label.magDir, 'g', 17), this);
  m_magDir->setValidator(new QDoubleValidator(m_magDir));
  form->addRow("Magnetization Direction (deg):", m_magDir);

  m_magDirFctn = new QLineEdit(label.magDirFctn, this);
  form->addRow("Mag. Direction Function (Lua, optional):", m_magDirFctn);

  m_turns = new QLineEdit(QString::number(label.turns), this);
  m_turns->setValidator(new QIntValidator(-1000000, 1000000, m_turns));
  form->addRow("Number of Turns:", m_turns);

  m_inGroup = new QLineEdit(QString::number(label.inGroup), this);
  m_inGroup->setValidator(new QIntValidator(0, 1000000, m_inGroup));
  form->addRow("In Group:", m_inGroup);

  m_isExternal = new QCheckBox("Is an external region label (axisymmetric)", this);
  m_isExternal->setChecked(label.isExternal);
  form->addRow(QString(), m_isExternal);

  m_isDefault = new QCheckBox("Is the default block label", this);
  m_isDefault->setChecked(label.isDefault);
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
  bool isHole = m_material->currentIndex() == 0;
  m_circuit->setEnabled(!isHole);
  m_automesh->setEnabled(!isHole);
  m_meshSize->setEnabled(!isHole && !m_automesh->isChecked());
  m_magDir->setEnabled(!isHole);
  m_magDirFctn->setEnabled(!isHole);
  m_turns->setEnabled(!isHole);
  m_isExternal->setEnabled(!isHole);
  m_isDefault->setEnabled(!isHole);
}

void BlockLabelPropDialog::onAccept()
{
  int matIdx = m_material->currentIndex();
  m_label.blockTypeIndex = (matIdx == 0) ? -1 : matIdx;
  m_label.circuitIndex = m_circuit->currentIndex();
  m_label.maxArea = m_automesh->isChecked() ? 0.0 : (M_PI * std::pow(m_meshSize->text().toDouble(), 2) / 4.0);
  m_label.magDir = m_magDir->text().toDouble();
  m_label.magDirFctn = m_magDirFctn->text();
  m_label.turns = m_turns->text().toInt();
  m_label.inGroup = m_inGroup->text().toInt();
  m_label.isExternal = m_isExternal->isChecked();
  m_label.isDefault = m_isDefault->isChecked();
  accept();
}
