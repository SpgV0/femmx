#include "ProblemPropertiesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>

#include <cmath>

namespace {
// femm/StdAfx.h:99,103 -- probdlg's own DDV_MinMaxDouble bounds.
constexpr double kMinAngleMax = 33.8;
}

ProblemPropertiesDialog::ProblemPropertiesDialog(FemmProblem& problem, QWidget* parent)
    : QDialog(parent)
    , m_problem(problem)
{
  setWindowTitle("Problem Properties");

  auto* form = new QFormLayout;

  m_problemType = new QComboBox(this);
  m_problemType->addItems({ "Planar", "Axisymmetric" });
  m_problemType->setCurrentIndex(problem.problemType == FemmCoordinateType::Axisymmetric ? 1 : 0);
  form->addRow("Problem Type:", m_problemType);

  m_lengthUnits = new QComboBox(this);
  m_lengthUnits->addItems({ "Inches", "Millimeters", "Centimeters", "Meters", "Mils", "Microns" });
  m_lengthUnits->setCurrentIndex(static_cast<int>(problem.lengthUnits));
  form->addRow("Length Units:", m_lengthUnits);

  m_frequency = new QLineEdit(QString::number(problem.frequency, 'g', 17), this);
  m_frequency->setValidator(new QDoubleValidator(0.0, 1e300, 17, m_frequency));
  form->addRow("Frequency (Hz):", m_frequency);

  m_smartMesh = new QCheckBox("Use smart mesh refinement near corners", this);
  m_smartMesh->setChecked(problem.smartMesh);
  form->addRow("Smart Mesh:", m_smartMesh);

  m_depth = new QLineEdit(QString::number(problem.depth, 'g', 17), this);
  m_depth->setValidator(new QDoubleValidator(m_depth));
  form->addRow("Depth:", m_depth);

  m_precision = new QLineEdit(QString::number(problem.precision, 'g', 17), this);
  form->addRow("Precision (1e-16 to 1e-8):", m_precision);

  m_minAngle = new QLineEdit(QString::number(problem.minAngle, 'g', 17), this);
  form->addRow(QString("Min Angle (1 to %1):").arg(kMinAngleMax), m_minAngle);

  m_solver = new QComboBox(this);
  m_solver->addItems({ "Succ. Approx", "Newton" });
  m_solver->setCurrentIndex(problem.acSolver == 1 ? 1 : 0);
  form->addRow("AC Solver:", m_solver);

  m_gpuAccel = new QCheckBox("Use CUDA-accelerated linear solve, if available", this);
  m_gpuAccel->setChecked(problem.gpuAccel != 0);
  form->addRow("GPU Accel:", m_gpuAccel);

  m_prevType = new QComboBox(this);
  m_prevType->addItems({ "None", "Incremental", "Frozen" });
  m_prevType->setCurrentIndex(qBound(0, problem.prevType, 2));
  form->addRow("Prev Type:", m_prevType);

  m_prevSoln = new QLineEdit(problem.prevSoln, this);
  form->addRow("Prev Solution:", m_prevSoln);

  m_comment = new QPlainTextEdit(problem.comment, this);
  m_comment->setFixedHeight(80);
  form->addRow("Comment:", m_comment);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &ProblemPropertiesDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(buttons);

  connect(m_problemType, &QComboBox::currentIndexChanged, this, &ProblemPropertiesDialog::onProblemTypeChanged);
  onProblemTypeChanged();
}

void ProblemPropertiesDialog::onProblemTypeChanged()
{
  // Matches probdlg::OnSelchangeProbtype (femm/probdlg.cpp:110-116) --
  // depth is meaningless (fixed at 1) for an axisymmetric problem.
  bool axisymmetric = m_problemType->currentIndex() == 1;
  m_depth->setEnabled(!axisymmetric);
}

void ProblemPropertiesDialog::onAccept()
{
  bool ok = true;
  double precision = m_precision->text().toDouble(&ok);
  if (!ok || precision < 1e-16 || precision > 1e-8) {
    QMessageBox::warning(this, "Invalid Value", "Precision must be between 1e-16 and 1e-8.");
    return;
  }
  double minAngle = m_minAngle->text().toDouble(&ok);
  if (!ok || minAngle < 1.0 || minAngle > kMinAngleMax) {
    QMessageBox::warning(this, "Invalid Value", QString("Min Angle must be between 1 and %1.").arg(kMinAngleMax));
    return;
  }

  bool axisymmetric = m_problemType->currentIndex() == 1;
  m_problem.problemType = axisymmetric ? FemmCoordinateType::Axisymmetric : FemmCoordinateType::Planar;
  m_problem.lengthUnits = static_cast<FemmLengthUnits>(m_lengthUnits->currentIndex());
  m_problem.frequency = std::abs(m_frequency->text().toDouble()); // probdlg::OnOK also forces frequency >= 0
  m_problem.smartMesh = m_smartMesh->isChecked();
  m_problem.depth = axisymmetric ? 1.0 : m_depth->text().toDouble();
  m_problem.precision = precision;
  m_problem.minAngle = minAngle;
  m_problem.acSolver = m_solver->currentIndex();
  m_problem.gpuAccel = m_gpuAccel->isChecked() ? 1 : 0;
  m_problem.prevType = m_prevType->currentIndex();
  m_problem.prevSoln = m_prevSoln->text();
  m_problem.comment = m_comment->toPlainText();

  accept();
}
