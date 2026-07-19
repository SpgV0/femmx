#pragma once

#include <QDialog>

#include "FemmProblem.h"

class QLineEdit;

// Edits a single FemmPointProp in place -- fields match femm's point
// property dialog (nodal applied current Jr/Ji, prescribed nodal value
// Ar/Ai).
class PointPropDialog : public QDialog {
  Q_OBJECT

  public:
  explicit PointPropDialog(FemmPointProp& prop, QWidget* parent = nullptr);

  private slots:
  void onAccept();

  private:
  FemmPointProp& m_prop;
  QLineEdit* m_name = nullptr;
  QLineEdit* m_jr = nullptr;
  QLineEdit* m_ji = nullptr;
  QLineEdit* m_ar = nullptr;
  QLineEdit* m_ai = nullptr;
};
