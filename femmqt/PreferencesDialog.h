#pragma once

#include <QDialog>

class QCheckBox;

// Qt equivalent of femm/GeneralPrefs.cpp's CGeneralPrefs dialog -- see
// AppPreferences.h for the exact femm.cfg fields this edits. "Default
// document type" isn't exposed here (femmqt is magnetics-only, see
// AppPreferences::defaultDocType's comment) and Lua Console has no
// meaning yet either, but both round-trip through unchanged so opening
// this dialog in the Qt GUI never loses the classic GUI's own settings.
class PreferencesDialog : public QDialog {
  Q_OBJECT

  public:
  explicit PreferencesDialog(QWidget* parent = nullptr);

  private slots:
  void onAccept();

  private:
  QCheckBox* m_smartMesh = nullptr;
  QCheckBox* m_separatePlots = nullptr;
  QCheckBox* m_showOutputWindow = nullptr;
  QCheckBox* m_darkTheme = nullptr;
};
