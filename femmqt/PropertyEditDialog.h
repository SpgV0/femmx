#pragma once

// PropertyEditDialog.h
//
// Added by Claude (Anthropic), noreply@anthropic.com, 2026-09-13
// (issue #81).
//
// One dialog for every property record of every problem kind, built from
// a PropertyFields::Spec rather than hand-written per physics. The
// classic GUI has sixteen classes here -- MatDlg/bd_MatDlg/hd_MatDlg/
// cd_MatDlg and the same split three more times -- which is the outcome
// #81 names as the one to avoid.
//
// Values are written back only on OK, so Cancel really cancels. The
// classic dialogs bind directly to the record and rely on the caller
// discarding a copy; going through the spec's setters on accept is
// simpler to be sure of.

#include <QDialog>

#include "PropertyFields.h"

class QLineEdit;
class QComboBox;

class PropertyEditDialog : public QDialog {
  Q_OBJECT

  public:
  explicit PropertyEditDialog(PropertyFields::Spec spec, QWidget* parent = nullptr);

  private slots:
  void onAccept();
  // Applies the selector's enable/disable rules, matching the classic
  // per-type dialogs' OnSelchangeBdryformat. Fields are DISABLED rather
  // than hidden, as they are there, so the layout does not jump as the
  // type changes.
  void updateEnabledFields();

  private:
  PropertyFields::Spec m_spec;
  QLineEdit* m_name = nullptr;
  QComboBox* m_selector = nullptr;
  QVector<QLineEdit*> m_edits; // parallel to m_spec.fields
};
