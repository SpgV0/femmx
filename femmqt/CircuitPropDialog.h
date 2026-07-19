#pragma once

#include <QDialog>

#include "FemmProblem.h"

class QLineEdit;
class QComboBox;

class CircuitPropDialog : public QDialog {
  Q_OBJECT

  public:
  explicit CircuitPropDialog(FemmCircuitProp& prop, QWidget* parent = nullptr);

  private slots:
  void onAccept();

  private:
  FemmCircuitProp& m_prop;
  QLineEdit* m_name = nullptr;
  QLineEdit* m_ampsRe = nullptr;
  QLineEdit* m_ampsIm = nullptr;
  QComboBox* m_circType = nullptr;
};
