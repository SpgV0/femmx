#include "MoveCopyDialog.h"

#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QGroupBox>
#include <QIntValidator>
#include <QLineEdit>
#include <QRadioButton>
#include <QVBoxLayout>

#include <algorithm>

MoveCopyDialog::MoveCopyDialog(bool isMove, QWidget* parent)
    : QDialog(parent)
    , m_isMove(isMove)
{
  // Matches CCopyDlg::OnInitDialog: title is "Move" or "Copy", not the
  // dialog resource's generic "Copy/Move" caption.
  setWindowTitle(isMove ? "Move" : "Copy");

  auto* layout = new QVBoxLayout(this);

  m_rotate = new QRadioButton("Rotation", this);
  m_translate = new QRadioButton("Translation", this);
  m_translate->setChecked(true); // matches classic's default selection
  layout->addWidget(m_rotate);

  auto* rotateBox = new QGroupBox(this);
  auto* rotateForm = new QFormLayout(rotateBox);
  m_shiftAngle = new QLineEdit("0", this);
  m_shiftAngle->setValidator(new QDoubleValidator(m_shiftAngle));
  rotateForm->addRow("Angular shift, degrees:", m_shiftAngle);
  m_aboutX = new QLineEdit("0", this);
  m_aboutX->setValidator(new QDoubleValidator(m_aboutX));
  rotateForm->addRow("About point, X:", m_aboutX);
  m_aboutY = new QLineEdit("0", this);
  m_aboutY->setValidator(new QDoubleValidator(m_aboutY));
  rotateForm->addRow("About point, Y:", m_aboutY);
  layout->addWidget(rotateBox);

  layout->addWidget(m_translate);

  auto* translateBox = new QGroupBox(this);
  auto* translateForm = new QFormLayout(translateBox);
  m_deltaX = new QLineEdit("0", this);
  m_deltaX->setValidator(new QDoubleValidator(m_deltaX));
  translateForm->addRow("Horizontal shift:", m_deltaX);
  m_deltaY = new QLineEdit("0", this);
  m_deltaY->setValidator(new QDoubleValidator(m_deltaY));
  translateForm->addRow("Vertical shift:", m_deltaY);
  layout->addWidget(translateBox);

  auto* copiesForm = new QFormLayout;
  m_numCopies = new QLineEdit(isMove ? "0" : "1", this);
  m_numCopies->setValidator(new QIntValidator(0, 100, m_numCopies));
  // Matches CCopyDlg::OnInitDialog's SendDlgItemMessage(IDC_NCOPIES,
  // WM_ENABLE, FALSE, 0) for Move -- there's always exactly one result
  // for Move, never a stamped-out series.
  m_numCopies->setEnabled(!isMove);
  copiesForm->addRow("Number of copies:", m_numCopies);
  layout->addLayout(copiesForm);

  connect(m_rotate, &QRadioButton::toggled, this, &MoveCopyDialog::onRotateChecked);
  connect(m_translate, &QRadioButton::toggled, this, &MoveCopyDialog::onTranslateChecked);
  onTranslateChecked();

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(buttons);
}

void MoveCopyDialog::onRotateChecked()
{
  bool on = m_rotate->isChecked();
  m_shiftAngle->setEnabled(on);
  m_aboutX->setEnabled(on);
  m_aboutY->setEnabled(on);
  m_deltaX->setEnabled(!on);
  m_deltaY->setEnabled(!on);
}

void MoveCopyDialog::onTranslateChecked()
{
  onRotateChecked();
}

MoveCopyDialog::TransformMode MoveCopyDialog::transformMode() const
{
  return m_rotate->isChecked() ? TransformMode::Rotate : TransformMode::Translate;
}

double MoveCopyDialog::aboutX() const { return m_aboutX->text().toDouble(); }
double MoveCopyDialog::aboutY() const { return m_aboutY->text().toDouble(); }
double MoveCopyDialog::shiftAngleDeg() const { return m_shiftAngle->text().toDouble(); }
double MoveCopyDialog::deltaX() const { return m_deltaX->text().toDouble(); }
double MoveCopyDialog::deltaY() const { return m_deltaY->text().toDouble(); }

int MoveCopyDialog::numCopies() const
{
  return m_isMove ? 0 : std::max(1, m_numCopies->text().toInt());
}
