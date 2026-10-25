#pragma once

#include <QDialog>
#include <QVector>

class QFormLayout;
class QLineEdit;

// A few numbers asked for in one dialog -- Scale's factor and base point,
// Mirror's two line points (#123). Those were chains of modal
// QInputDialogs, one value each, with no way back to fix an earlier one.
//
// Each field gets the objectName it is added with, so a test can fill it.
class NumberFormDialog : public QDialog {
  Q_OBJECT

  public:
  NumberFormDialog(const QString& title, QWidget* parent = nullptr);

  // Adds a row; returns its index for value(). `minimum` > -1e9 rejects
  // smaller values (Scale's factor must be positive).
  int addField(const QString& label, const QString& objectName, double initial, double minimum = -1e9);
  double value(int index) const;

  private:
  QVector<QLineEdit*> m_fields;
  QFormLayout* m_form = nullptr;
};
